#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

cmake_minimum_required(VERSION 3.15.0)

# NOTE: revng upstream did not historically set a project version, but having a
# version enables generating a proper CMake package config (revngConfig.cmake).
project(revng VERSION 0.0.0)

include(share/revng/cmake/Common.cmake)

#
# Build configuration knobs
#
# NOTE: defaults preserve the upstream "build the full distribution" behavior.
# Set REVNG_SDK_MODE=ON to get a leaner, embeddable build by default.
option(REVNG_SDK_MODE "Build a minimal embeddable SDK (disable docs/tests/tools and heavy packaging steps)" OFF)

if(REVNG_SDK_MODE)
  set(REVNG_DEFAULT_FULL_FEATURES OFF)
  set(REVNG_DEFAULT_INSTALL_LIBEXEC OFF)
  set(REVNG_DEFAULT_PYTHON_BINDINGS OFF)
  set(REVNG_DEFAULT_IMPORT_FROM_C OFF)
  set(REVNG_DEFAULT_PIPELINEC OFF)
  set(REVNG_DEFAULT_PREBUILT_RUNTIME_RESOURCES ON)
  set(REVNG_DEFAULT_WERROR OFF)
else()
  set(REVNG_DEFAULT_FULL_FEATURES ON)
  set(REVNG_DEFAULT_INSTALL_LIBEXEC ON)
  set(REVNG_DEFAULT_PYTHON_BINDINGS ON)
  set(REVNG_DEFAULT_IMPORT_FROM_C ON)
  set(REVNG_DEFAULT_PIPELINEC ON)
  set(REVNG_DEFAULT_PREBUILT_RUNTIME_RESOURCES OFF)
  set(REVNG_DEFAULT_WERROR ON)
endif()

option(REVNG_BUILD_DOCS "Build documentation targets" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_MKDOCS_HTML "Build mkdocs HTML site (requires mkdocs)" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_TYPESCRIPT "Build TypeScript packages (requires npm)" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_TOOLS "Build CLI tools" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_TESTS "Build and register tests" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_WELL_KNOWN_BINARIES "Build helper targets importing well-known models" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_CLANG_FORMAT_DOTFILE "Generate .clang-format config from check-conventions" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_IMPORT_FROM_C "Build ImportFromC analysis (requires clang-devel)" ${REVNG_DEFAULT_IMPORT_FROM_C})
option(REVNG_BUILD_PIPELINEC "Build PipelineC C API library (requires pycparser)" ${REVNG_DEFAULT_PIPELINEC})
option(REVNG_BUILD_CLIFT "Build experimental Clift/MLIR components (requires MLIR)" ${REVNG_DEFAULT_FULL_FEATURES})

option(REVNG_BUILD_PYTHON_WHEELS "Build and install Python wheels" ${REVNG_DEFAULT_FULL_FEATURES})
option(REVNG_BUILD_PYTHON_BINDINGS "Build native Python bindings (nanobind-based pipebox)" ${REVNG_DEFAULT_PYTHON_BINDINGS})
option(REVNG_BUILD_PYTHON "Build Python modules/tools (CLI wrapper, wheels)" ${REVNG_DEFAULT_FULL_FEATURES})

option(REVNG_INSTALL_LIBEXEC "Install libexec/ helper scripts" ${REVNG_DEFAULT_INSTALL_LIBEXEC})

option(REVNG_WERROR "Treat compiler warnings as errors" ${REVNG_DEFAULT_WERROR})

# Use prebuilt runtime resources from an extracted official distribution
# (e.g. docker /revng/root) to avoid generating helper bitcode/support modules.
option(REVNG_USE_PREBUILT_RUNTIME_RESOURCES
       "Use prebuilt runtime resources from REVNG_RUNTIME_PREFIX (avoid building helper bitcode/support modules)"
       ${REVNG_DEFAULT_PREBUILT_RUNTIME_RESOURCES})
option(REVNG_COPY_EXTERNAL_RUNTIME_TO_BUILD_TREE
       "Copy external runtime libraries (libtcg-*.so) from REVNG_RUNTIME_PREFIX into the build tree for running from build dir"
       ${REVNG_DEFAULT_PREBUILT_RUNTIME_RESOURCES})

# Run Python code generators via uv (no global pip installs).
option(REVNG_USE_UV
       "Use uv run --with to execute Python code generators without installing deps into the user/system environment"
       ON)

