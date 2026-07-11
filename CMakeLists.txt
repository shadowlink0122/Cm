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

# ビルド高速化: ccache自動検出
# ccacheがインストールされている場合、コンパイララッパーとして自動使用
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    message(STATUS "ccache found: ${CCACHE_PROGRAM} (ビルドキャッシュ有効)")
else()
    message(STATUS "ccache not found (インストール推奨: brew install ccache)")
endif()

# ビルド高速化: Unity build（ソース結合による翻訳単位削減）
# ヘッダのパース回数を大幅削減し、ビルド時間を短縮
set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)

# VERSIONファイルからバージョンを読み取り、コンパイル時定数として埋め込む
file(READ "${CMAKE_SOURCE_DIR}/VERSION" CM_VERSION_RAW)
string(STRIP "${CM_VERSION_RAW}" CM_VERSION)
add_compile_definitions(CM_VERSION="${CM_VERSION}")

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
    # サポートするLLVMバージョン（降順で検索）
    set(CM_LLVM_SUPPORTED_VERSIONS 18 17 16 15 14)
    foreach(_ver IN LISTS CM_LLVM_SUPPORTED_VERSIONS)
        if(NOT LLVM_FOUND)
            find_package(LLVM ${_ver} CONFIG QUIET)
        endif()
    endforeach()
    # バージョン指定なしのフォールバック
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

        # クロスターゲット設定 (ARM + WebAssembly)
        # CM_NO_CROSS_TARGETSが有効な場合はx86のみ (Docker x86_64ビルドなど)
        option(CM_NO_CROSS_TARGETS "Disable ARM and WebAssembly cross-compilation targets" OFF)
        set(_cm_llvm_components
            Core Support IRReader Passes CodeGen MC MCParser MCJIT OrcJIT ExecutionEngine Target
            X86CodeGen X86AsmParser X86Desc X86Info
        )
        
        # CM_NO_CROSS_TARGETSが無効な場合は全ターゲット追加
        if(NOT CM_NO_CROSS_TARGETS)
            list(APPEND _cm_llvm_components
                AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
                WebAssemblyCodeGen WebAssemblyAsmParser WebAssemblyDesc WebAssemblyInfo
            )
        else()
            # コード側の #ifndef CM_NO_CROSS_TARGETS と同期
            add_compile_definitions(CM_NO_CROSS_TARGETS)
        endif()

        llvm_map_components_to_libnames(llvm_libs ${_cm_llvm_components})

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
            # libLLVM.dylibのフルパスを使用（link_directoriesは非推奨で
            # 複数LLVMインストール環境では正しく動作しないため）
            find_library(LLVM_SHARED_LIB
                NAMES LLVM LLVM-17
                PATHS ${LLVM_LIBRARY_DIR}
                NO_DEFAULT_PATH
            )
            if(LLVM_SHARED_LIB)
                message(STATUS "LLVM shared library: ${LLVM_SHARED_LIB}")
                target_link_libraries(cm_llvm_backend INTERFACE ${LLVM_SHARED_LIB})
            else()
                message(WARNING "LLVM shared library not found in ${LLVM_LIBRARY_DIR}, falling back to static")
                target_link_libraries(cm_llvm_backend INTERFACE ${llvm_libs})
            endif()
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
# cm実行ファイルはプロジェクトルートに配置（CI/Docker互換性のため）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# テスト実行ファイル用の出力ディレクトリ
set(TEST_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/mir_cpp)
include_directories(${CMAKE_SOURCE_DIR}/third-party/picosha2)

# Common library (header-only for now)
add_library(cm_common INTERFACE)
target_include_directories(cm_common INTERFACE ${CMAKE_SOURCE_DIR}/src/common)

# Frontend library (header-only for now)
add_library(cm_frontend INTERFACE)
target_include_directories(cm_frontend INTERFACE ${CMAKE_SOURCE_DIR}/src/frontend)
target_link_libraries(cm_frontend INTERFACE cm_common)

