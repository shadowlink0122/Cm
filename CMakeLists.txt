cmake_minimum_required(VERSION 3.16)
project(Cm 
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Cm Programming Language Compiler"
)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(CM_DEBUG_BUILD "Enable debug logging macros" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(BUILD_TESTING "Build tests" ON)

# Debug build definition
if(CM_DEBUG_BUILD)
    add_compile_definitions(CM_DEBUG_BUILD)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
    
    # Debug/Release specific
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    
    # Coverage flags
    if(ENABLE_COVERAGE)
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    
    # Suppress some MSVC warnings
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Source files (to be added as development progresses)
# add_subdirectory(src/common)
# add_subdirectory(src/frontend)
# add_subdirectory(src/middle)
# add_subdirectory(src/backend)

# Main executable (placeholder)
# add_executable(cm src/main.cpp)

# Testing
if(BUILD_TESTING)
    enable_testing()
    
    # Find Google Test
    find_package(GTest QUIET)
    
    if(GTest_FOUND)
        include(GoogleTest)
        
        # Unit tests
        # add_executable(lexer_test tests/unit/lexer/lexer_test.cpp)
        # target_link_libraries(lexer_test GTest::gtest_main)
        # gtest_discover_tests(lexer_test PROPERTIES LABELS "unit")
        
        # Integration tests
        # add_executable(integration_test tests/integration/full_pipeline_test.cpp)
        # target_link_libraries(integration_test GTest::gtest_main)
        # gtest_discover_tests(integration_test PROPERTIES LABELS "integration")
    else()
        message(STATUS "Google Test not found, skipping C++ tests")
    endif()
    
    # E2E tests (always available)
    add_test(NAME e2e_tests
        COMMAND ${CMAKE_COMMAND} -E env 
            CM_BIN=$<TARGET_FILE:cm>
            ${CMAKE_SOURCE_DIR}/tests/e2e/run_all.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(e2e_tests PROPERTIES LABELS "e2e")
endif()

# Installation
# install(TARGETS cm RUNTIME DESTINATION bin)
