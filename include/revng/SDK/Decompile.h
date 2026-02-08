#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include "revng/Pipes/PipelineManager.h"
#include "revng/Storage/Path.h"

namespace revng::sdk {

struct PipelineConfig {
  // YAML pipeline file paths. If empty, defaults to the revng-pipelines.yml
  // shipped under the discovered revng root.
  std::vector<std::string> Pipelines;

  // Directory containing revng analysis plugins (.so). If empty, defaults to
  // <root>/lib/revng/analyses.
  std::string AnalysesDir;
  bool LoadAnalysesFromDisk = true;

  // Extra resource roots (prefixes) to search for share/revng, abi yamls, etc.
  std::vector<std::string> ResourceRoots;

  // Pipeline enabling flags (-f).
  std::vector<std::string> EnablingFlags;

  // Optional execution directory ("resume") for caching/serialization.
  // If empty, runs fully in-memory.
  std::string ExecutionDirectory;

  // Analyses list to run before producing artifacts.
  // If empty, no analyses list is executed (useful when resuming from an
  // execdir that already contains the needed state).
  std::string InitialAnalysesList = "revng-initial-auto-analysis";

  // When FunctionEntries is non-empty, the default behavior is to run the
  // InitialAnalysesList on the whole program (all targets), then produce only
  // the selected artifact targets.
  //
  // This can be very expensive on large binaries. If this flag is enabled, the
  // SDK will try to restrict function-scoped analyses (kinds with depth 1) in
  // the InitialAnalysesList to just the selected functions.
  //
  // Tradeoff: some analyses (e.g. data layout analysis) are interprocedural and
  // restricting them can reduce the quality/precision of the recovered types.
  bool RestrictInitialAnalysesToSelectedFunctions = false;

  // Pipeline step name to run in order to produce an artifact.
  // Default: plain-C recompilable archive.
  std::string ArtifactStep = "emit-recompilable-archive";

  // Optional list of function entries to operate on (repeatable).
  //
  // Each entry can be either:
  // - a serialized MetaAddress (e.g. "0x401000:Code_x86_64"); or
  // - a raw PC address (e.g. "0x401000"), interpreted according to the
  //   architecture imported in the model (MetaAddress::fromPC).
  //
  // When non-empty:
  // - for function-scoped artifacts (FunctionKind/TaggedFunctionKind), only
  //   these targets will be requested/produced;
  // - for "emit-recompilable-archive", the SDK will produce a *partial*
  //   recompilable archive containing only the selected functions.
  std::vector<std::string> FunctionEntries;
};

llvm::Error loadAnalysesFromDirectory(llvm::StringRef AnalysesDir);

llvm::Expected<revng::pipes::PipelineManager>
createPipelineManager(const PipelineConfig &Config);

/// Produce the artifact identified by Config.ArtifactStep and store it to Output.
///
/// This is a thin convenience wrapper around PipelineManager/Runner that also
/// supports selecting specific functions via Config.FunctionEntries.
llvm::Error produceArtifact(llvm::StringRef InputBinaryPath,
                            const revng::FilePath &Output,
                            const PipelineConfig &Config = {});

llvm::Error decompileToRecompilableArchive(llvm::StringRef InputBinaryPath,
                                           const revng::FilePath &Output,
                                           const PipelineConfig &Config = {});

} // namespace revng::sdk