# External runtime/deps integration points (superbuild-friendly).
set(REVNG_LIBTCG_PREFIX "" CACHE PATH "Prefix containing share/libtcg (libtcg-helpers-*.bc)")
set(REVNG_LIBTCG_HELPERS_DIR "" CACHE PATH "Directory containing libtcg helper bitcode (libtcg-helpers-*.bc)")
set(REVNG_RUNTIME_PREFIX "" CACHE PATH "Prefix to bundle external runtime deps from (e.g. extracted revng docker /revng/root)")
option(REVNG_BUNDLE_EXTERNAL_RUNTIME "Bundle external runtime deps (libtcg + helper bitcode) into this install prefix" OFF)

# jemalloc is a dependency of the official prebuilt libtcg runtimes. Ensure it
# is in the initial load set (DT_NEEDED) to avoid glibc static TLS allocation
# failures when dlopening libtcg later.
option(REVNG_LINK_JEMALLOC_EARLY
       "Link libjemalloc into revngLift to avoid dlopen static TLS failures"
       ON)
set(REVNG_JEMALLOC_LIBRARY "" CACHE FILEPATH "Path to libjemalloc (e.g. /usr/lib64/libjemalloc.so.2)")
set(REVNG_HAVE_JEMALLOC OFF)
if(REVNG_LINK_JEMALLOC_EARLY AND UNIX AND NOT APPLE)
  set(_REVNG_JEMALLOC_CANDIDATE "${REVNG_JEMALLOC_LIBRARY}")
  if(_REVNG_JEMALLOC_CANDIDATE STREQUAL "")
    # Prefer the devel symlink if available, but fall back to the runtime SONAME.
    find_library(_REVNG_JEMALLOC_SYMLINK NAMES jemalloc)
    if(_REVNG_JEMALLOC_SYMLINK)
      set(_REVNG_JEMALLOC_CANDIDATE "${_REVNG_JEMALLOC_SYMLINK}")
    else()
      find_file(_REVNG_JEMALLOC_SONAME
                NAMES libjemalloc.so.2
                PATHS /usr/lib64 /lib64 /usr/lib /lib)
      set(_REVNG_JEMALLOC_CANDIDATE "${_REVNG_JEMALLOC_SONAME}")
    endif()
  endif()

  if(_REVNG_JEMALLOC_CANDIDATE)
    set(REVNG_JEMALLOC_LIBRARY
        "${_REVNG_JEMALLOC_CANDIDATE}"
        CACHE FILEPATH
              "Path to libjemalloc (e.g. /usr/lib64/libjemalloc.so.2)"
        FORCE)
    message(STATUS "Found jemalloc: ${REVNG_JEMALLOC_LIBRARY}")
    set(REVNG_HAVE_JEMALLOC ON)
  else()
    message(WARNING
            "jemalloc not found. dlopening libtcg may fail with 'cannot allocate memory in static TLS block'. "
            "Install jemalloc or set -DREVNG_JEMALLOC_LIBRARY=/path/to/libjemalloc.so.2.")
  endif()
endif()

# clang used to compile support/early-linked LLVM IR modules.
set(TARGET_CLANG "" CACHE FILEPATH "Path to clang executable used to build revng support modules")

#
# Compile flags
#

# These have to be first to get highest priority
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_BINARY_DIR}/include")

add_definitions("-DINSTALL_PATH=\"${CMAKE_INSTALL_PREFIX}\"")

# Uncomment the following line if recursive coroutines make debugging hard
# add_definitions("-DDISABLE_RECURSIVE_COROUTINES")

# Remove -rdynamic
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

# Basic compiler options
# cmake-format: off
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -shared-libasan")
# cmake-format: on
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
if(REVNG_WERROR)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# set(TUPLE_TREE_GENERATOR_EMIT_TRACKING_DEBUG ON)

if(${TUPLE_TREE_GENERATOR_EMIT_TRACKING_DEBUG})
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -DTUPLE_TREE_GENERATOR_EMIT_TRACKING_DEBUG")
endif()

# Uncomment the following line if errors like `Couldn't find method
# SomeType::method` make debugging hard
#
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-limit-debug-info")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_flag_if_available("-Wno-unused-local-typedefs")
endif()

# Disable some warnings
add_flag_if_available("-Wno-unused-parameter")
add_flag_if_available("-Wno-unused-variable")
add_flag_if_available("-Wno-maybe-uninitialized")
add_flag_if_available("-Wno-init-list-lifetime")
add_flag_if_available("-Wno-ambiguous-reversed-operator")

