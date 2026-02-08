//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

#include "revng/Model/Importer/Binary/ImporterCLOptions.h"
#include "revng/SDK/Decompile.h"
#include "revng/Storage/CLPathOpt.h"
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
  AnalysesList("analysis-list",
               cl::desc("Analyses list to run before producing artifacts "
                        "(default: revng-initial-auto-analysis)"),
               cl::cat(SDKCategory));

static cl::opt<bool>
  NoInitialAnalyses("no-initial-analyses",
                    cl::desc("Do not run the initial analyses list "
                             "(useful with --execdir to resume)."),
                    cl::init(false),
                    cl::cat(SDKCategory));

static cl::opt<std::string>
  Step("step",
       cl::desc("Pipeline step to run to produce the requested artifact "
                "(default: emit-recompilable-archive)"),
       cl::cat(SDKCategory));

static cl::opt<bool>
  ListSteps("list-steps",
            cl::desc("List pipeline steps and exit."),
            cl::init(false),
            cl::cat(SDKCategory));

static cl::opt<bool>
  ListAnalysesLists("list-analyses-lists",
                    cl::desc("List available analyses lists and exit."),
                    cl::init(false),
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
                                        cl::cat(SDKCategory));

static revng::OutputPathOpt Output("o",
                                   cl::desc("Output filepath of produced artifact"),
                                   cl::cat(SDKCategory));

static llvm::ExitOnError AbortOnError;

static void listPipeline(const revng::sdk::PipelineConfig &Config,
                         bool PrintSteps,
                         bool PrintAnalysesLists) {
  auto MaybeManager = revng::sdk::createPipelineManager(Config);
  if (not MaybeManager)
    AbortOnError(MaybeManager.takeError());

  revng::pipes::PipelineManager Manager = std::move(MaybeManager.get());
  pipeline::Runner &Runner = Manager.getRunner();

  if (PrintSteps) {
    llvm::outs() << "Pipeline steps:\n";
    for (const pipeline::Step &S : Runner)
      llvm::outs() << "- " << S.getName() << "\n";
  }

  if (PrintAnalysesLists) {
    llvm::outs() << "Analyses lists:\n";
    for (size_t I = 0; I < Runner.getAnalysesListCount(); ++I)
      llvm::outs() << "- " << Runner.getAnalysesList(I).getName() << "\n";
  }
}

int main(int argc, char *argv[]) {
  // Show only SDK flags and the binary importer flags (e.g. --base, --debug-info).
  revng::InitRevng X(argc, argv, "", { &SDKCategory, &BinaryImporterCategory });

  revng::sdk::PipelineConfig Config;
  if (not PipelinePath.empty())
    Config.Pipelines = { PipelinePath };
  if (not AnalysesDir.empty())
    Config.AnalysesDir = AnalysesDir;
  Config.ResourceRoots.assign(ResourceRoots.begin(), ResourceRoots.end());
  if (not AnalysesList.empty())
    Config.InitialAnalysesList = AnalysesList;
  if (NoInitialAnalyses)
    Config.InitialAnalysesList = "";
  if (not Step.empty())
    Config.ArtifactStep = Step;
  if (not ExecDir.empty())
    Config.ExecutionDirectory = ExecDir;
  Config.FunctionEntries.assign(FunctionEntries.begin(), FunctionEntries.end());

  if (ListSteps or ListAnalysesLists) {
    listPipeline(Config, ListSteps, ListAnalysesLists);
    return EXIT_SUCCESS;
  }

  if (InputBinary.empty())
    AbortOnError(revng::createError("missing required <binary> argument"));

  auto MaybeOutput = AbortOnError(Output.get());
  if (not MaybeOutput.has_value())
    AbortOnError(revng::createError("missing required -o <output-path>"));

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
