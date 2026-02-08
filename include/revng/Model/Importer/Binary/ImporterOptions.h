#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <cstdint>
#include <string>

#include "llvm/ADT/ArrayRef.h"

// Note: this header is intentionally CLI-free. Do not include
// llvm/Support/CommandLine.h here: embedding users should be able to consume
// importer APIs without pulling in global command-line options.

enum class DebugInfoLevel {
  No,
  Yes,
  IgnoreLibraries
};

struct ImporterOptions {
  uint64_t BaseAddress = 0x400000;

  DebugInfoLevel DebugInfo = DebugInfoLevel::Yes;
  bool EnableRemoteDebugInfo = false;

  llvm::ArrayRef<std::string> AdditionalDebugInfoPaths = {};
};