# Add some extra warnings
add_flag_if_available("-Wstrict-aliasing")
add_flag_if_available("-fstrict-aliasing")

add_flag_if_available("-Wnon-virtual-dtor")
add_flag_if_available("-Wunreachable-code-break")
add_flag_if_available("-Winconsistent-missing-destructor-override")
add_flag_if_available("-Wnewline-eof")
add_flag_if_available("-Wmissing-prototypes")
add_flag_if_available("-Wimplicit-fallthrough")

add_definitions("-D_FILE_OFFSET_BITS=64")

check_cxx_compiler_flag("-no-pie" COMPILER_SUPPORTS_NO_PIE)

if(COMPILER_SUPPORTS_NO_PIE)
  set(NO_PIE "-no-pie")
endif()

include(CheckIncludeFiles)
check_include_files(valgrind/callgrind.h HAVE_VALGRIND_CALLGRIND_H)

if(HAVE_VALGRIND_CALLGRIND_H)
  add_definitions("-DHAVE_VALGRIND_CALLGRIND_H")
endif()

#
# Link LLVM
#
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIRS})
# Prefer linking to the monolithic LLVM shared library when available.
# This avoids embedding multiple copies of LLVM into different revng shared
# libraries (which can cause duplicate cl::opt registrations at runtime).
if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(
    LLVM_LIBRARIES
    core
    support
    irreader
    ScalarOpts
    linker
    Analysis
    object
    transformutils
    BitWriter
    BitReader
    InstCombine
    CodeGen
    Passes
    TargetParser)
endif()

# MLIR CMake stuff
#
# Only required for Clift/MLIR components (and Python bindings, which currently
# depend on Clift as well).
if(REVNG_BUILD_CLIFT OR REVNG_BUILD_PYTHON_BINDINGS)
  find_package(MLIR REQUIRED CONFIG)
endif()

#
# Link Clang
#
if(REVNG_BUILD_IMPORT_FROM_C)
  find_package(Clang REQUIRED CONFIG)
endif()

if(REVNG_BUILD_PYTHON_BINDINGS AND NOT REVNG_BUILD_CLIFT)
  message(FATAL_ERROR
          "REVNG_BUILD_PYTHON_BINDINGS requires REVNG_BUILD_CLIFT=ON "
          "(Pipebox links revngClift/revngCliftPipes).")
endif()

#
# Component hash
#
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/revng/component-hashes")
set(COMPONENT_HASH_PATH
    "${CMAKE_BINARY_DIR}/share/revng/component-hashes/revng")
add_custom_command(
  OUTPUT "${COMPONENT_HASH_PATH}"
  COMMAND
    sh -c
    ' (git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD || echo "\"${CMAKE_PROJECT_VERSION}\"") '
    > "${COMPONENT_HASH_PATH}")
add_custom_target(generate-component-hash ALL DEPENDS "${COMPONENT_HASH_PATH}")
install(FILES "${COMPONENT_HASH_PATH}"
        DESTINATION "share/revng/component-hashes/")

#
# share/revng
#
add_custom_target(copy_share ALL COMMAND cp -Tar "${CMAKE_SOURCE_DIR}/share/"
                                         "${CMAKE_BINARY_DIR}/share/")

install(
  DIRECTORY "${CMAKE_BINARY_DIR}/share/"
  DESTINATION share/
  USE_SOURCE_PERMISSIONS)

# Export CMake targets
install(
  EXPORT revng
  FILE revng.cmake
  NAMESPACE revng::
  DESTINATION share/revng/cmake)

# Provide a proper CMake package config so consumers can use:
#   find_package(revng CONFIG)
include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/share/revng/cmake/revngConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/share/revng/cmake/revngConfig.cmake"
  INSTALL_DESTINATION share/revng/cmake
  NO_CHECK_REQUIRED_COMPONENTS_MACRO)
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/share/revng/cmake/revngConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY AnyNewerVersion)
install(FILES "${CMAKE_BINARY_DIR}/share/revng/cmake/revngConfig.cmake"
              "${CMAKE_BINARY_DIR}/share/revng/cmake/revngConfigVersion.cmake"
        DESTINATION share/revng/cmake)

