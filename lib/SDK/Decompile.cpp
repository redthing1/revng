//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/SDK/Decompile.h"

#include <system_error>
#include <set>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"

#include "revng/Backend/DecompileFunction.h"
#include "revng/Backend/DecompileToSingleFile.h"
#include "revng/EarlyFunctionAnalysis/CFGStringMap.h"
#include "revng/EarlyFunctionAnalysis/ControlFlowGraphCache.h"
#include "revng/HeadersGeneration/PTMLHeaderBuilder.h"
#include "revng/Model/IRHelpers.h"
#include "revng/Pipeline/AllRegistries.h"
#include "revng/Pipeline/AnalysesList.h"
#include "revng/Pipeline/ContainerSet.h"
#include "revng/Pipeline/Target.h"
#include "revng/Pipes/Containers.h"
#include "revng/Pipes/Kinds.h"
#include "revng/Pipes/ModelGlobal.h"
#include "revng/Support/CommandLine.h"
#include "revng/Support/Error.h"
#include "revng/Support/GzipTarFile.h"
#include "revng/Support/MetaAddress.h"
#include "revng/Support/PathList.h"
#include "revng/Support/ResourceFinder.h"
#include "revng/Support/RuntimeDeps.h"

using pipeline::ContainerToTargetsMap;
using pipeline::Registry;
using pipeline::TargetInStepSet;

namespace revng::sdk {

static std::string defaultPipelinesYAML() {
  return joinPath(getCurrentRoot(), "share/revng/pipelines/revng-pipelines.yml");
}

static std::string defaultAnalysesDir() {
  return joinPath(getCurrentRoot(), "lib/revng/analyses");
}

llvm::Error loadAnalysesFromDirectory(llvm::StringRef Dir) {
  namespace fs = llvm::sys::fs;

  if (not fs::is_directory(Dir))
    return revng::createError("analyses-dir is not a directory: %s",
                              Dir.str().c_str());

  std::error_code EC;
  std::vector<std::string> Libs;
  for (fs::directory_iterator It(Dir, EC), End; It != End && !EC;
       It.increment(EC)) {
    llvm::StringRef Path = It->path();
    if (Path.ends_with(".so"))
      Libs.emplace_back(Path.str());
  }

  if (EC)
    return llvm::createStringError(EC,
                                   "failed to iterate analyses-dir: %s",
                                   Dir.str().c_str());

  llvm::sort(Libs);

  for (const std::string &Path : Libs) {
    std::string Err;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Path.c_str(), &Err)) {
      if (Err.empty())
        Err = "<no error message>";
      return revng::createError("failed to load %s: %s",
                                Path.c_str(),
                                Err.c_str());
    }
  }

  return llvm::Error::success();
}

llvm::Expected<revng::pipes::PipelineManager>
createPipelineManager(const PipelineConfig &Config) {
  // Make sure runtime deps needed by dlopen'ed components (e.g. libtcg) are
  // part of the initial load set.
  revng::runtime::ensureRuntimeDependenciesLoaded();

  // User-provided resource roots should take precedence over auto-discovered
  // ones (ResourceFinder prepends paths), so preserve user order explicitly.
  for (auto It = Config.ResourceRoots.rbegin(); It != Config.ResourceRoots.rend();
       ++It) {
    revng::addResourceRoot(*It);
  }

  std::vector<std::string> Pipelines = Config.Pipelines;
  if (Pipelines.empty())
    Pipelines.emplace_back(defaultPipelinesYAML());

  if (Config.LoadAnalysesFromDisk) {
    const std::string &Dir = Config.AnalysesDir.empty() ? defaultAnalysesDir() :
                                                          Config.AnalysesDir;
    if (auto Error = loadAnalysesFromDirectory(Dir))
      return Error;
  }

  Registry::runAllInitializationRoutines();

  return revng::pipes::PipelineManager::create(Pipelines,
                                               Config.EnablingFlags,
                                               Config.ExecutionDirectory);
}

static llvm::Expected<pipeline::TargetsList>
computeSelectedTargetsForKind(revng::pipes::PipelineManager &Manager,
                              const pipeline::Kind &Kind,
                              llvm::ArrayRef<std::string> FunctionEntries);