# ============================================
# コンポーネント別ソースリスト
# cm本体と単体テストターゲット（mir_lowering_test等）で共有する。
# ここに追加したファイルは両方に自動で反映されるため、
# テストターゲットへの登録漏れによるリンクエラーを防げる。
# ============================================
set(CM_LEXER_SOURCES
    src/frontend/lexer/token.cpp
    src/frontend/lexer/lexer.cpp
)
set(CM_PARSER_SOURCES
    src/frontend/parser/parser_stmt.cpp
    src/frontend/parser/parser_expr.cpp
    src/frontend/parser/parser_module.cpp
    src/frontend/parser/parser_decl.cpp
    src/frontend/parser/parser_type.cpp
)
set(CM_HIR_LOWERING_SOURCES
    src/hir/lowering/impl.cpp
    src/hir/lowering/decl.cpp
    src/hir/lowering/stmt.cpp
    src/hir/lowering/expr.cpp
    src/hir/lowering/expr_member.cpp
    src/hir/lowering/expr_match.cpp
)
# MIRコア（lowering + 最適化パス + 解析）
set(CM_MIR_SOURCES
    src/mir/nodes.cpp
    src/mir/optimizations/optimization_pipeline.cpp
    src/mir/lowering/context.cpp
    src/mir/lowering/base.cpp
    src/mir/lowering/lowering.cpp
    src/mir/lowering/auto_impl.cpp
    src/mir/lowering/impl.cpp
    src/mir/lowering/monomorphization_impl.cpp
    src/mir/lowering/monomorphization_utils.cpp
    # src/mir/lowering/monomorphization_stub.cpp   # Temporary stub until monomorphization is fixed
    src/mir/lowering/stmt.cpp
    src/mir/lowering/expr.cpp
    src/mir/lowering/expr_basic.cpp
    src/mir/lowering/expr_ops.cpp
    src/mir/lowering/expr_call.cpp
    src/mir/lowering/expr_interp.cpp
    # MIR passes (最適化パス実装)
    src/mir/passes/scalar/sccp.cpp
    src/mir/passes/scalar/folding.cpp
    src/mir/passes/scalar/propagation.cpp
    src/mir/passes/scalar/array_base_extraction.cpp
    src/mir/passes/cleanup/dce.cpp
    src/mir/passes/cleanup/dse.cpp
    src/mir/passes/cleanup/simplify_cfg.cpp
    src/mir/passes/cleanup/program_dce.cpp
    src/mir/passes/interprocedural/inlining.cpp
    src/mir/passes/interprocedural/tail_call_elimination.cpp
    src/mir/passes/loop/licm.cpp
    src/mir/passes/loop/const_unroll.cpp
    src/mir/passes/redundancy/gvn.cpp
    src/mir/passes/core/base.cpp
    src/mir/passes/core/manager.cpp
    src/mir/passes/convergence/manager.cpp
    src/mir/passes/convergence/smart.cpp
    src/mir/passes/validation/no_std_checker.cpp
    # MIR analysis (解析モジュール)
    src/mir/analysis/dominators.cpp
    src/mir/analysis/loop_analysis.cpp
)

