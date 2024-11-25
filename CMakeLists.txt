cmake_minimum_required(VERSION 3.28)

project(PerciTuner)

# Find the LLVM libraries
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Add LLVM include directories
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
# Add LLVM CMake directories
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -fno-rtti")

# Register pass plugin
add_llvm_pass_plugin(PerciTuner
    lib/RegisterPasses.cpp
    lib/CreateCallDependency.cpp
)

# Link against LLVM libraries
target_link_libraries(PerciTuner ${LLVM_LIBS})

