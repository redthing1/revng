#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES Support Core)
endif()
revng_add_library_internal(revngPypelineTraceRunner SHARED TraceFile.cpp
                           Runner.cpp SavePoint.cpp)
target_link_libraries(revngPypelineTraceRunner ${LLVM_LIBRARIES} revngModel
                      revngSupport sqlite3)
