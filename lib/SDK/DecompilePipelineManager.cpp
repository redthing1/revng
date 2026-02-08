//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/SDK/Decompile.h"

#include <system_error>

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"

#include "revng/Pipeline/AllRegistries.h"
#include "revng/Support/Error.h"
#include "revng/Support/PathList.h"
#include "revng/Support/ResourceFinder.h"
#include "revng/Support/RuntimeDeps.h"

using pipeline::Registry;

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

} // namespace revng::sdk