static llvm::Error runAnalysesList(revng::pipes::PipelineManager &Manager,
                                   llvm::StringRef Name,
                                   llvm::ArrayRef<std::string> FunctionEntries,
                                   bool RestrictToSelectedFunctions) {
  auto &Runner = Manager.getRunner();
  if (not Runner.hasAnalysesList(Name)) {
    return revng::createError("analysis list '%s' is not available",
                              Name.str().c_str());
  }

  pipeline::AnalysesList AL = Runner.getAnalysesList(Name);

  // Default behavior: use the upstream semantics for AnalysesLists, i.e. run
  // each analysis on all its accepted targets (whole-program).
  if (not RestrictToSelectedFunctions or FunctionEntries.empty()) {
    TargetInStepSet InvMap;
    auto MaybeDiffs = Manager.runAnalyses(AL, InvMap);
    if (not MaybeDiffs)
      return MaybeDiffs.takeError();
    return llvm::Error::success();
  }

  // Selected-functions behavior: run the list analysis-by-analysis, and try to
  // restrict function-scoped analyses (kinds with depth 1) to the selected
  // function targets.
  for (const pipeline::AnalysisReference &Ref : AL) {
    const pipeline::Step &Step = Runner.getStep(Ref.getStepName());
    const pipeline::AnalysisWrapper &Analysis = Step.getAnalysis(Ref.getAnalysisName());

    ContainerToTargetsMap Map;
    const std::vector<std::string> &Containers =
      Analysis->getRunningContainersNames();
    for (size_t I = 0; I < Containers.size(); ++I) {
      for (const pipeline::Kind *K : Analysis->getAcceptedKinds(I)) {
        if (K == nullptr)
          continue;

        if (K->depth() == 1) {
          auto MaybeTargets = computeSelectedTargetsForKind(Manager,
                                                           *K,
                                                           FunctionEntries);
          if (not MaybeTargets)
            return MaybeTargets.takeError();
          Map.add(Containers[I], MaybeTargets.get());
        } else {
          Map.add(Containers[I], pipeline::TargetsList::allTargets(Manager.context(),
                                                                  *K));
        }
      }
    }

    TargetInStepSet InvMap;
    auto MaybeDiff = Manager.runAnalysis(Ref.getAnalysisName(),
                                         Step.getName(),
                                         Map,
                                         InvMap);
    if (not MaybeDiff)
      return MaybeDiff.takeError();
  }

  return llvm::Error::success();
}

static llvm::Expected<MetaAddress>
parseFunctionEntry(llvm::StringRef Text, model::Architecture::Values Arch) {
  Text = Text.trim();
  if (Text.empty())
    return revng::createError("empty function entry");

  if (Text.contains(MetaAddress::Separator)) {
    MetaAddress Entry = MetaAddress::fromString(Text);
    if (not Entry.isValid())
      return revng::createError("invalid MetaAddress: '%s'",
                                Text.str().c_str());
    return Entry;
  }

  uint64_t PC = 0;
  if (Text.getAsInteger(0, PC))
    return revng::createError("invalid address: '%s'", Text.str().c_str());

  MetaAddress Entry = MetaAddress::fromPC(Arch, PC);
  if (not Entry.isValid())
    return revng::createError("invalid PC for architecture: '%s'",
                              Text.str().c_str());

  return Entry;
}

static llvm::Expected<std::set<MetaAddress>>
parseAndValidateFunctionEntries(const model::Binary &Model,
                                llvm::ArrayRef<std::string> Entries) {
  std::set<MetaAddress> Result;
  auto Arch = Model.Architecture();

  for (const std::string &Text : Entries) {
    auto MaybeEntry = parseFunctionEntry(Text, Arch);
    if (not MaybeEntry)
      return MaybeEntry.takeError();

    MetaAddress Entry = *MaybeEntry;
    if (Model.Functions().find(Entry) == Model.Functions().end()) {
      return revng::createError("no function with entry '%s' in the model",
                                Entry.toString().c_str());
    }

    Result.insert(Entry);
  }

  return Result;
}

