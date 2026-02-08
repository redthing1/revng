//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"

#include "revng/Model/Importer/Binary/ImporterCLOptions.h"

using Enum = llvm::cl::OptionEnumValue;
using SR = llvm::StringRef;
namespace cl = llvm::cl;

cl::OptionCategory BinaryImporterCategory("Binary Import Options", "");

constexpr SR DescBA = "Base address where dynamic objects should be loaded.";
cl::opt<uint64_t> BaseAddress("base",
                              cl::desc(DescBA),
                              cl::value_desc("address"),
                              cl::cat(BinaryImporterCategory),
                              cl::init(0x400000));

// TODO: This option could benefit from a better name,
//       maybe `external-debug-info`, etc.
constexpr SR DescImport = "Additional files to load debug information from.";
cl::list<std::string> ImportDebugInfo("import-debug-info",
                                      cl::desc(DescImport),
                                      cl::value_desc("path"),
                                      cl::ZeroOrMore,
                                      cl::cat(BinaryImporterCategory));

constexpr SR DescLevel = "Controls the debug information processing when "
                         "importing a binary.";
constexpr Enum No = clEnumValN(DebugInfoLevel::No,
                               "no",
                               "Ignore debug information even if it's "
                               "present.");
constexpr Enum Yes = clEnumValN(DebugInfoLevel::Yes,
                                "yes",
                                "Load debug information from the input file "
                                "and the libraries it directly depends on.");
constexpr Enum NoLib = clEnumValN(DebugInfoLevel::IgnoreLibraries,
                                  "ignore-libraries",
                                  "Load debug information from the input file "
                                  "only.");
cl::opt<DebugInfoLevel> DebugInfo("debug-info",
                                  cl::desc(DescLevel),
                                  cl::value_desc("level"),
                                  cl::values(No, Yes, NoLib),
                                  cl::cat(BinaryImporterCategory),
                                  cl::init(DebugInfoLevel::Yes));

constexpr SR DescRemote = "Allow fetching debug information from "
                          "canonical places or web.";
cl::opt<bool> EnableRemoteDebugInfo("enable-remote-debug-info",
                                    cl::desc(DescRemote),
                                    cl::cat(BinaryImporterCategory),
                                    cl::init(false));

ImporterOptions importerOptionsFromCommandLine() {
  return ImporterOptions{ .BaseAddress = BaseAddress,
                          .DebugInfo = DebugInfo,
                          .EnableRemoteDebugInfo = EnableRemoteDebugInfo,
                          .AdditionalDebugInfoPaths = ImportDebugInfo };
}