# Optionally bundle runtime dependencies shipped in the official revng
# distribution (or an extracted docker /revng/root prefix). This makes the
# installed prefix self-contained enough to run lifting without relying on
# system-installed libtcg.
if(REVNG_BUNDLE_EXTERNAL_RUNTIME)
  if("${REVNG_RUNTIME_PREFIX}" STREQUAL "")
    message(FATAL_ERROR
            "REVNG_BUNDLE_EXTERNAL_RUNTIME is enabled, but REVNG_RUNTIME_PREFIX is empty."
    )
  endif()

  # libtcg shared objects are expected by revng at <root>/lib/libtcg-*.so
  if(EXISTS "${REVNG_RUNTIME_PREFIX}/lib64")
    install(DIRECTORY "${REVNG_RUNTIME_PREFIX}/lib64/"
            DESTINATION lib
            FILES_MATCHING
            PATTERN "libtcg-*.so*")
  endif()
  if(EXISTS "${REVNG_RUNTIME_PREFIX}/lib")
    install(DIRECTORY "${REVNG_RUNTIME_PREFIX}/lib/"
            DESTINATION lib
            FILES_MATCHING
            PATTERN "libtcg-*.so*")
  endif()

  # Some distributions may provide raw libtcg helper bitcode here. This is not
  # required at runtime (revng uses share/revng/libtcg-helpers-*.bc), but it's
  # useful to ship it when available.
  if(EXISTS "${REVNG_RUNTIME_PREFIX}/share/libtcg")
    install(DIRECTORY "${REVNG_RUNTIME_PREFIX}/share/libtcg/"
            DESTINATION share/libtcg)
  endif()
endif()

# Create share/revng/additional-bin-paths
set(ADDITIONAL_BIN_PATHS "${CMAKE_BINARY_DIR}/share/revng/additional-bin-paths")
file(WRITE "${ADDITIONAL_BIN_PATHS}" "libexec/revng\n")

file(RELATIVE_PATH RELATIVE_LLVM_TOOLS_BINARY_DIR "${CMAKE_INSTALL_PREFIX}"
     "${LLVM_TOOLS_BINARY_DIR}")
file(APPEND "${ADDITIONAL_BIN_PATHS}" "${RELATIVE_LLVM_TOOLS_BINARY_DIR}\n")

# Create additional-search-prefixes
file(WRITE "${CMAKE_BINARY_DIR}/additional-search-prefixes"
     "${CMAKE_INSTALL_PREFIX}\n")

file(RELATIVE_PATH RELATIVE_LLVM_TOOLS_BINARY_DIR "${CMAKE_INSTALL_PREFIX}"
     "${LLVM_TOOLS_BINARY_DIR}")
file(APPEND "${ADDITIONAL_BIN_PATHS}" "${RELATIVE_LLVM_TOOLS_BINARY_DIR}\n")

# Build the support module for each architecture and in several configurations
set(SUPPORT_MODULES_CONFIGS "normal;trace")
set(SUPPORT_MODULES_CONFIG_normal "")
set(SUPPORT_MODULES_CONFIG_trace "-DTRACE")

# A set of llvm modules that contain all the known helpers.
set(HELPER_MODULE_LIST, "")