# Main executable
set(CM_SOURCES
    src/main.cpp
    ${CM_LEXER_SOURCES}
    ${CM_PARSER_SOURCES}
    # Type checking (分割済み)
    src/cli/options.cpp
    src/frontend/types/scope.cpp
    src/frontend/types/generic_context.cpp
    src/frontend/types/checking/decl.cpp
    src/frontend/types/checking/stmt.cpp
    src/frontend/types/checking/expr.cpp
    src/frontend/types/checking/call.cpp
    src/frontend/types/checking/generic.cpp
    src/frontend/types/checking/auto_impl.cpp
    src/frontend/types/checking/utils.cpp
    src/preprocessor/import.cpp
    src/preprocessor/export_filter.cpp
    src/preprocessor/module_resolve.cpp
    src/preprocessor/conditional.cpp
    src/module/resolver.cpp
    # Lint configuration
    src/lint/config.cpp
    # HIR lowering (AST -> HIR)
    ${CM_HIR_LOWERING_SOURCES}
    # MIR lowering (HIR -> MIR) + パス + 解析
    ${CM_MIR_SOURCES}
    src/mir/printer.cpp
    # auto_impl (トレイト別自動実装)
    src/mir/lowering/auto_impl/generator.cpp
    src/mir/lowering/auto_impl/eq.cpp
    src/mir/lowering/auto_impl/ord.cpp
    src/mir/lowering/auto_impl/clone_hash.cpp
    src/mir/lowering/auto_impl/debug_display_css.cpp
    # MIR分割ユーティリティ（モジュール別分離コンパイル用）
    src/mir/mir_splitter.cpp
    # 共通ライブラリ (HPP/CPP分離 Phase5)
    src/fmt/formatter.cpp
    src/common/format_string.cpp
    src/common/source_location.cpp
    src/common/diagnostics.cpp
    src/common/cache_manager.cpp
    src/hir/string_interpolation.cpp
    src/diagnostics/engine.cpp
    src/diagnostics/catalog.cpp
)

# JavaScript backend (LLVMに依存しない)
list(APPEND CM_SOURCES
    src/codegen/buffered_codegen.cpp
    src/codegen/llvm/monitoring/block_monitor.cpp
    src/codegen/llvm/monitoring/codegen_monitor.cpp
    src/codegen/llvm/monitoring/output_monitor.cpp
    src/codegen/llvm/monitoring/compilation_guard.cpp
    src/codegen/js/codegen.cpp
    src/codegen/js/utilities.cpp
    src/codegen/js/emit_function.cpp
    src/codegen/js/analysis.cpp
    src/codegen/js/css_optimization.cpp
    src/codegen/js/structured_flow.cpp
    src/codegen/js/control_flow.cpp
    src/codegen/js/emit_statements.cpp
    src/codegen/js/emit_expressions.cpp
    src/codegen/js/validation.cpp
    src/codegen/js/builtins.cpp
    src/codegen/js/runtime.cpp
)

# SystemVerilog backend (LLVMに依存しない)
list(APPEND CM_SOURCES
    src/codegen/sv/codegen.cpp
    src/codegen/sv/analyze.cpp
    src/codegen/sv/emit_control.cpp
    src/codegen/sv/validation.cpp
    src/codegen/sv/testbench.cpp
    src/codegen/sv/expr_tree.cpp
    src/codegen/sv/hierarchy.cpp
    src/codegen/sv/constraints.cpp
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
        src/codegen/llvm/core/statement.cpp
        src/codegen/llvm/core/rvalue.cpp
        src/codegen/llvm/core/operand.cpp
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
        src/codegen/llvm/native/loop_detector.cpp
        # JIT backend
        src/codegen/llvm/jit/jit_engine.cpp
        # std バッキング実装（C/C++/Obj-C++）
        libs/native/sync/sync_runtime.cpp
        libs/native/sync/channel_runtime.cpp
        libs/native/thread/thread_runtime.cpp
        libs/native/net/net_runtime.cpp
        libs/native/http/http_runtime.cpp
    )

    # Apple環境: GPU(Metal)バッキング実装を追加
    # Objective-C++はUnity buildから除外（C++と結合不可）
    if(APPLE)
        list(APPEND CM_SOURCES
            libs/native/gpu/gpu_runtime.mm
        )
        set_source_files_properties(libs/native/gpu/gpu_runtime.mm
            PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
        )
    endif()
endif()

add_executable(cm ${CM_SOURCES})
target_link_libraries(cm PRIVATE cm_frontend)

# macOS: パーサ/TypeCheckerの深い再帰に対応するためスタックサイズを256MBに増加
# (デフォルト8MBでは大規模ソースのネスト構文でスタックオーバーフローが発生)
if(APPLE)
    set_target_properties(cm PROPERTIES LINK_FLAGS "-Wl,-stack_size,0x10000000")
