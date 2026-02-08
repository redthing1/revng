#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <string>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include "revng/EarlyFunctionAnalysis/CollectCFG.h"
#include "revng/FunctionIsolation/IsolateFunctions.h"
#include "revng/Lift/Lift.h"
#include "revng/Model/Binary.h"
#include "revng/Model/Importer/Binary/BinaryImporter.h"
#include "revng/Model/Importer/Binary/ImporterOptions.h"
#include "revng/PipeboxCommon/BinariesContainer.h"
#include "revng/PipeboxCommon/LLVMContainer.h"
#include "revng/PipeboxCommon/Model.h"
#include "revng/Support/MetaAddress.h"

namespace revng::sdk::direct {

/// Parse `--function-entry` values and validate that the referenced functions
/// exist in the model.
///
/// If `Entries` is empty, this returns all model functions.
llvm::Expected<std::vector<MetaAddress>>
parseFunctionEntries(const model::Binary &Binary,
                     llvm::ArrayRef<std::string> Entries);

llvm::Expected<::Model> importModel(llvm::StringRef BinaryPath,
                                    const ImporterOptions &Options);

llvm::Error loadBinary(llvm::StringRef BinaryPath,
                       revng::pypeline::BinariesContainer &Out);

llvm::Error lift(const ::Model &Model,
                 const revng::pypeline::BinariesContainer &Binaries,
                 revng::pypeline::LLVMRootContainer &OutRoot);

llvm::Error collectCFG(const ::Model &Model,
                       revng::pypeline::LLVMRootContainer &Root,
                       revng::pypeline::CFGMap &OutCFG,
                       llvm::ArrayRef<MetaAddress> FunctionEntries);

llvm::Error isolate(const ::Model &Model,
                    const revng::pypeline::CFGMap &CFG,
                    revng::pypeline::LLVMRootContainer &Root,
                    revng::pypeline::LLVMFunctionContainer &OutIsolated,
                    llvm::ArrayRef<MetaAddress> FunctionEntries);

} // namespace revng::sdk::direct
