#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/Support/CommandLine.h"

#include "revng/Model/Importer/Binary/ImporterOptions.h"

// Command-line options for binary importing. These are intentionally separated
// from ImporterOptions.h so that embedding/library users do not pull in global
// llvm::cl state unless they explicitly opt into it.

extern llvm::cl::opt<uint64_t> BaseAddress;
extern llvm::cl::list<std::string> ImportDebugInfo;
extern llvm::cl::opt<DebugInfoLevel> DebugInfo;
extern llvm::cl::opt<bool> EnableRemoteDebugInfo;

[[nodiscard]] ImporterOptions importerOptionsFromCommandLine();