foreach(
  ARCH
  aarch64
  arm
  mips
  mipsel
  x86_64
  i386
  s390x)
  set(OUTPUT "early-linked-${ARCH}.ll")
  add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}"
    DEPENDS "${CMAKE_SOURCE_DIR}/share/revng/early-linked.c"
    COMMAND
      ${TARGET_CLANG} ARGS "${CMAKE_SOURCE_DIR}/share/revng/early-linked.c" -o
      "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}" -S -emit-llvm -g
      -DTARGET_${ARCH} -I"${CMAKE_SOURCE_DIR}/share/revng"
      -I"${CMAKE_CURRENT_SOURCE_DIR}/include")
  add_custom_target("early-linked-module-${OUTPUT}" ALL
                    DEPENDS "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}")
  add_dependencies(revng-all-binaries "early-linked-module-${OUTPUT}")
  list(APPEND HELPER_MODULE_LIST "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}")

  # Enable the support for C exceptions to avoid optimizations that break
  # exception support when linking a module with isolated functions
  foreach(CONFIG ${SUPPORT_MODULES_CONFIGS})
    set(OUTPUT "support-${ARCH}-${CONFIG}.ll")
    add_custom_command(
      OUTPUT "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}"
      DEPENDS "${CMAKE_SOURCE_DIR}/share/revng/support.c"
      COMMAND
        ${TARGET_CLANG} ARGS "${CMAKE_SOURCE_DIR}/share/revng/support.c" -O2
        -fexceptions -o "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}" -S
        -emit-llvm -g -DTARGET_${ARCH} -I"${CMAKE_SOURCE_DIR}/share/revng"
        -I"${CMAKE_CURRENT_SOURCE_DIR}/include"
        ${SUPPORT_MODULES_CONFIG_${CONFIG}})
    add_custom_target("support-module-${OUTPUT}" ALL
                      DEPENDS "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}")
    add_dependencies(revng-all-binaries "support-module-${OUTPUT}")
    list(APPEND HELPER_MODULE_LIST "${CMAKE_BINARY_DIR}/share/revng/${OUTPUT}")
  endforeach()

  # Annotate `libtcg-helpers-*.bc`. This loads the `libtcg-helpers` bitcode
  # files and runs analyses that determine which CSVs are accessed by each
  # helper function.
  set(LIBTCG_HELPERS
      "${CMAKE_INSTALL_PREFIX}/share/libtcg/libtcg-helpers-${ARCH}.bc")
  set(LIBTCG_HELPERS_FULL
      "${CMAKE_BINARY_DIR}/share/revng/libtcg-helpers-full-${ARCH}.bc")
  add_custom_command(
    OUTPUT "${LIBTCG_HELPERS_FULL}"
    DEPENDS "${LIBTCG_HELPERS}" revng-all-binaries
    COMMAND
      ./bin/revng opt -sroa -instsimplify -cpu-loop-exit
      -analyze-helper-arguments -fix-helpers
      -fix-helpers-architecture="${ARCH}" "${LIBTCG_HELPERS}" -o
      "${LIBTCG_HELPERS_FULL}")
  add_custom_target("annotated-libtcg-helpers-${ARCH}" ALL
                    DEPENDS "${LIBTCG_HELPERS_FULL}")

  function(process_helper_module)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs PASSES)
    cmake_parse_arguments(OPT "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN})

    set(PROCESS_HELPER_MODULE_OUTPUT
        "${CMAKE_BINARY_DIR}/share/revng/libtcg-helpers-${OPT_NAME}-${ARCH}.bc")
    list(TRANSFORM OPT_PASSES PREPEND -)
    add_custom_command(
      OUTPUT "${PROCESS_HELPER_MODULE_OUTPUT}"
      DEPENDS "${LIBTCG_HELPERS_FULL}" revng-all-binaries
      COMMAND ./bin/revng opt ${OPT_PASSES} "${LIBTCG_HELPERS_FULL}" -o
              "${PROCESS_HELPER_MODULE_OUTPUT}")
    add_custom_target("annotated-libtcg-helpers-${OPT_NAME}-${ARCH}" ALL
                      DEPENDS "${PROCESS_HELPER_MODULE_OUTPUT}")
    list(APPEND HELPER_MODULE_LIST "${PROCESS_HELPER_MODULE_OUTPUT}")
    set(HELPER_MODULE_LIST
        "${HELPER_MODULE_LIST}"
        PARENT_SCOPE)
  endfunction()

  # Produce to-inline version of helpers. In this version the bitcode file only
  # contains helper functions that are in the `revng_inline` section and the
  # CSVs that are used by them. This is used by `inline-helpers` to allow
  # inlining the body of the `revng_inline` function into the isolated
  # functions' bodies.
  process_helper_module(
    NAME
    to-inline
    PASSES
    slim-down-helpers-module
    sroa
    instsimplify
    simplifycfg
    strip-dead-debug-info)

  # Produce declarations-only version of helpers. In this version of the bitcode
  # file all the helper functions have been turned into declarations and all
  # CSVs have been dropped. This will be used in `lift` to import the helper
  # declarations to be used in the lifting process.
  process_helper_module(
    NAME declarations-only PASSES helpers-module-to-declarations globaldce
    strip-dead-debug-info)
endforeach()

# Produce well-known-models
file(
  GLOB
  WELL_KNOWN_BINARIES
  "${CMAKE_INSTALL_PREFIX}/share/revng/test/tests/well-known-models/*revng-qa.compiled-with-debug*"
)

add_custom_target(well-known-binaries ALL)

foreach(WELL_KNOWN_BINARY IN LISTS WELL_KNOWN_BINARIES)

  get_filename_component(BASENAME "${WELL_KNOWN_BINARY}" NAME)
  set(MODEL_PATH "share/revng/well-known-models/${BASENAME}.yml")
  set(FULL_MODEL_PATH "${CMAKE_BINARY_DIR}/${MODEL_PATH}")

if(REVNG_BUILD_WELL_KNOWN_BINARIES)
  # Produce well-known-models (dev/CI helper target).
  file(
    GLOB
    WELL_KNOWN_BINARIES
    "${CMAKE_INSTALL_PREFIX}/share/revng/test/tests/well-known-models/*revng-qa.compiled-with-debug*"
  )

  add_custom_target(well-known-binaries ALL)

  foreach(WELL_KNOWN_BINARY IN LISTS WELL_KNOWN_BINARIES)

    get_filename_component(BASENAME "${WELL_KNOWN_BINARY}" NAME)
    set(MODEL_PATH "share/revng/well-known-models/${BASENAME}.yml")
    set(FULL_MODEL_PATH "${CMAKE_BINARY_DIR}/${MODEL_PATH}")

    add_custom_command(
      OUTPUT "${FULL_MODEL_PATH}"
      COMMAND "./bin/revng" analyze import-binary -o "${FULL_MODEL_PATH}"
              "${WELL_KNOWN_BINARY}"
      MAIN_DEPENDENCY "${WELL_KNOWN_BINARY}"
      DEPENDS revng-all-binaries generate-revngModel-tuple-tree-code
              generate-revngPipeline-tuple-tree-code
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_custom_target("import-${BASENAME}" DEPENDS "${FULL_MODEL_PATH}")

    add_dependencies(well-known-binaries "import-${BASENAME}")

  endforeach()
endif()

#
# libexec/revng (helper scripts)
#
if(REVNG_INSTALL_LIBEXEC OR REVNG_BUILD_CLANG_FORMAT_DOTFILE)
  add_custom_target(
    copy_libexec ALL COMMAND cp -Tar "${CMAKE_SOURCE_DIR}/libexec/"
                             "${CMAKE_BINARY_DIR}/libexec/")

  if(REVNG_INSTALL_LIBEXEC)
    install(
      DIRECTORY "${CMAKE_BINARY_DIR}/libexec/"
      DESTINATION libexec/
      USE_SOURCE_PERMISSIONS)
  endif()
endif()

if(REVNG_BUILD_CLANG_FORMAT_DOTFILE)
  # Custom command to create .clang-format file from revng-check-conventions
  add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/share/revng/.clang-format"
    DEPENDS "${CMAKE_SOURCE_DIR}/libexec/revng/check-conventions"
            "${CMAKE_SOURCE_DIR}/share/revng/clang-format-style-file.yml"
            copy_share copy_libexec
    COMMAND
      "${CMAKE_BINARY_DIR}/libexec/revng/check-conventions" ARGS
      --print-clang-format-config >
      "${CMAKE_BINARY_DIR}/share/revng/.clang-format")

  add_custom_target(clang-format-dot-file ALL
                    DEPENDS "${CMAKE_BINARY_DIR}/share/revng/.clang-format")
endif()

#
# Export information useful for subdirectories
#
set(PYTHON_GENERATED_MODEL_PATH revng/model/_generated.py)

#
# Enable CTest
#
if(REVNG_BUILD_TESTS)
  enable_testing()
endif()

#
# Find Python using the FindPython module.
# Interpreter: always required (codegen scripts and revng CLI wrapper).
# Development: only required for native bindings (nanobind).
set(REVNG_PYTHON_COMPONENTS Interpreter)
if(REVNG_BUILD_PYTHON_BINDINGS)
  list(APPEND REVNG_PYTHON_COMPONENTS Development)
endif()
find_package(Python COMPONENTS ${REVNG_PYTHON_COMPONENTS} REQUIRED)
file(RELATIVE_PATH PYTHON_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}"
     "${Python_SITELIB}")

