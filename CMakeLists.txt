cmake_minimum_required(VERSION 3.10)

project(PerciTuner)

# CIR (Clang IR) support requires LLVM22 + MLIR. Enable only when -DPERCI_ENABLE_CIR=ON
option(PERCI_ENABLE_CIR "Enable CIR (Clang IR) support" OFF)

# Find the LLVM libraries
find_package(LLVM REQUIRED CONFIG)
# LLVM 19+ Clang's CMake config references MLIR targets, so MLIR must be
# found before Clang. On LLVM 17, Clang does not depend on MLIR and the
# MLIR config may be absent/broken, so we skip it.
if(LLVM_VERSION_MAJOR VERSION_GREATER_EQUAL 19)
    find_package(MLIR REQUIRED CONFIG)
endif()
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using Clang: ${Clang_DIR}")
if(PERCI_ENABLE_CIR)
    message(STATUS "CIR/MLIR support enabled")
endif()

# Add LLVM include directories
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
# Add LLVM CMake directories
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -fno-rtti -g")

# add_subdirectory(lib/check_result)
# add_subdirectory(lib/dd_real)

# Register pass plugin
add_llvm_pass_plugin(PerciTuner
    lib/RegisterPasses.cpp
    lib/PrintConfig.cpp
    lib/ChangeVariableDecl.cpp
    lib/ChangeMathCall.cpp
    lib/IRTiming.cpp
    lib/MergeMultiVersion.cpp
    lib/RecordResult.cpp
    lib/RemovePragma.cpp
    lib/FP128ToDD.cpp
)

add_library(PerciAST MODULE ast/PerciAST.cpp)

# Link against LLVM libraries
target_link_libraries(PerciTuner ${LLVM_LIBS})

###############################################################################
# Test suite
###############################################################################
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import lit"
                RESULT_VARIABLE _lit_rc OUTPUT_QUIET ERROR_QUIET)
if(_lit_rc EQUAL 0)
    add_custom_target(check
        COMMAND "${Python3_EXECUTABLE}" -m lit -v -j1 "${CMAKE_SOURCE_DIR}/test"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running Perci-Tuner lit tests" USES_TERMINAL VERBATIM)
    add_dependencies(check PerciTuner)
else()
    message(WARNING "lit not found; 'check' target unavailable (pip install lit)")
endif()

