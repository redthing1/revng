#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_library_internal(revngSDK SHARED Decompile.cpp)

target_link_libraries(
  revngSDK
  PUBLIC revngPipeline revngPipes revngRuntimeDeps
  PRIVATE revngBackend)
