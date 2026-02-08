//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/SDK/Direct/Direct.h"

#include <set>
#include <utility>

#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"

#include "revng/Support/Error.h"
#include "revng/Support/RuntimeDeps.h"
#include "revng/TupleTree/TupleLikeTraits.h"

namespace revng::sdk::direct {

static model::BinaryReference makeReference(model::Binary &Binary,
                                            size_t Index) {
  using Fields = TupleLikeTraits<model::Binary>::Fields;
  TupleTreePath BinaryPath;
  BinaryPath.push_back(static_cast<size_t>(Fields::Binaries));
  BinaryPath.push_back(Index);
  return model::BinaryReference{ &Binary, BinaryPath };
}

static std::string sha256Hex(llvm::ArrayRef<char> Data) {
  const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(Data.data());
  std::array<uint8_t, 32> Hash = llvm::SHA256::hash({ Ptr, Data.size() });
  return llvm::toHex(Hash, /*LowerCase=*/true);
}

static llvm::Expected<MetaAddress>
parseFunctionEntry(llvm::StringRef Text, model::Architecture::Values Arch) {
  Text = Text.trim();
  if (Text.empty())
    return revng::createError("empty function entry");

  if (Text.contains(MetaAddress::Separator)) {
    MetaAddress Entry = MetaAddress::fromString(Text);
    if (not Entry.isValid())
      return revng::createError("invalid MetaAddress: '%s'", Text.str().c_str());
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

llvm::Expected<std::vector<MetaAddress>>
parseFunctionEntries(const model::Binary &Binary,
                     llvm::ArrayRef<std::string> Entries) {
  if (Entries.empty()) {
    std::vector<MetaAddress> Result;
    Result.reserve(Binary.Functions().size());
    for (const model::Function &F : Binary.Functions())
      Result.push_back(F.Entry());
    return Result;
  }

  std::set<MetaAddress> Unique;
  const auto Arch = Binary.Architecture();

  for (const std::string &Text : Entries) {
    auto MaybeEntry = parseFunctionEntry(Text, Arch);
    if (not MaybeEntry)
      return MaybeEntry.takeError();

    MetaAddress Entry = *MaybeEntry;
    if (Binary.Functions().find(Entry) == Binary.Functions().end()) {
      return revng::createError("no function with entry '%s' in the model",
                                Entry.toString().c_str());
    }

    Unique.insert(Entry);
  }

  return std::vector<MetaAddress>(Unique.begin(), Unique.end());
}

llvm::Expected<::Model> importModel(llvm::StringRef BinaryPath,
                                    const ImporterOptions &Options) {
  auto MaybeBuffer = llvm::MemoryBuffer::getFile(BinaryPath,
                                                 /*IsText=*/false,
                                                 /*RequiresNullTerminator=*/false);
  if (not MaybeBuffer)
    return llvm::createStringError(MaybeBuffer.getError(),
                                   "failed to read binary '%s'",
                                   BinaryPath.str().c_str());

  const llvm::MemoryBuffer &Buffer = **MaybeBuffer;
  llvm::ArrayRef<char> Data(Buffer.getBufferStart(), Buffer.getBufferSize());
  std::string Hash = sha256Hex(Data);

  TupleTree<model::Binary> Imported;
  Imported->Binaries().insert(model::BinaryIdentifier(0,
                                                      Hash,
                                                      Data.size(),
                                                      llvm::sys::path::filename(BinaryPath)
                                                        .str()));

  model::BinaryReference BinaryReference;
  BinaryReference = makeReference(*Imported.get(), 0);
  if (llvm::Error Err = ::importBinary(Imported,
                                       **MaybeBuffer,
                                       BinaryPath,
                                       Options,
                                       BinaryReference)) {
    return std::move(Err);
  }

  ::Model Result;
  Result.get() = std::move(Imported);
  return Result;
}

llvm::Error loadBinary(llvm::StringRef BinaryPath,
                       revng::pypeline::BinariesContainer &Out) {
  return Out.addFileFromPath(BinaryPath);
}

llvm::Error lift(const ::Model &Model,
                 const revng::pypeline::BinariesContainer &Binaries,
                 revng::pypeline::LLVMRootContainer &OutRoot) {
  revng::runtime::ensureRuntimeDependenciesLoaded();

  if (Binaries.size() != 1) {
    return revng::createError("Binaries must have exactly one element (got %zu)",
                              Binaries.size());
  }

  if (llvm::Error Err = revng::pypeline::piperuns::Lift::checkPrecondition(Model))
    return Err;

  revng::pypeline::piperuns::Lift Runner(Model, "", "", Binaries, OutRoot);
  Runner.run();

  return llvm::Error::success();
}

llvm::Error collectCFG(const ::Model &Model,
                       revng::pypeline::LLVMRootContainer &Root,
                       revng::pypeline::CFGMap &OutCFG,
                       llvm::ArrayRef<MetaAddress> FunctionEntries) {
  const model::Binary &Binary = *Model.get().get();

  revng::pypeline::piperuns::CollectCFG Runner(Model, "", "", Root, OutCFG);

  if (FunctionEntries.empty()) {
    for (const model::Function &F : Binary.Functions())
      Runner.runOnFunction(F);
  } else {
    for (const MetaAddress &Entry : FunctionEntries)
      Runner.runOnFunction(Binary.Functions().at(Entry));
  }

  return llvm::Error::success();
}

llvm::Error isolate(const ::Model &Model,
                    const revng::pypeline::CFGMap &CFG,
                    revng::pypeline::LLVMRootContainer &Root,
                    revng::pypeline::LLVMFunctionContainer &OutIsolated,
                    llvm::ArrayRef<MetaAddress> FunctionEntries) {
  const model::Binary &Binary = *Model.get().get();

  // The Isolate piperun does most of its work in the destructor (epilogue and
  // splitting functions into per-function modules).
  revng::pypeline::piperuns::Isolate Runner(Model, "", "", CFG, Root, OutIsolated);

  if (FunctionEntries.empty()) {
    for (const model::Function &F : Binary.Functions())
      Runner.runOnFunction(F);
  } else {
    for (const MetaAddress &Entry : FunctionEntries)
      Runner.runOnFunction(Binary.Functions().at(Entry));
  }

  return llvm::Error::success();
}

} // namespace revng::sdk::direct
