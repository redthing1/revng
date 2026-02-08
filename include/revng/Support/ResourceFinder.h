#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/Support/PathList.h"

namespace revng {

extern PathList ResourceFinder;

// Extend or override resource search roots. This is intended for embedding
// revng as a library, where resources (share/revng, libtcg, etc.) might live
// outside the default prefix discovered via librevngSupport location.
void addResourceRoot(llvm::StringRef Path);
void setResourceRoots(const std::vector<std::string> &Roots);

std::string getComponentsHash();

} // namespace revng
