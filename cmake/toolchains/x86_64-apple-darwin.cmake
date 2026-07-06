# x86_64 Apple Darwin toolchain for cross-compiling on ARM64 Mac
# Usage: cmake -B build-x86_64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/x86_64-apple-darwin.cmake

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_OSX_ARCHITECTURES x86_64)

# x86_64 Homebrew prefix
set(BREW_PREFIX "/usr/local")

# Find x86_64 LLVM installation
set(CMAKE_PREFIX_PATH "${BREW_PREFIX}/opt/llvm@17;${BREW_PREFIX}/opt/openssl@3")

# RPATH settings for proper library resolution
set(CMAKE_INSTALL_RPATH "${BREW_PREFIX}/opt/llvm@17/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_MACOSX_RPATH TRUE)

# Ensure we use Rosetta-compatible libraries
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
