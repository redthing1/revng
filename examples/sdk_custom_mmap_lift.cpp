//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <array>
#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"

#include "revng/Model/ABI.h"
#include "revng/Model/BinaryIdentifier.h"
#include "revng/Model/Segment.h"
#include "revng/PipeboxCommon/BinariesContainer.h"
#include "revng/PipeboxCommon/LLVMContainer.h"
#include "revng/PipeboxCommon/Model.h"
#include "revng/SDK/Direct/Direct.h"
#include "revng/Support/Error.h"
#include "revng/Support/IRHelpers.h"
#include "revng/Support/InitRevng.h"
#include "revng/Support/MetaAddress.h"
#include "revng/Support/ResourceFinder.h"
#include "revng/TupleTree/TupleLikeTraits.h"

namespace cl = llvm::cl;

static cl::OptionCategory Category("SDK Custom Memory Map Lift Example Options",
                                  "");

static cl::opt<std::string>
  ArchName("arch",
           cl::desc("Architecture: x86, x86_64, arm, aarch64, mips, mipsel, "
                    "systemz"),
           cl::value_desc("arch"),
           cl::Required,
           cl::cat(Category));

static cl::opt<std::string>
  ABIName("abi",
          cl::desc("ABI name (optional). If omitted, a sensible default is "
                   "picked (ELF-style)."),
          cl::value_desc("abi"),
          cl::init(""),
          cl::cat(Category));

static cl::opt<uint64_t> BaseAddress("base",
                                     cl::desc("Virtual base address where the "
                                              "blob is mapped."),
                                     cl::value_desc("address"),
                                     cl::init(0x400000),
                                     cl::cat(Category));

static cl::opt<uint64_t>
  EntryAddress("entry",
               cl::desc("Entry PC address (defaults to --base)."),
               cl::value_desc("address"),
               cl::init(0),
               cl::cat(Category));

static cl::list<std::string>
  ResourceRoots("resource-root",
                cl::desc("Extra resource roots (prefixes) to search for "
                         "share/revng and other runtime resources. "
                         "May be repeated."),
                cl::ZeroOrMore,
                cl::cat(Category));

static cl::opt<std::string>
  Emit("emit",
       cl::desc("Output format: bc (default) or ll"),
       cl::value_desc("format"),
       cl::init("bc"),
       cl::cat(Category));

static cl::opt<std::string> InputBlob(cl::Positional,
                                      cl::desc("<blob>"),
                                      cl::Required,
                                      cl::cat(Category));

static cl::opt<std::string> OutputPath("o",
                                       cl::desc("Output file for the lifted "
                                                "LLVM module"),
                                       cl::Required,
                                       cl::cat(Category));

static llvm::ExitOnError AbortOnError;

static void applyResourceRoots() {
  // addResourceRoot prepends, so add in reverse to preserve user order.
  std::vector<std::string> Roots(ResourceRoots.begin(), ResourceRoots.end());
  for (auto It = Roots.rbegin(); It != Roots.rend(); ++It)
    revng::addResourceRoot(*It);
}

static std::string sha256Hex(llvm::ArrayRef<char> Data) {
  const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(Data.data());
  std::array<uint8_t, 32> Hash = llvm::SHA256::hash({ Ptr, Data.size() });
  return llvm::toHex(Hash, /*LowerCase=*/true);
}

static model::BinaryReference makeReference(model::Binary &Binary,
                                            size_t Index) {
  using Fields = TupleLikeTraits<model::Binary>::Fields;
  TupleTreePath BinaryPath;
  BinaryPath.push_back(static_cast<size_t>(Fields::Binaries));
  BinaryPath.push_back(Index);
  return model::BinaryReference{ &Binary, BinaryPath };
}

static model::Architecture::Values parseArch(llvm::StringRef Name) {
  model::Architecture::Values Arch = model::Architecture::fromName(Name);
  if (Arch == model::Architecture::Invalid) {
    AbortOnError(revng::createError("invalid --arch '%s'", Name.str().c_str()));
  }
  return Arch;
}

static model::ABI::Values pickABI(model::Architecture::Values Arch) {
  if (ABIName.empty()) {
    auto MaybeDefault = model::ABI::getDefaultForELF(Arch);
    if (not MaybeDefault.has_value())
      AbortOnError(revng::createError("no default ABI for architecture '%s'",
                                     model::Architecture::getName(Arch).str().c_str()));
    return *MaybeDefault;
  }

  model::ABI::Values Abi = model::ABI::fromName(ABIName);
  if (Abi == model::ABI::Invalid) {
    AbortOnError(revng::createError("invalid --abi '%s'", ABIName.c_str()));
  }

  if (model::ABI::getArchitecture(Abi) != Arch) {
    AbortOnError(revng::createError("--abi '%s' does not match --arch '%s'",
                                   ABIName.c_str(),
                                   model::Architecture::getName(Arch).str().c_str()));
  }

  return Abi;
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

  const auto Arch = parseArch(ArchName);
  const auto Abi = pickABI(Arch);

  auto MaybeBuffer = llvm::MemoryBuffer::getFile(InputBlob,
                                                 /*IsText=*/false,
                                                 /*RequiresNullTerminator=*/false);
  if (not MaybeBuffer)
    AbortOnError(llvm::createStringError(MaybeBuffer.getError(),
                                        "failed to read blob '%s'",
                                        InputBlob.c_str()));

  const llvm::MemoryBuffer &Buffer = **MaybeBuffer;
  llvm::ArrayRef<char> Data(Buffer.getBufferStart(), Buffer.getBufferSize());

  const uint64_t EntryPC = (EntryAddress.getNumOccurrences() != 0)
                             ? EntryAddress
                             : BaseAddress;

  TupleTree<model::Binary> TT;
  TT->Architecture() = Arch;
  TT->DefaultABI() = Abi;
  TT->EntryPoint() = MetaAddress::fromPC(Arch, EntryPC);

  std::string Hash = sha256Hex(Data);
  TT->Binaries().insert(model::BinaryIdentifier(0,
                                                Hash,
                                                Data.size(),
                                                llvm::sys::path::filename(InputBlob)
                                                  .str()));
  model::BinaryReference BinRef = makeReference(*TT.get(), 0);

  model::Segment S;
  S.Binary() = BinRef;
  S.StartAddress() = MetaAddress::fromGeneric(Arch, BaseAddress);
  S.VirtualSize() = Data.size();
  S.StartOffset() = 0;
  S.FileSize() = Data.size();
  S.IsReadable() = true;
  S.IsWriteable() = false;
  S.IsExecutable() = true;
  S.Name() = "blob";
  TT->Segments().insert(std::move(S));

  ::Model Model;
  Model.get() = std::move(TT);

  revng::pypeline::BinariesContainer Binaries;
  AbortOnError(Binaries.addFileFromBuffer(Data));

  revng::pypeline::LLVMRootContainer Root;
  AbortOnError(revng::sdk::direct::lift(Model, Binaries, Root));

  writeModuleToFile(Root.getModule(), OutputPath);
  return EXIT_SUCCESS;
}

