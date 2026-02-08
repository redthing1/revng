#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

install_pattern("*.h" "*.inc")

# MLIR Clift
if(REVNG_BUILD_CLIFT)
  add_subdirectory(revng)
endif()
