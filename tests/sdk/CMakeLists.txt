#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# A small native binary used by SDK CLI smoke tests.
add_executable(sdk_cli_fixture fixture.c)
set_target_properties(sdk_cli_fixture PROPERTIES OUTPUT_NAME "sdk-cli-fixture")

