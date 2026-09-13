cmake_minimum_required(VERSION 3.10)

project(PerciTuner)

# Find the LLVM libraries
find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using Clang: ${Clang_DIR}")

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
# CIR pass plugin (loaded by cir-opt; see scripts/run.sh `cir` option)
###############################################################################
add_library(PerciTunerCIR MODULE
    cir/RegisterCIRPasses.cpp
    cir/CIRPrintConfig.cpp
    cir/CIRTransform.cpp
)
set_target_properties(PerciTunerCIR PROPERTIES PREFIX "")
target_include_directories(PerciTunerCIR PRIVATE
    ${MLIR_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/cir
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
target_link_libraries(PerciTunerCIR PRIVATE
    MLIRCIR
    MLIRIR
    MLIRPass
)

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

