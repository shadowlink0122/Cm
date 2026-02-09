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

# ターゲットアーキテクチャ設定
# デフォルト: ホストアーキテクチャを自動検出
# 使用例: cmake -DCM_TARGET_ARCH=arm64 / cmake -DCM_TARGET_ARCH=x86_64
if(NOT CM_TARGET_ARCH)
    # LLVMのホストターゲットからアーキテクチャを検出
    # （cmコンパイラが生成するコードのアーキテクチャはLLVMに依存するため）
    execute_process(
        COMMAND llvm-config --host-target
        OUTPUT_VARIABLE _LLVM_HOST_TARGET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_LLVM_HOST_TARGET)
        # トリプルからアーキテクチャ部分を抽出（例: x86_64-apple-darwin → x86_64）
        string(REGEX REPLACE "^([^-]+)-.*" "\\1" CM_TARGET_ARCH "${_LLVM_HOST_TARGET}")
    else()
        # LLVMが見つからない場合はホストアーキテクチャにフォールバック
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE CM_TARGET_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT CM_TARGET_ARCH)
        set(CM_TARGET_ARCH "arm64")  # 最終フォールバック
    endif()
endif()
# aarch64 → arm64 に正規化（macOS表記に統一）
if(CM_TARGET_ARCH STREQUAL "aarch64")
    set(CM_TARGET_ARCH "arm64")
endif()
message(STATUS "Target architecture: ${CM_TARGET_ARCH}")

# Debug build definition
if(CM_DEBUG_BUILD)
    add_compile_definitions(CM_DEBUG_BUILD)
endif()

# LLVM Configuration
if(CM_USE_LLVM)
    # Try to find LLVM using CMake config files
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
        find_package(LLVM CONFIG QUIET)
    endif()

    if(LLVM_FOUND)
        # Standard CMake-based LLVM configuration
        message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
        message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

        add_compile_definitions(CM_LLVM_ENABLED)
        add_compile_definitions(${LLVM_DEFINITIONS})
        include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

        llvm_map_components_to_libnames(llvm_libs
            Core Support IRReader Passes CodeGen MC MCParser MCJIT OrcJIT ExecutionEngine Target
            X86CodeGen X86AsmParser X86Desc X86Info
            AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
            WebAssemblyCodeGen WebAssemblyAsmParser WebAssemblyDesc WebAssemblyInfo
        )

        add_library(cm_llvm_backend INTERFACE)
        target_include_directories(cm_llvm_backend INTERFACE
            ${CMAKE_SOURCE_DIR}/src/codegen/llvm/core
            ${CMAKE_SOURCE_DIR}/src/codegen/llvm/native
            ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm
        )

        if(CM_LLVM_STATIC)
            target_link_libraries(cm_llvm_backend INTERFACE ${llvm_libs})
        else()
            get_filename_component(LLVM_LIBRARY_DIR "${LLVM_DIR}/../.." ABSOLUTE)
            message(STATUS "LLVM library directory: ${LLVM_LIBRARY_DIR}")
            link_directories(${LLVM_LIBRARY_DIR})
            target_link_libraries(cm_llvm_backend INTERFACE LLVM)
            set(CMAKE_INSTALL_RPATH "${LLVM_LIBRARY_DIR}")
            set(CMAKE_BUILD_RPATH "${LLVM_LIBRARY_DIR}")
            set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        endif()

        message(STATUS "LLVM backend enabled (CMake config)")

    else()
        message(FATAL_ERROR "LLVM not found. Please install LLVM development packages.")
    endif()
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
    src/preprocessor/conditional.cpp
    src/module/resolver.cpp
    # Lint configuration
    src/lint/config.cpp
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

# JavaScript backend (LLVMに依存しない)
list(APPEND CM_SOURCES
    src/codegen/js/codegen.cpp
    src/codegen/js/control_flow.cpp
    src/codegen/js/emit_statements.cpp
    src/codegen/js/emit_expressions.cpp
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
        # JIT backend
        src/codegen/llvm/jit/jit_engine.cpp
        # std バッキング実装（C/C++/Obj-C++）
        std/sync/sync_runtime.cpp
        std/sync/channel_runtime.cpp
        std/thread/thread_runtime.cpp
        std/net/net_runtime.cpp
        std/http/http_runtime.cpp
    )

    # Apple環境: GPU(Metal)バッキング実装を追加
    if(APPLE)
        list(APPEND CM_SOURCES
            std/gpu/gpu_runtime.mm
        )
    endif()
endif()

add_executable(cm ${CM_SOURCES})
target_link_libraries(cm PRIVATE cm_frontend)

