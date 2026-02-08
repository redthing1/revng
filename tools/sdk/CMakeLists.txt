#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(sdk-decompile Main.cpp)

# Link the binary importer library so its command-line options (e.g. --base,
# --debug-info) are registered before InitRevng parses argv. The analysis itself
# is still loaded via plugins at runtime.
target_link_libraries(sdk-decompile revngSDK revngModelImporterBinary)

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
