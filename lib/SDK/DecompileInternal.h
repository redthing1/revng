#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <set>
#include <string>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include "revng/Pipeline/Target.h"
#include "revng/Support/MetaAddress.h"

namespace model {
class Binary;
}

namespace revng {
class FilePath;
namespace pipes {
class PipelineManager;
}
} // namespace revng

namespace revng::sdk::detail {

llvm::Expected<std::set<MetaAddress>>
parseAndValidateFunctionEntries(const model::Binary &Model,
                                llvm::ArrayRef<std::string> Entries);

llvm::Expected<pipeline::TargetsList>
computeSelectedTargetsForKind(revng::pipes::PipelineManager &Manager,
                              const pipeline::Kind &Kind,
                              llvm::ArrayRef<std::string> FunctionEntries);

llvm::Error runAnalysesList(revng::pipes::PipelineManager &Manager,
                            llvm::StringRef Name,
                            llvm::ArrayRef<std::string> FunctionEntries,
                            bool RestrictToSelectedFunctions);

llvm::Error produceStepArtifactToFile(revng::pipes::PipelineManager &Manager,
                                      llvm::StringRef StepName,
                                      const revng::FilePath &Output,
                                      llvm::ArrayRef<std::string> FunctionEntries);

llvm::Error
producePartialRecompilableArchive(revng::pipes::PipelineManager &Manager,
                                  const revng::FilePath &Output,
                                  llvm::ArrayRef<std::string> Entries);

} // namespace revng::sdk::detail

