cmake_minimum_required(VERSION 3.16)
project(Cm
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Cm Programming Language Compiler"
)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(CM_DEBUG_BUILD "Enable debug logging macros" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(BUILD_TESTING "Build tests (use -DBUILD_TESTING=ON to enable)" OFF)
option(CM_USE_LLVM "Enable LLVM backend" ON)
option(CM_LLVM_STATIC "Link LLVM statically" OFF)

# Debug build definition
if(CM_DEBUG_BUILD)
    add_compile_definitions(CM_DEBUG_BUILD)
endif()

# LLVM Configuration
if(CM_USE_LLVM)
    # Find LLVM (try versions from newest to oldest, suppress warnings with QUIET)
    # Support LLVM 14-18 for compatibility
    find_package(LLVM 18 CONFIG QUIET)
    if(NOT LLVM_FOUND)
        find_package(LLVM 17 CONFIG QUIET)
    endif()
    if(NOT LLVM_FOUND)
        find_package(LLVM 16 CONFIG QUIET)
    endif()
    if(NOT LLVM_FOUND)
        find_package(LLVM 15 CONFIG QUIET)
    endif()
    if(NOT LLVM_FOUND)
        find_package(LLVM 14 CONFIG QUIET)
    endif()
    if(NOT LLVM_FOUND)
        # Final attempt without version requirement
        find_package(LLVM CONFIG REQUIRED)
    endif()

    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    # LLVM definitions
    add_compile_definitions(CM_LLVM_ENABLED)
    add_compile_definitions(${LLVM_DEFINITIONS})

    # LLVM include directories (SYSTEM to suppress warnings from LLVM headers)
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

    # Find the libraries that correspond to the LLVM components we need
    llvm_map_components_to_libnames(llvm_libs
        Core
        Support
        IRReader
        Passes
        CodeGen
        MC
        MCParser
        MCJIT
        OrcJIT
        ExecutionEngine
        Target

        # Target architectures (add as needed)
        X86CodeGen X86AsmParser X86Desc X86Info
        AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
        WebAssemblyCodeGen WebAssemblyAsmParser WebAssemblyDesc WebAssemblyInfo
    )

    # Create LLVM backend library
    add_library(cm_llvm_backend INTERFACE)
    target_include_directories(cm_llvm_backend INTERFACE
        ${CMAKE_SOURCE_DIR}/src/codegen/llvm/core
        ${CMAKE_SOURCE_DIR}/src/codegen/llvm/native
        ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm
    )

    if(CM_LLVM_STATIC)
        target_link_libraries(cm_llvm_backend INTERFACE ${llvm_libs})
    else()
        # Dynamic linking
        target_link_libraries(cm_llvm_backend INTERFACE LLVM)
        
        # Set RPATH to find LLVM shared libraries at runtime
        # This is crucial for finding libLLVM-*.so on Linux
        get_filename_component(LLVM_LIBRARY_DIR "${LLVM_DIR}/../.." ABSOLUTE)
        message(STATUS "LLVM library directory: ${LLVM_LIBRARY_DIR}")
        
        # Add RPATH settings
        set(CMAKE_INSTALL_RPATH "${LLVM_LIBRARY_DIR}")
        set(CMAKE_BUILD_RPATH "${LLVM_LIBRARY_DIR}")
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    endif()

    message(STATUS "LLVM backend enabled")
else()
    message(STATUS "LLVM backend disabled")
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
# cm実行ファイルはルートに、テスト実行ファイルはbuild/binに配置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# テスト実行ファイル用の出力ディレクトリ
set(TEST_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/mir_cpp)

# Common library (header-only for now)
add_library(cm_common INTERFACE)
target_include_directories(cm_common INTERFACE ${CMAKE_SOURCE_DIR}/src/common)

# Frontend library (header-only for now)
add_library(cm_frontend INTERFACE)
target_include_directories(cm_frontend INTERFACE ${CMAKE_SOURCE_DIR}/src/frontend)
target_link_libraries(cm_frontend INTERFACE cm_common)

# Main executable
set(CM_SOURCES
    src/main.cpp
    src/frontend/lexer/token.cpp
    src/frontend/parser/parser_stmt.cpp
    src/frontend/parser/parser_expr.cpp
    src/frontend/parser/parser_module.cpp
    # Type checking (分割済み)
    src/frontend/types/checking/decl.cpp
    src/frontend/types/checking/stmt.cpp
    src/frontend/types/checking/expr.cpp
    src/frontend/types/checking/call.cpp
    src/frontend/types/checking/generic.cpp
    src/frontend/types/checking/auto_impl.cpp
    src/frontend/types/checking/utils.cpp
    src/preprocessor/import.cpp
    src/module/resolver.cpp
    # HIR lowering (AST -> HIR)
    src/hir/lowering/impl.cpp
    src/hir/lowering/decl.cpp
    src/hir/lowering/stmt.cpp
    src/hir/lowering/expr.cpp
    # MIR lowering (HIR -> MIR)
    src/mir/optimizations/optimization_pipeline.cpp
    src/mir/lowering/impl.cpp
    src/mir/lowering/monomorphization_impl.cpp
    src/mir/lowering/monomorphization_utils.cpp
    # src/mir/lowering/monomorphization_stub.cpp   # Temporary stub until monomorphization is fixed
    src/mir/lowering/stmt.cpp
    src/mir/lowering/expr_basic.cpp
    src/mir/lowering/expr_ops.cpp
    src/mir/lowering/expr_call.cpp
    # auto_impl (トレイト別自動実装)
    src/mir/lowering/auto_impl/generator.cpp
    src/mir/lowering/auto_impl/eq.cpp
    src/mir/lowering/auto_impl/ord.cpp
    src/mir/lowering/auto_impl/clone_hash.cpp
    src/mir/lowering/auto_impl/debug_display_css.cpp
)

# Add LLVM sources if enabled
if(CM_USE_LLVM)
    list(APPEND CM_SOURCES
        # Common codegen
        src/codegen/common/type_converter.cpp
        src/codegen/common/runtime_functions.cpp
        # LLVM core (shared between native and wasm)
        src/codegen/llvm/core/context.cpp
        src/codegen/llvm/core/mir_to_llvm.cpp
        src/codegen/llvm/core/interface.cpp
        src/codegen/llvm/core/terminator.cpp
        src/codegen/llvm/core/print_codegen.cpp
        src/codegen/llvm/core/types.cpp
        src/codegen/llvm/core/operators.cpp
        src/codegen/llvm/core/utils.cpp
        src/codegen/llvm/core/intrinsics.cpp
        # LLVM optimizations
        src/codegen/llvm/optimizations/optimization_manager.cpp
        src/codegen/llvm/optimizations/peephole/peephole_optimizer.cpp
        src/codegen/llvm/optimizations/inst_combine/inst_combiner.cpp
        src/codegen/llvm/optimizations/vectorization/vectorizer.cpp
        src/codegen/llvm/optimizations/loop_unrolling/loop_unroller.cpp
        # LLVM native backend
        src/codegen/llvm/native/codegen.cpp
        src/codegen/llvm/native/target.cpp
        # JavaScript backend
        src/codegen/js/codegen.cpp
        src/codegen/js/control_flow.cpp
        src/codegen/js/emit_statements.cpp
        src/codegen/js/emit_expressions.cpp
        # JIT backend
        src/codegen/llvm/jit/jit_engine.cpp
    )
endif()

add_executable(cm ${CM_SOURCES})
target_link_libraries(cm PRIVATE cm_frontend)

# Link LLVM backend if enabled
if(CM_USE_LLVM)
    target_link_libraries(cm PRIVATE cm_llvm_backend)
    
    # Linux: JITがホストプロセスからシンボルを解決できるように
    # ランタイムシンボル（cm_print_*, cm_format_*など）をエクスポート
    if(UNIX AND NOT APPLE)
        set_target_properties(cm PROPERTIES ENABLE_EXPORTS ON)
        target_link_options(cm PRIVATE "-rdynamic")
    endif()
    
    # LLVMのホストターゲットを取得
    execute_process(
        COMMAND llvm-config --host-target
        OUTPUT_VARIABLE LLVM_HOST_TARGET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # ランタイムライブラリはUnix系でのみビルド（Windows/MSVCは未対応）
    if(NOT WIN32)
        # ランタイムライブラリ用のclangとターゲット設定
        # macOSではシステムclangを使用（Homebrew LLVMはSDK検出に問題がある）
        if(APPLE)
            set(CM_RUNTIME_CLANG "/usr/bin/clang")
        else()
            set(CM_RUNTIME_CLANG "clang")
        endif()
        
        if(APPLE)
            # macOSではSDKパスを検出してsysrootを設定（Homebrew LLVMに必要）
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(CM_RUNTIME_SYSROOT_FLAG "-isysroot ${MACOS_SDK_PATH}")
            
            if(LLVM_HOST_TARGET MATCHES "x86_64")
                set(CM_RUNTIME_ARCH_FLAG "--target=x86_64-apple-darwin ${CM_RUNTIME_SYSROOT_FLAG}")
            elseif(LLVM_HOST_TARGET MATCHES "arm64|aarch64")
                set(CM_RUNTIME_ARCH_FLAG "--target=arm64-apple-darwin ${CM_RUNTIME_SYSROOT_FLAG}")
            endif()
        else()
            # Linux
            if(LLVM_HOST_TARGET MATCHES "x86_64")
                set(CM_RUNTIME_ARCH_FLAG "--target=x86_64-unknown-linux-gnu")
            elseif(LLVM_HOST_TARGET MATCHES "arm64|aarch64")
                set(CM_RUNTIME_ARCH_FLAG "--target=aarch64-unknown-linux-gnu")
            else()
                set(CM_RUNTIME_ARCH_FLAG "")
            endif()
        endif()
        
        message(STATUS "LLVM host target: ${LLVM_HOST_TARGET}")
        message(STATUS "Runtime clang: ${CM_RUNTIME_CLANG}")
        message(STATUS "Runtime arch flag: ${CM_RUNTIME_ARCH_FLAG}")
        
        # Build LLVM runtime library
        # Cランタイムをコンパイルしてbuild/lib/に配置
        # -ffunction-sections: 関数ごとにセクションを分離（デッドコード削除を有効化）
        set(CM_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/src/codegen/llvm/native/runtime.c)
        set(CM_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_runtime.o)

        add_custom_command(
            OUTPUT ${CM_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG} -c ${CM_RUNTIME_SOURCE} -o ${CM_RUNTIME_OUTPUT} -O2 -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_RUNTIME_SOURCE}
            COMMENT "Building Cm runtime library (${LLVM_HOST_TARGET})"
        )


        # Build WASM runtime library
        set(CM_RUNTIME_WASM_SOURCE ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_wasm.c)
        set(CM_RUNTIME_WASM_FORMAT ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_format.c)
        set(CM_RUNTIME_WASM_PRINT ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_print.c)
        set(CM_RUNTIME_WASM_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_runtime_wasm.o)

        add_custom_command(
            OUTPUT ${CM_RUNTIME_WASM_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND clang -c ${CM_RUNTIME_WASM_SOURCE} -o ${CM_RUNTIME_WASM_OUTPUT} -O2 --target=wasm32-unknown-wasi -ffreestanding -fno-builtin
            DEPENDS ${CM_RUNTIME_WASM_SOURCE} ${CM_RUNTIME_WASM_FORMAT} ${CM_RUNTIME_WASM_PRINT}
            COMMENT "Building Cm WASM runtime library"
        )

        add_custom_target(cm_runtime ALL DEPENDS ${CM_RUNTIME_OUTPUT} ${CM_RUNTIME_WASM_OUTPUT})
        add_dependencies(cm cm_runtime)
        
        # ランタイムライブラリをcm実行ファイルにリンク（JIT用）
        # これによりランタイム関数がホストプロセスのシンボルテーブルに追加される
        target_link_libraries(cm PRIVATE ${CM_RUNTIME_OUTPUT})
        
        # ランタイムライブラリのパスをコンパイル時に埋め込む
        target_compile_definitions(cm PRIVATE
            CM_RUNTIME_PATH="${CM_RUNTIME_OUTPUT}"
            CM_RUNTIME_WASM_PATH="${CM_RUNTIME_WASM_OUTPUT}"
        )
    else()
        # Windows: ランタイムライブラリなし（ダミーパス）
        message(WARNING "Runtime library build is not supported on Windows/MSVC")
        target_compile_definitions(cm PRIVATE
            CM_RUNTIME_PATH=""
            CM_RUNTIME_WASM_PATH=""
        )
    endif()
endif()

# Testing
if(BUILD_TESTING)
    enable_testing()
    
    # Find Google Test
    find_package(GTest QUIET)
    
    if(GTest_FOUND)
        include(GoogleTest)
        
        # Unit tests - Lexer
        add_executable(lexer_test
            tests/unit/lexer_test.cpp
            src/frontend/lexer/token.cpp
        )
        set_target_properties(lexer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(lexer_test GTest::gtest_main cm_frontend)
        gtest_discover_tests(lexer_test PROPERTIES LABELS "unit")

        # Unit tests - HIR Lowering
        add_executable(hir_lowering_test
            tests/unit/hir_lowering_test.cpp
            src/frontend/lexer/token.cpp
            src/frontend/parser/parser_stmt.cpp
            src/frontend/parser/parser_expr.cpp
            src/frontend/parser/parser_module.cpp
            src/hir/lowering/impl.cpp
            src/hir/lowering/stmt.cpp
            src/hir/lowering/expr.cpp
            src/hir/lowering/decl.cpp
        )
        set_target_properties(hir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(hir_lowering_test GTest::gtest_main cm_frontend)
        gtest_discover_tests(hir_lowering_test PROPERTIES LABELS "unit")

        # Unit tests - MIR Lowering
        add_executable(mir_lowering_test
            tests/unit/mir_lowering_test.cpp
            src/frontend/lexer/token.cpp
            src/frontend/parser/parser_stmt.cpp
            src/frontend/parser/parser_expr.cpp
            src/frontend/parser/parser_module.cpp
            src/hir/lowering/impl.cpp
            src/hir/lowering/stmt.cpp
            src/hir/lowering/expr.cpp
            src/hir/lowering/decl.cpp
            src/mir/lowering/impl.cpp
            src/mir/lowering/stmt.cpp
            src/mir/lowering/expr_basic.cpp
            src/mir/lowering/expr_ops.cpp
            src/mir/lowering/expr_call.cpp
            src/mir/lowering/monomorphization_impl.cpp
            src/mir/lowering/monomorphization_utils.cpp
        )
        set_target_properties(mir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(mir_lowering_test GTest::gtest_main cm_frontend)
        gtest_discover_tests(mir_lowering_test PROPERTIES LABELS "unit")

        # Unit tests - MIR Optimization
        add_executable(mir_optimization_test
            tests/unit/mir_optimization_test.cpp
            src/frontend/lexer/token.cpp
            src/frontend/parser/parser_stmt.cpp
            src/frontend/parser/parser_expr.cpp
            src/frontend/parser/parser_module.cpp
            src/hir/lowering/impl.cpp
            src/hir/lowering/stmt.cpp
            src/hir/lowering/expr.cpp
            src/hir/lowering/decl.cpp
            src/mir/lowering/impl.cpp
            src/mir/lowering/stmt.cpp
            src/mir/lowering/expr_basic.cpp
            src/mir/lowering/expr_ops.cpp
            src/mir/lowering/expr_call.cpp
            src/mir/lowering/monomorphization_impl.cpp
            src/mir/lowering/monomorphization_utils.cpp
            src/mir/optimizations/optimization_pipeline.cpp
        )
        set_target_properties(mir_optimization_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(mir_optimization_test GTest::gtest_main cm_frontend)
        gtest_discover_tests(mir_optimization_test PROPERTIES LABELS "unit")

        # Unit tests - MIR Interpreter (disabled until MirBuilder is implemented)
        # add_executable(mir_interpreter_test
        #     tests/unit/mir_interpreter_test.cpp
        #     src/frontend/lexer/token.cpp
        #     src/frontend/parser/parser_stmt.cpp
        #     src/frontend/parser/parser_expr.cpp
        #     src/frontend/parser/parser_module.cpp
        # )
        # target_link_libraries(mir_interpreter_test GTest::gtest_main cm_frontend)
        # gtest_discover_tests(mir_interpreter_test PROPERTIES LABELS "unit")
    else()
        message(STATUS "Google Test not found, skipping C++ tests")
        message(STATUS "Install with: brew install googletest (macOS)")
    endif()
endif()

# Installation
# install(TARGETS cm RUNTIME DESTINATION bin)

