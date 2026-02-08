//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "DecompileInternal.h"

#include <set>

#include "llvm/Support/MemoryBuffer.h"

#include "revng/Backend/DecompileFunction.h"
#include "revng/Backend/DecompileToSingleFile.h"
#include "revng/EarlyFunctionAnalysis/CFGStringMap.h"
#include "revng/EarlyFunctionAnalysis/ControlFlowGraphCache.h"
#include "revng/HeadersGeneration/PTMLHeaderBuilder.h"
#include "revng/Model/IRHelpers.h"
#include "revng/Pipes/Containers.h"
#include "revng/Pipes/Kinds.h"
#include "revng/Pipes/ModelGlobal.h"
#include "revng/Pipes/PipelineManager.h"
#include "revng/Storage/Path.h"
#include "revng/Support/Error.h"
#include "revng/Support/GzipTarFile.h"
#include "revng/Support/ResourceFinder.h"

namespace revng::sdk::detail {

llvm::Error produceStepArtifactToFile(revng::pipes::PipelineManager &Manager,
                                      llvm::StringRef StepName,
                                      const revng::FilePath &Output,
                                      llvm::ArrayRef<std::string>
                                        FunctionEntries) {
  auto &Runner = Manager.getRunner();
  if (not Runner.containsStep(StepName))
    return revng::createError("no pipeline step named '%s'",
                              StepName.str().c_str());

  auto &Step = Runner.getStep(StepName);
  auto MaybeContainer = Step.getArtifactsContainer();
  if (not MaybeContainer) {
    return revng::createError("step '%s' is not associated to an artifact",
                              StepName.str().c_str());
  }

  auto ContainerName = MaybeContainer->first();
  auto *Kind = Step.getArtifactsKind();
  if (Kind == nullptr) {
    return revng::createError("step '%s' has no artifacts kind",
                              StepName.str().c_str());
  }

  auto MaybeTargets = computeSelectedTargetsForKind(Manager,
                                                    *Kind,
                                                    FunctionEntries);
  if (not MaybeTargets)
    return MaybeTargets.takeError();

  pipeline::ContainerToTargetsMap Map;
  Map.add(ContainerName, MaybeTargets.get());
  if (llvm::Error Error = Runner.run(StepName, Map); Error)
    return Error;

  const pipeline::TargetsList &Targets = Map.at(ContainerName);
  auto Produced = MaybeContainer->second->cloneFiltered(Targets);
  return Produced->store(Output);
}

llvm::Error
producePartialRecompilableArchive(revng::pipes::PipelineManager &Manager,
                                  const revng::FilePath &Output,
                                  llvm::ArrayRef<std::string> Entries) {
  const model::Binary &Model = *revng::getModelFromContext(Manager.context());
  auto MaybeEntries = parseAndValidateFunctionEntries(Model, Entries);
  if (not MaybeEntries)
    return MaybeEntries.takeError();
  const std::set<MetaAddress> &Selected = MaybeEntries.get();

  auto &Runner = Manager.getRunner();
  constexpr llvm::StringRef PrepareStepName = "embed-statement-comments";
  if (not Runner.containsStep(PrepareStepName)) {
    return revng::createError("no pipeline step named '%s' (needed for partial "
                              "recompilable archive generation)",
                              PrepareStepName.str().c_str());
  }

  // Materialize only the selected functions in the containers we need.
  pipeline::ContainerToTargetsMap Map;
  constexpr llvm::StringRef FunctionsContainerName = "functions.bc.zstd";
  constexpr llvm::StringRef CFGContainerName = "cfg.yml.tar.gz";
  for (const MetaAddress &Entry : Selected) {
    Map.add(FunctionsContainerName,
            pipeline::Target(Entry.toString(), revng::kinds::StackAccessesSegregated));
    Map.add(CFGContainerName,
            pipeline::Target(Entry.toString(), revng::kinds::CFG));
  }

  if (llvm::Error Error = Runner.run(PrepareStepName, Map); Error)
    return Error;

  auto &Step = Runner.getStep(PrepareStepName);
  pipeline::LLVMContainer &IRContainer =
    Step.containers().get<pipeline::LLVMContainer>(FunctionsContainerName);
  const revng::pipes::CFGMap &CFGMap =
    Step.containers().get<revng::pipes::CFGMap>(CFGContainerName);

  auto MaybeWritable = Output.getWritableFile(revng::ContentEncoding::Gzip);
  if (not MaybeWritable)
    return MaybeWritable.takeError();
  auto &Writable = MaybeWritable.get();

  revng::GzipTarWriter TarWriter{ Writable->os() };

  llvm::Module &Module = IRContainer.getModule();

  ptml::ModelCBuilder B(llvm::nulls(),
                        Model,
                        /* EnableTaglessMode = */ true,
                        { .EnableStackFrameInlining = false });

  {
    ControlFlowGraphCache Cache{ CFGMap };
    revng::pipes::DecompileStringMap DecompiledFunctions("tmp");

    for (pipeline::Target &Target : CFGMap.enumerate()) {
      llvm::StringRef TargetKey = Target.getPathComponents()[0];
      MetaAddress Entry = MetaAddress::fromString(TargetKey);
      if (not Entry.isValid()) {
        return revng::createError("invalid function entry in CFG target: '%s'",
                                  TargetKey.str().c_str());
      }

      auto It = Model.Functions().find(Entry);
      if (It == Model.Functions().end()) {
        return revng::createError("CFG refers to '%s' but the model contains no "
                                  "function with that entry",
                                  Entry.toString().c_str());
      }

      const model::Function &Function = *It;
      std::string LLVMName = llvmName(Function);
      auto *F = Module.getFunction(LLVMName);
      if (F == nullptr) {
        return revng::createError("missing LLVM function for model entry '%s' "
                                  "(expected LLVM name '%s')",
                                  Entry.toString().c_str(),
                                  LLVMName.c_str());
      }

      std::string CCode = decompile(Cache, *F, Model, B);
      DecompiledFunctions.insert_or_assign(Entry, std::move(CCode));
    }

    std::string DecompiledC;
    llvm::raw_string_ostream Out{ DecompiledC };
    B.setOutputStream(Out);
    printSingleCFile(B, DecompiledFunctions, {} /* Targets */);
    Out.flush();

    TarWriter.append("decompiled/functions.c",
                     llvm::ArrayRef{ DecompiledC.data(),
                                     DecompiledC.length() });
  }

  {
    std::string ModelHeader;
    llvm::raw_string_ostream Out{ ModelHeader };
    B.setOutputStream(Out);

    ptml::HeaderBuilder HB = B;
    HB.printModelHeader(&Module);

    Out.flush();
    TarWriter.append("decompiled/types-and-globals.h",
                     llvm::ArrayRef{ ModelHeader.data(),
                                     ModelHeader.length() });
  }

  {
    std::string HelpersHeader;
    llvm::raw_string_ostream Out{ HelpersHeader };
    B.setOutputStream(Out);

    ptml::HeaderBuilder HB = B;
    HB.printHelpersHeader(Module);

    Out.flush();
    TarWriter.append("decompiled/helpers.h",
                     llvm::ArrayRef{ HelpersHeader.data(),
                                     HelpersHeader.length() });
  }

  {
    auto Path =
      revng::ResourceFinder.findFile("share/revng/include/attributes.h");
    if (not Path or Path->empty())
      return revng::createError("can't find attributes.h");

    auto BufferOrError = llvm::MemoryBuffer::getFileOrSTDIN(*Path);
    auto Buffer = llvm::cantFail(llvm::errorOrToExpected(std::move(BufferOrError)));
    TarWriter.append("decompiled/attributes.h",
                     { Buffer->getBufferStart(), Buffer->getBufferSize() });
  }

  {
    auto Path =
      revng::ResourceFinder.findFile("share/revng/include/primitive-types.h");
    if (not Path or Path->empty())
      return revng::createError("can't find primitive-types.h");

    auto BufferOrError = llvm::MemoryBuffer::getFileOrSTDIN(*Path);
    auto Buffer = llvm::cantFail(llvm::errorOrToExpected(std::move(BufferOrError)));
    TarWriter.append("decompiled/primitive-types.h",
                     { Buffer->getBufferStart(), Buffer->getBufferSize() });
  }

  TarWriter.close();
  return Writable->commit();
}

} // namespace revng::sdk::detail