static llvm::Expected<pipeline::TargetsList>
computeSelectedTargetsForKind(revng::pipes::PipelineManager &Manager,
                              const pipeline::Kind &Kind,
                              llvm::ArrayRef<std::string> FunctionEntries) {
  if (FunctionEntries.empty())
    return Kind.allTargets(Manager.context());

  if (Kind.depth() != 1) {
    return revng::createError("kind '%s' does not support function selection "
                              "(expected depth 1, got %zu)",
                              Kind.name().str().c_str(),
                              Kind.depth());
  }

  const model::Binary &Model = *revng::getModelFromContext(Manager.context());
  auto MaybeEntries = parseAndValidateFunctionEntries(Model, FunctionEntries);
  if (not MaybeEntries)
    return MaybeEntries.takeError();

  pipeline::TargetsList Targets;
  for (const MetaAddress &Entry : MaybeEntries.get())
    Targets.push_back(pipeline::Target(Entry.toString(), Kind));

  return Targets;
}

static llvm::Error produceStepArtifactToFile(revng::pipes::PipelineManager &Manager,
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

  ContainerToTargetsMap Map;
  Map.add(ContainerName, MaybeTargets.get());
  if (llvm::Error Error = Runner.run(StepName, Map); Error)
    return Error;

  const pipeline::TargetsList &Targets = Map.at(ContainerName);
  auto Produced = MaybeContainer->second->cloneFiltered(Targets);
  return Produced->store(Output);
}

static llvm::Error
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
  ContainerToTargetsMap Map;
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
                     llvm::ArrayRef{ DecompiledC.data(), DecompiledC.length() });
  }

  {
    std::string ModelHeader;
    llvm::raw_string_ostream Out{ ModelHeader };
    B.setOutputStream(Out);

    ptml::HeaderBuilder HB = B;
    HB.printModelHeader(&Module);

    Out.flush();
    TarWriter.append("decompiled/types-and-globals.h",
                     llvm::ArrayRef{ ModelHeader.data(), ModelHeader.length() });
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
    auto Path = revng::ResourceFinder.findFile("share/revng/include/attributes.h");
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

llvm::Error decompileToRecompilableArchive(llvm::StringRef InputBinaryPath,
                                           const revng::FilePath &Output,
                                           const PipelineConfig &Config) {
  return produceArtifact(InputBinaryPath, Output, Config);
}

llvm::Error produceArtifact(llvm::StringRef InputBinaryPath,
                            const revng::FilePath &Output,
                            const PipelineConfig &Config) {
  auto MaybeManager = createPipelineManager(Config);
  if (not MaybeManager)
    return MaybeManager.takeError();

  auto Manager = std::move(MaybeManager.get());
  auto &Runner = Manager.getRunner();

  // Load input binary into the pipeline "input" container.
  auto &InputContainer = Runner.begin()->containers()["input"];
  InputPath = InputBinaryPath.str();
  revng::FilePath InputFilePath = revng::FilePath::fromLocalStorage(InputBinaryPath);
  if (llvm::Error Error = InputFilePath.check())
    return Error;
  if (llvm::Error Error = InputContainer.load(InputFilePath))
    return Error;

  if (not Config.InitialAnalysesList.empty()) {
    if (llvm::Error Error = runAnalysesList(Manager,
                                            Config.InitialAnalysesList,
                                            Config.FunctionEntries,
                                            Config.RestrictInitialAnalysesToSelectedFunctions))
      return Error;
  }

  llvm::Error Result = llvm::Error::success();
  if (Config.ArtifactStep == "emit-recompilable-archive"
      and not Config.FunctionEntries.empty()) {
    Result = producePartialRecompilableArchive(Manager,
                                               Output,
                                               Config.FunctionEntries);
  } else {
    Result = produceStepArtifactToFile(Manager,
                                       Config.ArtifactStep,
                                       Output,
                                       Config.FunctionEntries);
  }

  if (Result)
    return Result;

  // Persist the execution directory if configured.
  if (not Config.ExecutionDirectory.empty())
    return Manager.store();

  return llvm::Error::success();
}

} // namespace revng::sdk
