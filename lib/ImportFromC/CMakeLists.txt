#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# ImportFromC

revng_add_analyses_library_internal(revngImportFromCAnalysis HeaderToModel.cpp
                                    ImportFromCAnalysis.cpp)

target_link_libraries(
  revngImportFromCAnalysis
  revngTypeNames
  revngModel
  revngSupport
  revngPipeline
  revngPipes
  revngPTML
  revngBasicAnalyses
  revngModelToHeader
  # Fedora (and other distros) commonly ship the monolithic libclang-cpp.so,
  # not the individual libclang*.so component libraries.
  clang-cpp
  ${LLVM_LIBRARIES})
