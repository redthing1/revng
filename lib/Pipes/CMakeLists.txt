#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_library_internal(
  revngPipes
  SHARED
  Containers.cpp
  IRHelpers.cpp
  PipelineManager.cpp
  Pipes.cpp
  RootKind.cpp
  FunctionPass.cpp
  TaggedFunctionKind.cpp
  GlobalsAnalyses.cpp
  DebugInfoHelpers.cpp)

if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES Core MC Support)
endif()
target_link_libraries(revngPipes revngPipeline revngModel revngSupport
                      revngStorage ${LLVM_LIBRARIES})
