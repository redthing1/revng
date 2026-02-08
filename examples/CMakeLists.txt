#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# SDK examples are intentionally small, copy-paste friendly entry points.
# They are not installed by default (they are documentation as much as tooling).

set(REVNG_EXAMPLES_OUTPUT_DIR "libexec/revng/examples")

revng_add_executable_internal(sdk-direct-e2e "${REVNG_EXAMPLES_OUTPUT_DIR}"
                              sdk_direct_e2e.cpp)
target_link_libraries(sdk-direct-e2e PRIVATE revngSDKDirect)

revng_add_executable_internal(sdk-custom-mmap-lift
                              "${REVNG_EXAMPLES_OUTPUT_DIR}"
                              sdk_custom_mmap_lift.cpp)
target_link_libraries(sdk-custom-mmap-lift PRIVATE revngSDKDirect)

