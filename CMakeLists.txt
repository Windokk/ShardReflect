cmake_minimum_required(VERSION 3.15)
project(PulseReflect LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM and Clang
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in ${LLVM_DIR}")

set(GCC_INC_DIR "" CACHE PATH "Path to gcc's include dir")

include_directories(${LLVM_INCLUDE_DIRS} ${GCC_INC_DIR})
add_definitions(${LLVM_DEFINITIONS})

# Your executable
add_executable(PulseReflect src/main.cpp)

# Disable RTTI if needed
target_compile_options(PulseReflect PRIVATE -fno-rtti)

# Link clang tooling
target_link_libraries(PulseReflect
    PRIVATE
    LLVM
    clang-cpp
)
