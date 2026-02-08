#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_library_internal(
  revngSDK
  SHARED
  Decompile.cpp
  DecompileAnalyses.cpp
  DecompileArtifacts.cpp
  DecompilePipelineManager.cpp
  DecompileSelection.cpp)

target_link_libraries(
  revngSDK
  PUBLIC revngPipeline revngPipes revngRuntimeDeps
  PRIVATE revngBackend)

revng_add_library_internal(revngSDKDirect SHARED Direct.cpp)

target_link_libraries(
  revngSDKDirect
  PUBLIC
    revngModelBinaryImporter
    revngLift
    revngEarlyFunctionAnalysis
    revngFunctionIsolation
    revngRuntimeDeps
    revngSupport)
