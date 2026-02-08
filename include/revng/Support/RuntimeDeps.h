#pragma once
//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

namespace revng::runtime {

/// Ensure runtime dependencies required by dlopen'ed components (e.g. libtcg)
/// are part of the initial load set of the current process.
///
/// This function is intentionally a no-op at runtime: its purpose is to force
/// the linker to pull in revngRuntimeDeps, whose translation unit anchors the
/// needed DT_NEEDED entries (e.g. jemalloc) for glibc static TLS safety.
void ensureRuntimeDependenciesLoaded();

} // namespace revng::runtime