#
# Build docs
#
if(REVNG_BUILD_MKDOCS_HTML)
  configure_file(mkdocs.yml "${CMAKE_BINARY_DIR}" COPYONLY)
  set(HTML_DOCS_PATH "${CMAKE_CURRENT_BINARY_DIR}/share/doc/revng/html")
  find_program(MKDOCS NAMES mkdocs)
  if("${MKDOCS}" STREQUAL "MKDOCS-NOTFOUND")
    message(FATAL_ERROR
            "mkdocs not found, but REVNG_BUILD_MKDOCS_HTML is enabled. Install mkdocs or set -DREVNG_BUILD_MKDOCS_HTML=OFF."
    )
  endif()

  add_custom_command(
    OUTPUT share/doc/revng/html/404.html
    DEPENDS mkdocs.yml
            copy_share
            "share/doc/revng/references/abi-definition.md"
            "share/doc/revng/references/mime-types.md"
            "share/doc/revng/references/ptml.md"
            "share/doc/revng/references/cli/revng-common.md"
            "share/doc/revng/references/cli/revng-artifact.md"
            "share/doc/revng/references/cli/revng-analyze.md"
            "share/doc/revng/user-manual/index.md"
            "share/doc/revng/user-manual/initial-setup.md"
            "share/doc/revng/user-manual/key-concepts/metaaddress.md"
            "share/doc/revng/user-manual/key-concepts/model.md"
            "share/doc/revng/user-manual/key-concepts/artifacts-and-analyses.md"
            "share/doc/revng/user-manual/tutorial/model-from-scratch.md"
            "share/doc/revng/user-manual/tutorial/running-analyses.md"
            "share/doc/revng/what-is-revng.md"
            "share/doc/revng/index.md"
            generate-revngModel-tuple-tree-code
            generate-revngPipeline-tuple-tree-code
            python/revng/internal/cli/_commands/process_docs_yaml/pipeline.md.tpl
            python/revng/internal/cli/_commands/process_docs_yaml/artifacts.md.tpl
            python/revng/internal/cli/_commands/process_docs_yaml/analyses.md.tpl
            python/revng/internal/cli/_commands/process_docs_yaml/common.md.tpl
            share/revng/pipelines/revng-pipelines.yml
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMAND
      "./bin/revng" process-docs-yaml
      "${CMAKE_SOURCE_DIR}/python/revng/internal/cli/_commands/process_docs_yaml/pipeline.md.tpl"
      < "${CMAKE_SOURCE_DIR}/share/revng/pipelines/revng-pipelines.yml" >
      share/doc/revng/references/pipeline.md
    COMMAND
      "./bin/revng" process-docs-yaml
      "${CMAKE_SOURCE_DIR}/python/revng/internal/cli/_commands/process_docs_yaml/analyses.md.tpl"
      < "${CMAKE_SOURCE_DIR}/share/revng/pipelines/revng-pipelines.yml" >
      share/doc/revng/references/analyses.md
    COMMAND
      "./bin/revng" process-docs-yaml
      "${CMAKE_SOURCE_DIR}/python/revng/internal/cli/_commands/process_docs_yaml/artifacts.md.tpl"
      < "${CMAKE_SOURCE_DIR}/share/revng/pipelines/revng-pipelines.yml" >
      share/doc/revng/references/artifacts.md
    COMMAND ${MKDOCS} build --quiet --strict --site-dir mkdocs-output
    COMMAND rm -rf "${HTML_DOCS_PATH}"
    COMMAND mv -T mkdocs-output "${HTML_DOCS_PATH}"
    COMMENT "Generating mkdocs")
  add_custom_target(mkdocs ALL DEPENDS share/doc/revng/html/404.html)
