//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"

#include "revng/Model/Importer/Binary/ImporterOptions.h"
#include "revng/SDK/Direct/Direct.h"
#include "revng/Support/Error.h"
#include "revng/Support/IRHelpers.h"
#include "revng/Support/InitRevng.h"
#include "revng/Support/PathList.h"
#include "revng/Support/ResourceFinder.h"

namespace cl = llvm::cl;

static cl::OptionCategory Category("SDK Direct E2E Example Options", "");

static cl::opt<uint64_t> BaseAddress("base",
                                     cl::desc("Base address where dynamic "
                                              "objects should be loaded."),
                                     cl::value_desc("address"),
                                     cl::init(0x400000),
                                     cl::cat(Category));

static cl::list<std::string> ImportDebugInfo("import-debug-info",
                                             cl::desc("Additional files to "
                                                      "load debug information "
                                                      "from."),
                                             cl::value_desc("path"),
                                             cl::ZeroOrMore,
                                             cl::cat(Category));

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
            cl::cat(Category));

static cl::opt<bool>
  EnableRemoteDebugInfo("enable-remote-debug-info",
                        cl::desc("Allow fetching debug information from "
                                 "canonical places or web."),
                        cl::init(false),
                        cl::cat(Category));

static cl::list<std::string>
  ResourceRoots("resource-root",
                cl::desc("Extra resource roots (prefixes) to search for "
                         "share/revng and other runtime resources. "
                         "May be repeated."),
                cl::ZeroOrMore,
                cl::cat(Category));

static cl::list<std::string>
  FunctionEntries("function-entry",
                  cl::desc("Select specific functions by entry address "
                           "(MetaAddress like 0x401000:Code_x86_64 or raw PC "
                           "like 0x401000). May be repeated."),
                  cl::ZeroOrMore,
                  cl::cat(Category));

static cl::opt<bool>
  AllFunctions("all-functions",
               cl::desc("Run CFG recovery + isolation on all functions in the "
                        "imported model."),
               cl::init(false),
               cl::cat(Category));

static cl::opt<std::string>
  Emit("emit",
       cl::desc("Output format for emitted LLVM modules: bc (default) or ll"),
       cl::value_desc("format"),
       cl::init("bc"),
       cl::cat(Category));

static cl::opt<std::string> InputBinary(cl::Positional,
                                        cl::desc("<binary>"),
                                        cl::Required,
                                        cl::cat(Category));

static cl::opt<std::string> OutDir(cl::Positional,
                                   cl::desc("<out-dir>"),
                                   cl::Required,
                                   cl::cat(Category));

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

static void ensureDir(llvm::StringRef Dir) {
  if (std::error_code EC = llvm::sys::fs::create_directories(Dir))
    AbortOnError(llvm::errorCodeToError(EC));
}

static std::string safeFilename(const MetaAddress &Entry) {
  std::string Name = Entry.toString();
  for (char &C : Name) {
    if (C == ':' || C == '/')
      C = '_';
  }
  return Name;
}

static void writeBytesToFile(llvm::StringRef Path, llvm::StringRef Data) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(Path, EC, llvm::sys::fs::OF_None);
  if (EC)
    AbortOnError(llvm::errorCodeToError(EC));
  OS << Data;
}

static void writeModuleToFile(const llvm::Module &M, llvm::StringRef Path) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(Path, EC, llvm::sys::fs::OF_None);
  if (EC)
    AbortOnError(llvm::errorCodeToError(EC));

  if (Emit == "ll") {
    M.print(OS, nullptr);
  } else if (Emit == "bc") {
    llvm::SmallVector<char, 0> Buffer;
    writeBitcode(M, Buffer);
    OS << llvm::StringRef(Buffer.data(), Buffer.size());
  } else {
    AbortOnError(revng::createError("invalid --emit '%s' (expected 'bc' or 'll')",
                                   Emit.c_str()));
  }
}

int main(int argc, char *argv[]) {
  revng::InitRevng X(argc, argv, "", { &Category });
  applyResourceRoots();

  ensureDir(OutDir);

  ImporterOptions Options = importerOptionsFromFlags();
  ::Model Model = AbortOnError(revng::sdk::direct::importModel(InputBinary,
                                                               Options));

  revng::pypeline::BinariesContainer Binaries;
  AbortOnError(revng::sdk::direct::loadBinary(InputBinary, Binaries));

  revng::pypeline::LLVMRootContainer Root;
  AbortOnError(revng::sdk::direct::lift(Model, Binaries, Root));

  // Emit the whole-binary root module.
  writeModuleToFile(Root.getModule(), joinPath(OutDir, "lift." + Emit));

  // CFG/isolation is shown only when explicitly requested (either by selecting
  // functions or by requesting all functions).
  if (FunctionEntries.empty() and not AllFunctions)
    return EXIT_SUCCESS;

  const model::Binary &Binary = *Model.get().get();
  std::vector<std::string> EntryStrings(FunctionEntries.begin(),
                                        FunctionEntries.end());
  llvm::ArrayRef<std::string> EntriesRef;
  if (AllFunctions)
    EntriesRef = {};
  else
    EntriesRef = EntryStrings;
  std::vector<MetaAddress> Selected =
    AbortOnError(revng::sdk::direct::parseFunctionEntries(Binary, EntriesRef));

  ensureDir(joinPath(OutDir, "cfg"));
  ensureDir(joinPath(OutDir, "isolated"));

  revng::pypeline::CFGMap CFGs;
  AbortOnError(revng::sdk::direct::collectCFG(Model, Root, CFGs, Selected));

  for (const MetaAddress &Entry : Selected) {
    const ObjectID ID(Entry);
    const auto &CFG = CFGs.getElement(ID);

    llvm::SmallVector<char, 0> Buffer;
    llvm::raw_svector_ostream OS(Buffer);
    CFG.serialize(OS);

    writeBytesToFile(joinPath(joinPath(OutDir, "cfg"),
                              safeFilename(Entry) + ".yml"),
                     llvm::StringRef(Buffer.data(), Buffer.size()));
  }

  revng::pypeline::LLVMFunctionContainer Isolated;
  AbortOnError(revng::sdk::direct::isolate(Model, CFGs, Root, Isolated, Selected));

  for (const MetaAddress &Entry : Selected) {
    const ObjectID ID(Entry);
    const llvm::Module &M = Isolated.getModule(ID);

    writeModuleToFile(M,
                      joinPath(joinPath(OutDir, "isolated"),
                               safeFilename(Entry) + "." + Emit));
  }

  return EXIT_SUCCESS;
}
