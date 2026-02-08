#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_executable(model-opt Main.cpp)

if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES IRReader BitWriter)
endif()
target_link_libraries(model-opt revngModelPasses ${LLVM_LIBRARIES})
