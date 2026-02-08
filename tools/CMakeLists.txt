#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

if(REVNG_BUILD_CLIFT)
  add_subdirectory(clift-opt)
endif()
add_subdirectory(lddtree)
add_subdirectory(model)
add_subdirectory(pipeline)
add_subdirectory(pypeline-trace-run)
add_subdirectory(sdk)
add_subdirectory(trace)