endif()

#
# Gather helper names
#
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/share/revng/helper-list.csv"
  COMMAND
    ${CMAKE_SOURCE_DIR}/scripts/extract-helper-names.sh ${HELPER_MODULE_LIST} >
    "${CMAKE_BINARY_DIR}/share/revng/helper-list.csv"
  DEPENDS copy_share ${HELPER_MODULE_LIST})
add_custom_target(helper-list ALL
                  DEPENDS "${CMAKE_BINARY_DIR}/share/revng/helper-list.csv")

#
# Proceed to subdirectories
#
if(REVNG_BUILD_DOCS)
  add_subdirectory(docs)
endif()
add_subdirectory(include)
add_subdirectory(lib)
if(REVNG_BUILD_PYTHON)
  add_subdirectory(python)
else()
  if(REVNG_BUILD_PYTHON_BINDINGS OR REVNG_BUILD_PYTHON_WHEELS)
    message(FATAL_ERROR
            "REVNG_BUILD_PYTHON is OFF, but Python wheels/bindings were requested. "
            "Enable REVNG_BUILD_PYTHON or disable REVNG_BUILD_PYTHON_{BINDINGS,WHEELS}.")
  endif()
endif()
if(REVNG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(REVNG_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(REVNG_BUILD_TYPESCRIPT)
  add_subdirectory(typescript)
endif()
