#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_library_internal(
  revngRuntimeDeps
  STATIC
  RuntimeDeps.cpp)

target_include_directories(revngRuntimeDeps INTERFACE $<INSTALL_INTERFACE:include/>)

if(REVNG_HAVE_JEMALLOC)
  target_compile_definitions(revngRuntimeDeps PRIVATE REVNG_RUNTIMEDEPS_USE_JEMALLOC=1)
  target_link_libraries(revngRuntimeDeps PUBLIC "${REVNG_JEMALLOC_LIBRARY}")
endif()

