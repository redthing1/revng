#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(sdk-decompile Main.cpp)

# Link the binary importer CLI options so flags like --base/--debug-info are
# registered before InitRevng parses argv. The analysis itself is still loaded
# via plugins at runtime.
target_link_libraries(sdk-decompile revngSDK revngModelBinaryImporterCLI)

# `sdk-decompile` loads analysis plugins from disk (dlopen) and the default
# pipelines.yml/analyses list expects them to be present under lib/revng/analyses.
#
# Make sure `ninja sdk-decompile` builds them, otherwise runtime behavior can be
# confusing (stale plugins).
add_dependencies(sdk-decompile analyses)

revng_add_executable(sdk-import Import.cpp)
target_link_libraries(sdk-import revngSDKDirect)

revng_add_executable(sdk-lift Lift.cpp)
target_link_libraries(sdk-lift revngSDKDirect)

revng_add_executable(sdk-cfg CFG.cpp)
target_link_libraries(sdk-cfg revngSDKDirect)

revng_add_executable(sdk-isolate Isolate.cpp)
target_link_libraries(sdk-isolate revngSDKDirect)

if(REVNG_BUILD_TESTS)
  revng_add_test(
    NAME
    sdk-cli-smoke
    COMMAND
    "${CMAKE_SOURCE_DIR}/tests/sdk/sdk_cli_smoke.sh"
    "$<TARGET_FILE:sdk-import>"
    "$<TARGET_FILE:sdk-lift>"
    "$<TARGET_FILE:sdk-cfg>"
    "$<TARGET_FILE:sdk-isolate>"
    "$<TARGET_FILE:sdk-decompile>"
    "$<TARGET_FILE:sdk_cli_fixture>"
    "${CMAKE_CURRENT_BINARY_DIR}/sdk-cli-smoke-out")
  set_tests_properties(sdk-cli-smoke PROPERTIES LABELS "sdk")
endif()
