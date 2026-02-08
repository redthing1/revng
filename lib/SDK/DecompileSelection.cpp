//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "DecompileInternal.h"

#include <set>

#include "llvm/ADT/StringRef.h"

#include "revng/Model/Binary.h"
#include "revng/Pipes/ModelGlobal.h"
#include "revng/Pipes/PipelineManager.h"
#include "revng/Support/Error.h"

namespace revng::sdk::detail {

static llvm::Expected<MetaAddress>
parseFunctionEntry(llvm::StringRef Text, model::Architecture::Values Arch) {
  Text = Text.trim();
  if (Text.empty())
    return revng::createError("empty function entry");

  if (Text.contains(MetaAddress::Separator)) {
    MetaAddress Entry = MetaAddress::fromString(Text);
    if (not Entry.isValid())
      return revng::createError("invalid MetaAddress: '%s'",
                                Text.str().c_str());
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

llvm::Expected<std::set<MetaAddress>>
parseAndValidateFunctionEntries(const model::Binary &Model,
                                llvm::ArrayRef<std::string> Entries) {
  std::set<MetaAddress> Result;
  auto Arch = Model.Architecture();

  for (const std::string &Text : Entries) {
    auto MaybeEntry = parseFunctionEntry(Text, Arch);
    if (not MaybeEntry)
      return MaybeEntry.takeError();

    MetaAddress Entry = *MaybeEntry;
    if (Model.Functions().find(Entry) == Model.Functions().end()) {
      return revng::createError("no function with entry '%s' in the model",
                                Entry.toString().c_str());
    }

    Result.insert(Entry);
  }

  return Result;
}

llvm::Expected<pipeline::TargetsList>
computeSelectedTargetsForKind(revng::pipes::PipelineManager &Manager,
                              const pipeline::Kind &Kind,
                              llvm::ArrayRef<std::string> FunctionEntries) {
  if (FunctionEntries.empty())
    return Kind.allTargets(Manager.context());

  if (Kind.depth() != 1) {
    return revng::createError("kind '%s' does not support function selection "
                              "(expected depth 1, got %zu)",
                              Kind.name().str().c_str(),
                              Kind.depth());
  }

  const model::Binary &Model = *revng::getModelFromContext(Manager.context());
  auto MaybeEntries = parseAndValidateFunctionEntries(Model, FunctionEntries);
  if (not MaybeEntries)
    return MaybeEntries.takeError();

  pipeline::TargetsList Targets;
  for (const MetaAddress &Entry : MaybeEntries.get())
    Targets.push_back(pipeline::Target(Entry.toString(), Kind));

  return Targets;
}

} // namespace revng::sdk::detail

