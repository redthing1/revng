#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# Core binary importer logic as a normal library, suitable for SDK consumers to
# link against without pulling in pipeline analysis registration.
revng_add_library_internal(
  revngModelBinaryImporter
  SHARED
  BinaryImporter.cpp
  ELFImporter.cpp
  MachOImporter.cpp
  PECOFFImporter.cpp)

# Command-line options and helpers for the binary importer.
revng_add_library_internal(revngModelBinaryImporterCLI SHARED
                           ImporterCLOptions.cpp)

# Pipeline analysis plugin that registers the "import-binary" analysis.
revng_add_analyses_library_internal(revngModelImporterBinary
                                   ImportBinaryAnalysis.cpp)

if(LLVM_LINK_LLVM_DYLIB)
  set(LLVM_LIBRARIES LLVM)
else()
  llvm_map_components_to_libnames(LLVM_LIBRARIES Object)
endif()
target_link_libraries(revngModelBinaryImporter revngModel
                      revngModelImporterDebugInfo revngABI ${LLVM_LIBRARIES})

target_link_libraries(revngModelBinaryImporterCLI revngSupport)

target_link_libraries(revngModelImporterBinary revngModelBinaryImporter
                      revngModelBinaryImporterCLI)
