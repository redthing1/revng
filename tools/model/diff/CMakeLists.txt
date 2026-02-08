#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(model-diff Main.cpp)

if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES IRReader)
endif()
target_link_libraries(model-diff revngModel ${LLVM_LIBRARIES})
