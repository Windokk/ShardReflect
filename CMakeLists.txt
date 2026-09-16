cmake_minimum_required(VERSION 3.15)
project(ShardReflect LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MINGW AND NOT CMAKE_PREFIX_PATH)
  set(CMAKE_PREFIX_PATH "C:/msys64/mingw64" CACHE PATH "")
endif()

# Find LLVM and Clang
find_package(ZLIB REQUIRED)
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in ${LLVM_DIR}")

if(LLVM_LINK_LLVM_DYLIB)
  set(llvm_libs LLVM)
else()
  llvm_map_components_to_libnames(llvm_libs support core irreader)
endif()

set(GCC_INC_DIR "" CACHE PATH "Path to gcc's include dir")

include_directories(${LLVM_INCLUDE_DIRS} ${GCC_INC_DIR})
add_definitions(${LLVM_DEFINITIONS})

# Executable
add_executable(ShardReflect src/main.cpp)

# Disable RTTI if needed
#target_compile_options(ShardReflect PRIVATE -fno-rtti)

# Link clang tooling
target_link_libraries(ShardReflect
  PRIVATE
    clangTooling
    clangASTMatchers
    clangAST
    clangBasic
    clangFrontend
    ${llvm_libs}
)
