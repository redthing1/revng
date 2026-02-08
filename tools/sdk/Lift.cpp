//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

#include "revng/SDK/Direct/Direct.h"
#include "revng/Storage/CLPathOpt.h"
#include "revng/Support/Error.h"
#include "revng/Support/IRHelpers.h"
#include "revng/Support/InitRevng.h"
#include "revng/Support/ResourceFinder.h"

namespace cl = llvm::cl;

static cl::OptionCategory SDKLiftCategory("SDK Lift Options", "");

static cl::opt<uint64_t> BaseAddress("base",
                                     cl::desc("Base address where dynamic "
                                              "objects should be loaded."),
                                     cl::value_desc("address"),
                                     cl::init(0x400000),
                                     cl::cat(SDKLiftCategory));

static cl::list<std::string> ImportDebugInfo("import-debug-info",
                                             cl::desc("Additional files to "
                                                      "load debug information "
                                                      "from."),
                                             cl::value_desc("path"),
                                             cl::ZeroOrMore,
                                             cl::cat(SDKLiftCategory));

static cl::opt<DebugInfoLevel>
  DebugInfo("debug-info",
            cl::desc("Controls debug information processing when importing a "
                     "binary."),
            cl::value_desc("level"),
            cl::values(clEnumValN(DebugInfoLevel::No,
                                  "no",
                                  "Ignore debug information even if it's "
                                  "present."),
                       clEnumValN(DebugInfoLevel::Yes,
                                  "yes",
                                  "Load debug information from the input file "
                                  "and the libraries it directly depends on."),
                       clEnumValN(DebugInfoLevel::IgnoreLibraries,
                                  "ignore-libraries",
                                  "Load debug information from the input file "
                                  "only.")),
            cl::init(DebugInfoLevel::Yes),
            cl::cat(SDKLiftCategory));

static cl::opt<bool>
  EnableRemoteDebugInfo("enable-remote-debug-info",
                        cl::desc("Allow fetching debug information from "
                                 "canonical places or web."),
                        cl::init(false),
                        cl::cat(SDKLiftCategory));

static cl::list<std::string>
  ResourceRoots("resource-root",
                cl::desc("Extra resource roots (prefixes) to search for "
                         "share/revng and other runtime resources. "
                         "May be repeated."),
                cl::ZeroOrMore,
                cl::cat(SDKLiftCategory));

static cl::opt<std::string>
  Emit("emit",
       cl::desc("Output format: bc (default) or ll"),
       cl::value_desc("format"),
       cl::init("bc"),
       cl::cat(SDKLiftCategory));

static revng::OutputPathOpt Output("o",
                                   cl::desc("Output file for the lifted LLVM "
                                            "module"),
                                   cl::cat(SDKLiftCategory));

static cl::opt<std::string> InputBinary(cl::Positional,
                                        cl::desc("<binary>"),
                                        cl::Required,
                                        cl::cat(SDKLiftCategory));

static llvm::ExitOnError AbortOnError;

static ImporterOptions importerOptionsFromFlags() {
  return ImporterOptions{ .BaseAddress = BaseAddress,
                          .DebugInfo = DebugInfo,
                          .EnableRemoteDebugInfo = EnableRemoteDebugInfo,
                          .AdditionalDebugInfoPaths = ImportDebugInfo };
}

static void applyResourceRoots() {
  // addResourceRoot prepends, so add in reverse to preserve user order.
  std::vector<std::string> Roots(ResourceRoots.begin(), ResourceRoots.end());
  for (auto It = Roots.rbegin(); It != Roots.rend(); ++It)
    revng::addResourceRoot(*It);
}

int main(int argc, char *argv[]) {
  revng::InitRevng X(argc, argv, "", { &SDKLiftCategory });
  applyResourceRoots();

  auto MaybeOutput = AbortOnError(Output.get());
  if (not MaybeOutput.has_value())
    AbortOnError(revng::createError("missing required -o <output-path>"));

  ImporterOptions Options = importerOptionsFromFlags();
  ::Model Model = AbortOnError(revng::sdk::direct::importModel(InputBinary,
                                                               Options));

  revng::pypeline::BinariesContainer Binaries;
  AbortOnError(revng::sdk::direct::loadBinary(InputBinary, Binaries));

  revng::pypeline::LLVMRootContainer Root;
  AbortOnError(revng::sdk::direct::lift(Model, Binaries, Root));

  auto Writable = AbortOnError(MaybeOutput->getWritableFile());

  if (Emit == "ll") {
    Root.getModule().print(Writable->os(), nullptr);
  } else if (Emit == "bc") {
    llvm::SmallVector<char, 0> Buffer;
    writeBitcode(Root.getModule(), Buffer);
    Writable->os() << llvm::StringRef(Buffer.data(), Buffer.size());
  } else {
    AbortOnError(revng::createError("invalid --emit '%s' (expected 'bc' or 'll')",
                                   Emit.c_str()));
  }

  AbortOnError(Writable->commit());
  return EXIT_SUCCESS;
}