endif()

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
if(OpenSSL_FOUND AND TARGET OpenSSL::SSL)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    target_include_directories(cm PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(cm PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(cm PRIVATE CM_HAS_OPENSSL)
else()
    message(STATUS "OpenSSL not found - HTTPS support disabled")
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
        
        # コアランタイムのビルド（JITシンボル解決に必須）
        # cm_runtime.oはcm実行ファイルにリンクされるためCMakeで管理
        set(CM_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/src/codegen/llvm/native/runtime.c)
        set(CM_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_runtime.o)

        add_custom_command(
            OUTPUT ${CM_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG} -c ${CM_RUNTIME_SOURCE} -o ${CM_RUNTIME_OUTPUT} -O2 -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_RUNTIME_SOURCE}
            COMMENT "Building Cm core runtime library (${LLVM_HOST_TARGET})"
        )

        add_custom_target(cm_runtime ALL DEPENDS ${CM_RUNTIME_OUTPUT})
        add_dependencies(cm cm_runtime)

        # WASMランタイムのビルド（wasm32ターゲット用クロスコンパイル）
        # Homebrew LLVMのclangを使用（システムclangはwasm32をサポートしない）
        set(CM_RUNTIME_WASM_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_runtime_wasm.o)
        find_program(CM_WASM_CLANG
            NAMES clang
            PATHS /opt/homebrew/opt/llvm@17/bin /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm@17/bin
            NO_DEFAULT_PATH
        )
        if(CM_WASM_CLANG)
            set(CM_RUNTIME_WASM_SOURCE ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_wasm.c)
            add_custom_command(
                OUTPUT ${CM_RUNTIME_WASM_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
                COMMAND ${CM_WASM_CLANG} -c ${CM_RUNTIME_WASM_SOURCE} -o ${CM_RUNTIME_WASM_OUTPUT}
                    --target=wasm32-wasi -O2 -ffunction-sections -fdata-sections
                    -nostdlib -D__wasi__
                    -I${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm
                DEPENDS ${CM_RUNTIME_WASM_SOURCE}
                    ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_format.c
                    ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_print.c
                    ${CMAKE_SOURCE_DIR}/src/codegen/llvm/wasm/runtime_slice.c
                COMMENT "Building Cm WASM runtime library (wasm32-wasi)"
            )
            add_custom_target(cm_runtime_wasm ALL DEPENDS ${CM_RUNTIME_WASM_OUTPUT})
            add_dependencies(cm cm_runtime_wasm)
            message(STATUS "WASM runtime: ${CM_WASM_CLANG} -> ${CM_RUNTIME_WASM_OUTPUT}")
        else()
            message(STATUS "WASM clang not found - WASM runtime will be compiled on-demand")
        endif()

        # コアランタイムをcm実行ファイルにリンク（JIT用シンボルエクスポート）
        target_link_libraries(cm PRIVATE ${CM_RUNTIME_OUTPUT})
        
        # stdランタイムライブラリのパス定義
        # ビルドは make libs (libs/native/Makefile) で実行
        # CM_RUNTIME_WASM_OUTPUT は上のWASMビルドセクションで定義済み
        set(CM_NET_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_net_runtime.o)
        set(CM_SYNC_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_sync_runtime.a)
        set(CM_THREAD_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_thread_runtime.o)
        set(CM_HTTP_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_http_runtime.o)
        if(APPLE)
            set(CM_GPU_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_gpu_runtime.o)
        endif()
        
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
    
    # FetchContentでGoogle Testをソースからビルド（アーキテクチャ整合性を保証）
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    # GMockを無効化（AppleClangでの互換性問題を回避）
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # FetchContentのGTestインクルードパスを取得
    FetchContent_GetProperties(googletest SOURCE_DIR GTEST_SRC_DIR)
    set(GTEST_INCLUDE_DIR "${GTEST_SRC_DIR}/googletest/include")
    
    include(GoogleTest)
        
        # Unit tests - Lexer
        add_executable(lexer_test
            tests/unit/lexer_test.cpp
            ${CM_LEXER_SOURCES}
        )
        set_target_properties(lexer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(lexer_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(lexer_test gtest_main cm_frontend)
        gtest_discover_tests(lexer_test PROPERTIES LABELS "unit")

        # Integration tests - HIR Lowering
        # （Cmプログラムを tests/integration/cases/hir_lowering/ から読み込む）
        add_executable(hir_lowering_test
            tests/integration/hir_lowering_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
        )
        set_target_properties(hir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(hir_lowering_test PRIVATE
            CM_HIR_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/integration/cases/hir_lowering")
        target_include_directories(hir_lowering_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(hir_lowering_test gtest_main cm_frontend)
        gtest_discover_tests(hir_lowering_test PROPERTIES LABELS "integration")

        # Integration tests - MIR Lowering
        # （Cmプログラムを tests/integration/cases/mir_lowering/ から読み込む）
        add_executable(mir_lowering_test
            tests/integration/mir_lowering_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
        )
        set_target_properties(mir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(mir_lowering_test PRIVATE
            CM_MIR_LOWERING_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/integration/cases/mir_lowering")
        target_include_directories(mir_lowering_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(mir_lowering_test gtest_main cm_frontend)
        gtest_discover_tests(mir_lowering_test PROPERTIES LABELS "integration")

        # Integration tests - MIR Optimization
        # （Cmプログラムを tests/integration/cases/mir_optimization/ から読み込む）
        add_executable(mir_optimization_test
            tests/integration/mir_optimization_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
        )
        set_target_properties(mir_optimization_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(mir_optimization_test PRIVATE
            CM_MIR_OPT_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/integration/cases/mir_optimization")
        target_include_directories(mir_optimization_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(mir_optimization_test gtest_main cm_frontend)
        gtest_discover_tests(mir_optimization_test PROPERTIES LABELS "integration")

        # Integration tests - SV Codegen
        add_executable(sv_codegen_test
            tests/integration/sv_codegen_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
            src/codegen/buffered_codegen.cpp
            src/codegen/sv/codegen.cpp
            src/codegen/sv/analyze.cpp
            src/codegen/sv/emit_control.cpp
            src/codegen/sv/validation.cpp
            src/codegen/sv/testbench.cpp
            src/codegen/sv/constraints.cpp
            src/codegen/sv/expr_tree.cpp
        )
        set_target_properties(sv_codegen_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        # ゴールデンテスト入力のCmソース断片ディレクトリ
        target_compile_definitions(sv_codegen_test PRIVATE
            CM_SV_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/integration/cases/sv")
        target_include_directories(sv_codegen_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(sv_codegen_test gtest_main cm_frontend)
        gtest_discover_tests(sv_codegen_test PROPERTIES LABELS "integration")

        # Integration tests - Formatter
        # （Cmプログラムを tests/integration/cases/formatter/ から読み込む）
        add_executable(formatter_test
            tests/integration/formatter_test.cpp
            src/fmt/formatter.cpp
        )
        set_target_properties(formatter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(formatter_test PRIVATE
            CM_FORMATTER_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/integration/cases/formatter")
        target_include_directories(formatter_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_include_directories(formatter_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(formatter_test gtest_main)
        gtest_discover_tests(formatter_test PROPERTIES LABELS "integration")

        # Unit tests - Error handling
        add_executable(error_test
            tests/unit/error_test.cpp
        )
        set_target_properties(error_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(error_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(error_test gtest_main)
        gtest_discover_tests(error_test PROPERTIES LABELS "unit")

        # Integration tests - MIR Interpreter (disabled until MirBuilder is implemented)
        # add_executable(mir_interpreter_test
        #     tests/integration/mir_interpreter_test.cpp
        #     src/frontend/lexer/token.cpp
        #     src/frontend/parser/parser_stmt.cpp
        #     src/frontend/parser/parser_expr.cpp
        #     src/frontend/parser/parser_module.cpp
        # )
        # target_link_libraries(mir_interpreter_test GTest::gtest_main cm_frontend)
        # gtest_discover_tests(mir_interpreter_test PROPERTIES LABELS "unit")
endif()

# Installation
# install(TARGETS cm RUNTIME DESTINATION bin)
