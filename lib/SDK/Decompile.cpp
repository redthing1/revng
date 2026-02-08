//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/SDK/Decompile.h"

#include "DecompileInternal.h"

#include "revng/Support/CommandLine.h"

namespace revng::sdk {

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
  revng::FilePath InputFilePath =
    revng::FilePath::fromLocalStorage(InputBinaryPath);
  if (llvm::Error Error = InputFilePath.check())
    return Error;
  if (llvm::Error Error = InputContainer.load(InputFilePath))
    return Error;

  if (not Config.InitialAnalysesList.empty()) {
    if (llvm::Error Error = detail::runAnalysesList(Manager,
                                                    Config.InitialAnalysesList,
                                                    Config.FunctionEntries,
                                                    Config.RestrictInitialAnalysesToSelectedFunctions))
      return Error;
  }

  llvm::Error Result = llvm::Error::success();
  if (Config.ArtifactStep == "emit-recompilable-archive"
      and not Config.FunctionEntries.empty()) {
    Result = detail::producePartialRecompilableArchive(Manager,
                                                       Output,
                                                       Config.FunctionEntries);
  } else {
    Result = detail::produceStepArtifactToFile(Manager,
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

