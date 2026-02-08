//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

#include "revng/SDK/Decompile.h"
#include "revng/Storage/CLPathOpt.h"
#include "revng/Support/CommandLine.h"
#include "revng/Support/Error.h"
#include "revng/Support/InitRevng.h"

namespace cl = llvm::cl;

static cl::OptionCategory SDKCategory("SDK Options", "");

static cl::opt<std::string> PipelinePath("pipeline",
                                         cl::desc("Path to revng pipelines.yml "
                                                  "(defaults to share/revng/"
                                                  "pipelines/revng-pipelines.yml "
                                                  "under the discovered root)"),
                                         cl::cat(SDKCategory));

static cl::opt<std::string> AnalysesDir("analyses-dir",
                                        cl::desc("Directory containing revng "
                                                 "analysis plugins (.so). "
                                                 "Defaults to lib/revng/analyses "
                                                 "under the discovered root."),
                                        cl::cat(SDKCategory));

static cl::opt<std::string>
  Step("step",
       cl::desc("Pipeline step to run to produce the requested artifact "
                "(default: emit-recompilable-archive)"),
       cl::cat(SDKCategory));

static cl::opt<std::string>
  AnalysisScope("analysis-scope",
                cl::desc("Scope for function-scoped analyses in the initial "
                         "analyses list when --function-entry is present. "
                         "Allowed: whole (default), selected"),
                cl::init("whole"),
                cl::cat(SDKCategory));

static cl::list<std::string>
  FunctionEntries("function-entry",
                  cl::desc("Select specific functions by entry address "
                           "(MetaAddress like 0x401000:Code_x86_64 or raw PC "
                           "like 0x401000). May be repeated."),
                  cl::cat(SDKCategory));

static cl::opt<std::string>
  ExecDir("execdir",
          cl::desc("Execution directory used to cache/resume pipeline state "
                   "(optional)"),
          cl::cat(SDKCategory));

static cl::list<std::string>
  ResourceRoots("resource-root",
                cl::desc("Extra resource roots (prefixes) to search for "
                         "share/revng and other runtime resources. "
                         "May be repeated."),
                cl::cat(SDKCategory));

static cl::opt<std::string> InputBinary(cl::Positional,
                                        cl::desc("<binary>"),
                                        cl::Required,
                                        cl::cat(SDKCategory));

static revng::OutputPathOpt Output("o",
                                   cl::desc("Output filepath of produced artifact"),
                                   cl::cat(SDKCategory));

static llvm::ExitOnError AbortOnError;

int main(int argc, char *argv[]) {
  // Also register the global revng options (e.g. importer knobs like --base,
  // --debug-info) so sdk-decompile can be used for non-default layouts.
  revng::InitRevng X(argc, argv, "", { &SDKCategory, &MainCategory });

  auto MaybeOutput = AbortOnError(Output.get());
  if (not MaybeOutput.has_value())
    AbortOnError(revng::createError("missing required -o <output-path>"));

  revng::sdk::PipelineConfig Config;
  if (not PipelinePath.empty())
    Config.Pipelines = { PipelinePath };
  if (not AnalysesDir.empty())
    Config.AnalysesDir = AnalysesDir;
  Config.ResourceRoots.assign(ResourceRoots.begin(), ResourceRoots.end());
  if (not Step.empty())
    Config.ArtifactStep = Step;
  if (not ExecDir.empty())
    Config.ExecutionDirectory = ExecDir;
  Config.FunctionEntries.assign(FunctionEntries.begin(), FunctionEntries.end());

  if (AnalysisScope == "selected") {
    Config.RestrictInitialAnalysesToSelectedFunctions = true;
  } else if (AnalysisScope == "whole") {
    Config.RestrictInitialAnalysesToSelectedFunctions = false;
  } else {
    AbortOnError(revng::createError("invalid --analysis-scope '%s' (expected "
                                   "'whole' or 'selected')",
                                   AnalysisScope.c_str()));
  }

  AbortOnError(revng::sdk::produceArtifact(InputBinary, *MaybeOutput, Config));

  return EXIT_SUCCESS;
}
