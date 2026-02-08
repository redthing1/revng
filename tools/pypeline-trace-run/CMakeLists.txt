#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(pypeline-trace-run Main.cpp)
if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES Support Core)
endif()
target_link_libraries(pypeline-trace-run revngPypelineTraceRunner revngSupport
                      ${LLVM_LIBRARIES})