# OpenSSL検出（HTTPS対応）
# macOSではHomebrewのパスを優先的に検索
if(APPLE)
    # Homebrew ARM優先（/opt/homebrew）、Intel（/usr/local）にフォールバック
    if(EXISTS "/opt/homebrew/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
    elseif(EXISTS "/usr/local/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@3")
    endif()
endif()
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    target_include_directories(cm PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(cm PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(cm PRIVATE CM_HAS_OPENSSL)
else()
    message(WARNING "OpenSSL not found - HTTPS support disabled")
endif()

# Link LLVM backend if enabled
if(CM_USE_LLVM)
    target_link_libraries(cm PRIVATE cm_llvm_backend)

    # Apple: Metal/Foundation フレームワークをリンク（GPU演算サポート）
    if(APPLE)
        target_link_libraries(cm PRIVATE
            "-framework Metal"
            "-framework Foundation"
        )
    endif()
    
    # JITがホストプロセスからシンボルを解決できるように
    # ランタイムシンボル（cm_print_*, cm_format_*, gpu_*など）をエクスポート
    set_target_properties(cm PROPERTIES ENABLE_EXPORTS ON)
    if(UNIX AND NOT APPLE)
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
        
        message(STATUS "LLVM host target: ${LLVM_HOST_TARGET}")
        message(STATUS "Runtime clang: ${CM_RUNTIME_CLANG}")
        
        # CM_TARGET_ARCHに基づいてランタイムアーキテクチャフラグを決定
        if(APPLE)
            if(CM_RUNTIME_CLANG STREQUAL "/usr/bin/clang")
                # システムclang: -archフラグを使用
                set(CM_RUNTIME_ARCH_FLAG "-arch;${CM_TARGET_ARCH}")
            else()
                # Homebrew LLVM clang: --targetと-isysrootを使用
                execute_process(
                    COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE MACOS_SDK_PATH
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                if(CM_TARGET_ARCH STREQUAL "x86_64")
                    set(CM_RUNTIME_ARCH_FLAG "--target=x86_64-apple-darwin;-isysroot;${MACOS_SDK_PATH}")
                else()
                    set(CM_RUNTIME_ARCH_FLAG "--target=arm64-apple-darwin;-isysroot;${MACOS_SDK_PATH}")
                endif()
            endif()
        else()
            # Linux
            if(CM_TARGET_ARCH STREQUAL "x86_64")
                set(CM_RUNTIME_ARCH_FLAG "--target=x86_64-unknown-linux-gnu")
            else()
                set(CM_RUNTIME_ARCH_FLAG "--target=aarch64-unknown-linux-gnu")
            endif()
        endif()
        
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

        # Apple環境: GPUランタイムを別途ビルド（ネイティブコンパイル用）
        if(APPLE)
            set(CM_GPU_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/gpu/gpu_runtime.mm)
            set(CM_GPU_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_gpu_runtime.o)
            add_custom_command(
                OUTPUT ${CM_GPU_RUNTIME_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
                COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_GPU_RUNTIME_SOURCE}
                        -o ${CM_GPU_RUNTIME_OUTPUT} -O2 -std=c++17
                        -fobjc-arc ${CM_RUNTIME_ARCH_FLAG}
                DEPENDS ${CM_GPU_RUNTIME_SOURCE}
                COMMENT "Building Cm GPU runtime library (Metal)"
            )
        endif()

        # stdランタイムを個別オブジェクトファイルとしてビルド（LLVM Nativeリンク用）
        # --- Net Runtime ---
        set(CM_NET_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/net/net_runtime.cpp)
        set(CM_NET_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_net_runtime.o)
        add_custom_command(
            OUTPUT ${CM_NET_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_NET_RUNTIME_SOURCE}
                    -o ${CM_NET_RUNTIME_OUTPUT} -O2 -std=c++17
                    -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_NET_RUNTIME_SOURCE}
            COMMENT "Building Cm net runtime library"
        )

        # --- Sync Runtime (sync + channel を1つのアーカイブに) ---
        set(CM_SYNC_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/sync/sync_runtime.cpp)
        set(CM_CHANNEL_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/sync/channel_runtime.cpp)
        set(CM_SYNC_RUNTIME_OBJ ${CMAKE_BINARY_DIR}/lib/cm_sync_runtime_obj.o)
        set(CM_CHANNEL_RUNTIME_OBJ ${CMAKE_BINARY_DIR}/lib/cm_channel_runtime_obj.o)
        set(CM_SYNC_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_sync_runtime.a)
        add_custom_command(
            OUTPUT ${CM_SYNC_RUNTIME_OBJ}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_SYNC_RUNTIME_SOURCE}
                    -o ${CM_SYNC_RUNTIME_OBJ} -O2 -std=c++17
                    -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_SYNC_RUNTIME_SOURCE}
            COMMENT "Building Cm sync runtime object"
        )
        add_custom_command(
            OUTPUT ${CM_CHANNEL_RUNTIME_OBJ}
            COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_CHANNEL_RUNTIME_SOURCE}
                    -o ${CM_CHANNEL_RUNTIME_OBJ} -O2 -std=c++17
                    -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_CHANNEL_RUNTIME_SOURCE}
            COMMENT "Building Cm channel runtime object"
        )
        add_custom_command(
            OUTPUT ${CM_SYNC_RUNTIME_OUTPUT}
            COMMAND ar rcs ${CM_SYNC_RUNTIME_OUTPUT} ${CM_SYNC_RUNTIME_OBJ} ${CM_CHANNEL_RUNTIME_OBJ}
            DEPENDS ${CM_SYNC_RUNTIME_OBJ} ${CM_CHANNEL_RUNTIME_OBJ}
            COMMENT "Creating Cm sync runtime archive"
        )

        # --- Thread Runtime ---
        set(CM_THREAD_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/thread/thread_runtime.cpp)
        set(CM_THREAD_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_thread_runtime.o)
        add_custom_command(
            OUTPUT ${CM_THREAD_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_THREAD_RUNTIME_SOURCE}
                    -o ${CM_THREAD_RUNTIME_OUTPUT} -O2 -std=c++17
                    -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_THREAD_RUNTIME_SOURCE}
            COMMENT "Building Cm thread runtime library"
        )

        # --- HTTP Runtime ---
        set(CM_HTTP_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/std/http/http_runtime.cpp)
        set(CM_HTTP_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_http_runtime.o)
        # OpenSSLが見つかった場合はHTTPSサポート付きでビルド
        set(CM_HTTP_EXTRA_FLAGS "")
        if(OpenSSL_FOUND)
            set(CM_HTTP_EXTRA_FLAGS "-DCM_HAS_OPENSSL -I${OPENSSL_INCLUDE_DIR}")
        endif()
        add_custom_command(
            OUTPUT ${CM_HTTP_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG}++ -c ${CM_HTTP_RUNTIME_SOURCE}
                    -o ${CM_HTTP_RUNTIME_OUTPUT} -O2 -std=c++17
                    -ffunction-sections -fdata-sections ${CM_HTTP_EXTRA_FLAGS} ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_HTTP_RUNTIME_SOURCE}
            COMMENT "Building Cm HTTP runtime library"
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

        # ランタイムターゲット
        set(CM_STD_RUNTIME_DEPS
            ${CM_NET_RUNTIME_OUTPUT}
            ${CM_SYNC_RUNTIME_OUTPUT}
            ${CM_THREAD_RUNTIME_OUTPUT}
            ${CM_HTTP_RUNTIME_OUTPUT}
        )
        if(APPLE)
            add_custom_target(cm_runtime ALL DEPENDS
                ${CM_RUNTIME_OUTPUT} ${CM_RUNTIME_WASM_OUTPUT} ${CM_GPU_RUNTIME_OUTPUT}
                ${CM_STD_RUNTIME_DEPS}
            )
        else()
            add_custom_target(cm_runtime ALL DEPENDS
                ${CM_RUNTIME_OUTPUT} ${CM_RUNTIME_WASM_OUTPUT}
                ${CM_STD_RUNTIME_DEPS}
            )
        endif()
        add_dependencies(cm cm_runtime)
        
        # ランタイムライブラリをcm実行ファイルにリンク（JIT用）
        # これによりランタイム関数がホストプロセスのシンボルテーブルに追加される
        target_link_libraries(cm PRIVATE ${CM_RUNTIME_OUTPUT})
        
        # ランタイムライブラリのパスをコンパイル時に埋め込む
        if(APPLE)
            target_compile_definitions(cm PRIVATE
                CM_RUNTIME_PATH="${CM_RUNTIME_OUTPUT}"
                CM_RUNTIME_WASM_PATH="${CM_RUNTIME_WASM_OUTPUT}"
                CM_GPU_RUNTIME_PATH="${CM_GPU_RUNTIME_OUTPUT}"
                CM_NET_RUNTIME_PATH="${CM_NET_RUNTIME_OUTPUT}"
                CM_SYNC_RUNTIME_PATH="${CM_SYNC_RUNTIME_OUTPUT}"
                CM_THREAD_RUNTIME_PATH="${CM_THREAD_RUNTIME_OUTPUT}"
                CM_HTTP_RUNTIME_PATH="${CM_HTTP_RUNTIME_OUTPUT}"
                CM_DEFAULT_TARGET_ARCH="${CM_TARGET_ARCH}"
            )
        else()
            target_compile_definitions(cm PRIVATE
                CM_RUNTIME_PATH="${CM_RUNTIME_OUTPUT}"
                CM_RUNTIME_WASM_PATH="${CM_RUNTIME_WASM_OUTPUT}"
                CM_NET_RUNTIME_PATH="${CM_NET_RUNTIME_OUTPUT}"
                CM_SYNC_RUNTIME_PATH="${CM_SYNC_RUNTIME_OUTPUT}"
                CM_THREAD_RUNTIME_PATH="${CM_THREAD_RUNTIME_OUTPUT}"
                CM_HTTP_RUNTIME_PATH="${CM_HTTP_RUNTIME_OUTPUT}"
                CM_DEFAULT_TARGET_ARCH="${CM_TARGET_ARCH}"
            )
        endif()
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

