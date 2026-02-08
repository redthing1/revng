//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/Support/RuntimeDeps.h"

#include <cstddef>

namespace revng::runtime {

void ensureRuntimeDependenciesLoaded() {
  // Intentionally empty: this function exists to pull this translation unit
  // into the final link so the anchors below can force DT_NEEDED entries.
}

} // namespace revng::runtime

#if defined(__linux__) && defined(REVNG_RUNTIMEDEPS_USE_JEMALLOC)
extern "C" int mallctl(const char *, void *, size_t *, void *, size_t);
[[gnu::used]] static void *const RevngJemallocAnchor = (void *)&mallctl;
#endif

