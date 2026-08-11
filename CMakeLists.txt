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
if(CM_VERSION STREQUAL "")
    message(FATAL_ERROR "VERSIONファイルが空です。バージョンを設定してください")
endif()
add_compile_definitions(CM_VERSION="${CM_VERSION}")

# ヘルプ本文をデータファイルから生成ヘッダへ埋め込む
# （原本: src/cmd/cm/help_<lang>.txt。変更時は再configureで追従。
# メッセージカタログはC++側（src/internal/base/messages/ のenum+stringテーブル）で管理する。
# ヘルプの言語を追加するには CM_LANGS にコードを足し、help_<lang>.txt を置く）
set(CM_LANGS en ja)
set(CM_TEXTDATA_DECLS "")
set(CM_TEXTDATA_TABLE "")
foreach(_cm_lang IN LISTS CM_LANGS)
    file(READ "${CMAKE_SOURCE_DIR}/src/cmd/cm/help/${_cm_lang}.txt" _cm_lang_help)
    string(APPEND CM_TEXTDATA_DECLS
        "inline constexpr const char* kHelp_${_cm_lang} = R\"CMTEXT(${_cm_lang_help})CMTEXT\";\n\n")
    string(APPEND CM_TEXTDATA_TABLE
        "    {\"${_cm_lang}\", kHelp_${_cm_lang}},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/cmd/cm/help/${_cm_lang}.txt")
endforeach()
configure_file("${CMAKE_SOURCE_DIR}/src/internal/base/text_data.hpp.in"
               "${CMAKE_BINARY_DIR}/generated/internal/base/text_data.hpp" @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")

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
            # ARM(32bit)はベアメタル（--target=bm のthumbv7m）に必須。target.cpp の LLVMInitializeARM* と同期
            list(APPEND _cm_llvm_components
                AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
                ARMCodeGen ARMAsmParser ARMDesc ARMInfo
                WebAssemblyCodeGen WebAssemblyAsmParser WebAssemblyDesc WebAssemblyInfo
            )
        else()
            # コード側の #ifndef CM_NO_CROSS_TARGETS と同期
            add_compile_definitions(CM_NO_CROSS_TARGETS)
        endif()

        llvm_map_components_to_libnames(llvm_libs ${_cm_llvm_components})

        add_library(cm_llvm_backend INTERFACE)
        target_include_directories(cm_llvm_backend INTERFACE
            ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/core
            ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native
            ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm
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
include_directories(${CMAKE_SOURCE_DIR}/third-party/picosha2)

# Common library (header-only for now)
add_library(cm_common INTERFACE)
target_include_directories(cm_common INTERFACE ${CMAKE_SOURCE_DIR}/src/internal/base)

# Frontend library (header-only for now)
add_library(cm_frontend INTERFACE)
target_include_directories(cm_frontend INTERFACE ${CMAKE_SOURCE_DIR}/src/internal)
target_link_libraries(cm_frontend INTERFACE cm_common)

# ============================================
# コンポーネント別ソースリスト
# cm本体と単体テストターゲット（mir_lowering_test等）で共有する。
# ここに追加したファイルは両方に自動で反映されるため、
# テストターゲットへの登録漏れによるリンクエラーを防げる。
# ============================================
set(CM_LEXER_SOURCES
    src/internal/syntax/lexer/token.cpp
    src/internal/syntax/lexer/lexer.cpp
)
# AST型ノード（型ヘルパー・型文字列化の実装）
set(CM_AST_SOURCES
    src/internal/syntax/ast/types.cpp
    src/internal/syntax/ast/typekey.cpp
    src/internal/syntax/ast/convkind.cpp
)
# メッセージi18nカタログ本文テーブル（i18n::msg/msgf を使うターゲットは必ずリンクする）
# 型検査（cm本体と回帰テストターゲットで共有。型情報に依存するMIR loweringの検証に必要）
set(CM_TYPECHECK_SOURCES
    src/internal/types/scope.cpp
    src/internal/types/generic_context.cpp
    src/internal/types/checking/decl.cpp
    src/internal/types/checking/stmt.cpp
    src/internal/types/checking/expr/primary.cpp
    src/internal/types/checking/expr/operator.cpp
    src/internal/types/checking/expr/match.cpp
    src/internal/types/checking/expr/lambda.cpp
    src/internal/types/checking/call/function.cpp
    src/internal/types/checking/call/method.cpp
    src/internal/types/checking/generic.cpp
    src/internal/types/checking/generic/bounds.cpp
    src/internal/types/checking/auto_impl.cpp
    src/internal/types/checking/utils/compat.cpp
    src/internal/types/checking/utils/conversion.cpp
    src/internal/types/checking/utils/interp.cpp
    src/internal/types/checking/utils/diagnostics.cpp
    src/internal/types/checking/utils/naming.cpp
    src/internal/types/checking/utils/eval.cpp
    src/internal/types/checking/match_hoist.cpp
)
set(CM_I18N_SOURCES
    src/internal/base/messages/messages.cpp
)
set(CM_PARSER_SOURCES
    src/internal/syntax/parser/parser_stmt.cpp
    src/internal/syntax/parser/expr/binary.cpp
    src/internal/syntax/parser/expr/postfix.cpp
    src/internal/syntax/parser/expr/primary.cpp
    src/internal/syntax/parser/expr/match.cpp
    src/internal/syntax/parser/module/decl.cpp
    src/internal/syntax/parser/module/attribute.cpp
    src/internal/syntax/parser/module/toplevel.cpp
    src/internal/syntax/parser/parser_decl.cpp
    src/internal/syntax/parser/parser_type.cpp
)
set(CM_HIR_LOWERING_SOURCES
    src/internal/hir/lowering/impl.cpp
    src/internal/hir/lowering/decl.cpp
    src/internal/hir/lowering/stmt.cpp
    src/internal/hir/lowering/expr.cpp
    src/internal/hir/lowering/expr_member.cpp
    src/internal/hir/lowering/expr_match.cpp
)
# MIRコア（lowering + 最適化パス + 解析）
set(CM_MIR_SOURCES
    src/internal/mir/nodes.cpp
    src/internal/mir/optimizations/optimization_pipeline.cpp
    src/internal/mir/lowering/context.cpp
    src/internal/mir/lowering/base.cpp
    src/internal/mir/lowering/lowering.cpp
    src/internal/mir/lowering/auto_impl.cpp
    src/internal/mir/lowering/auto_impl_compare.cpp
    src/internal/mir/lowering/auto_impl_css.cpp
    src/internal/mir/lowering/auto_impl_debug.cpp
    src/internal/mir/lowering/impl.cpp
    src/internal/mir/lowering/mono/scan.cpp
    src/internal/mir/lowering/mono/specialize.cpp
    src/internal/mir/lowering/mono/canonicalize.cpp
    src/internal/mir/lowering/mono/rewrite.cpp
    src/internal/mir/lowering/mono/driver.cpp
    src/internal/mir/lowering/mono/typeinfo.cpp
    src/internal/mir/lowering/mono_internal.cpp
    src/internal/mir/lowering/mono_structs.cpp
    src/internal/mir/lowering/monomorphization_utils.cpp
    # src/internal/mir/lowering/monomorphization_stub.cpp   # Temporary stub until monomorphization is fixed
    src/internal/mir/lowering/stmt/lower.cpp
    src/internal/mir/lowering/stmt/let.cpp
    src/internal/mir/lowering/stmt/assign.cpp
    src/internal/mir/lowering/stmt/control.cpp
    src/internal/mir/lowering/stmt/scope.cpp
    src/internal/mir/lowering/stmt/temp_drop.cpp
    src/internal/mir/lowering/stmt/asm.cpp
    src/internal/mir/lowering/expr.cpp
    src/internal/mir/lowering/expr/basic.cpp
    src/internal/mir/lowering/expr/access.cpp
    src/internal/mir/lowering/expr/construct.cpp
    src/internal/mir/lowering/expr/materialize.cpp
    src/internal/mir/lowering/expr/cast.cpp
    src/internal/mir/lowering/expr/binary.cpp
    src/internal/mir/lowering/expr/unary.cpp
    src/internal/mir/lowering/expr_call.cpp
    src/internal/mir/lowering/expr_interp.cpp
    src/internal/mir/lowering/expr_println.cpp
    src/internal/mir/lowering/expr_slice.cpp
    # MIR passes (最適化パス実装)
    src/internal/mir/passes/instrumentation/undefined.cpp
    src/internal/mir/passes/instrumentation/bounds.cpp
    src/internal/mir/passes/scalar/sccp.cpp
    src/internal/mir/passes/scalar/folding.cpp
    src/internal/mir/passes/scalar/propagation.cpp
    src/internal/mir/passes/scalar/array_base_extraction.cpp
    src/internal/mir/passes/cleanup/dce.cpp
    src/internal/mir/passes/cleanup/dse.cpp
    src/internal/mir/passes/cleanup/string_reassign_free.cpp
    src/internal/mir/passes/cleanup/simplify_cfg.cpp
    src/internal/mir/passes/cleanup/program_dce.cpp
    src/internal/mir/passes/interprocedural/inlining.cpp
    src/internal/mir/passes/interprocedural/tail_call_elimination.cpp
    src/internal/mir/passes/loop/licm.cpp
    src/internal/mir/passes/loop/const_unroll.cpp
    src/internal/mir/passes/redundancy/gvn.cpp
    src/internal/mir/passes/core/base.cpp
    src/internal/mir/passes/core/manager.cpp
    src/internal/mir/passes/convergence/manager.cpp
    src/internal/mir/passes/convergence/smart.cpp
    src/internal/mir/passes/validation/no_std_checker.cpp
    # MIR analysis (解析モジュール)
    src/internal/mir/analysis/dominators.cpp
    src/internal/mir/analysis/loop_analysis.cpp
)

# Main executable
set(CM_SOURCES
    src/cmd/cm/main.cpp
    src/cmd/cm/util.cpp
    src/cmd/cm/check.cpp
    src/cmd/cm/frontend.cpp
    src/internal/macro/derive.cpp
    src/cmd/cm/fmt.cpp
    src/cmd/cm/build.cpp
    src/cmd/cm/backend/run.cpp
    src/cmd/cm/backend/sv.cpp
    src/cmd/cm/backend/js.cpp
    src/cmd/cm/backend/llvm.cpp
    ${CM_LEXER_SOURCES}
    ${CM_AST_SOURCES}
    ${CM_PARSER_SOURCES}
    ${CM_I18N_SOURCES}
    # Type checking (分割済み)
    src/cmd/cm/options.cpp
    ${CM_TYPECHECK_SOURCES}
    src/internal/preprocessor/import/setup.cpp
    src/internal/preprocessor/module_resolve.cpp
    src/internal/preprocessor/conditional.cpp
    src/internal/module/resolver.cpp
    src/internal/module/graph.cpp
    src/internal/hir/type_audit.cpp
    # Lint configuration
    src/internal/lint/config.cpp
    # HIR lowering (AST -> HIR)
    ${CM_HIR_LOWERING_SOURCES}
    # MIR lowering (HIR -> MIR) + パス + 解析
    ${CM_MIR_SOURCES}
    src/internal/mir/printer.cpp
    # auto_impl (トレイト別自動実装)
    # MIR分割ユーティリティ（モジュール別分離コンパイル用）
    src/internal/mir/mir_splitter.cpp
    # 共通ライブラリ (HPP/CPP分離 Phase5)
    src/internal/fmt/formatter.cpp
    src/internal/base/format_string.cpp
    src/internal/base/source_location.cpp
    src/internal/base/diagnostics.cpp
    src/internal/base/diag_emitter.cpp
    src/internal/hir/string_interpolation.cpp
    src/internal/diagnostics/engine.cpp
    src/internal/diagnostics/catalog.cpp
)

# JavaScript backend (LLVMに依存しない)
list(APPEND CM_SOURCES
    src/internal/codegen/buffered_codegen.cpp
    src/internal/codegen/llvm/monitoring/block_monitor.cpp
    src/internal/codegen/llvm/monitoring/codegen_monitor.cpp
    src/internal/codegen/llvm/monitoring/output_monitor.cpp
    src/internal/codegen/llvm/monitoring/compilation_guard.cpp
    src/internal/codegen/js/codegen.cpp
    src/internal/codegen/js/utilities.cpp
    src/internal/codegen/js/emit_function.cpp
    src/internal/codegen/js/analysis.cpp
    src/internal/codegen/js/css_optimization.cpp
    src/internal/codegen/js/structured_flow.cpp
    src/internal/codegen/js/control_flow.cpp
    src/internal/codegen/js/emit_statements.cpp
    src/internal/codegen/js/emit_expressions.cpp
    src/internal/codegen/js/validation.cpp
    src/internal/codegen/js/builtins.cpp
    src/internal/codegen/js/runtime.cpp
)

# SystemVerilog backend (LLVMに依存しない)
list(APPEND CM_SOURCES
    src/internal/codegen/sv/codegen.cpp
    src/internal/codegen/sv/analyze.cpp
    src/internal/codegen/sv/analyze/globals.cpp
    src/internal/codegen/sv/analyze/ports.cpp
    src/internal/codegen/sv/analyze/clock.cpp
    src/internal/codegen/sv/analyze/declarations.cpp
    src/internal/codegen/sv/analyze/function.cpp
    src/internal/codegen/sv/self_param.cpp
    src/internal/codegen/sv/emit_control.cpp
    src/internal/codegen/sv/validation.cpp
    src/internal/codegen/sv/testbench.cpp
    src/internal/codegen/sv/expr_tree.cpp
    src/internal/codegen/sv/hierarchy.cpp
    src/internal/codegen/sv/constraints.cpp
)

# Add LLVM sources if enabled
if(CM_USE_LLVM)
    list(APPEND CM_SOURCES
        # Common codegen
        src/internal/codegen/common/type_converter.cpp
        src/internal/codegen/common/runtime_functions.cpp
        # LLVM core (shared between native and wasm)
        src/internal/codegen/llvm/core/context.cpp
        src/internal/codegen/llvm/core/mir_to_llvm.cpp
        src/internal/codegen/llvm/core/translate/program.cpp
        src/internal/codegen/llvm/core/translate/signature.cpp
        src/internal/codegen/llvm/core/translate/function.cpp
        src/internal/codegen/llvm/core/statement.cpp
        src/internal/codegen/llvm/core/statement/assign.cpp
        src/internal/codegen/llvm/core/statement/asm.cpp
        src/internal/codegen/llvm/core/rvalue.cpp
        src/internal/codegen/llvm/core/operand.cpp
        src/internal/codegen/llvm/core/interface.cpp
        src/internal/codegen/llvm/core/terminator.cpp
        src/internal/codegen/llvm/core/terminator/dispatch.cpp
        src/internal/codegen/llvm/core/terminator/call.cpp
        src/internal/codegen/llvm/core/terminator/invoke.cpp
        src/internal/codegen/llvm/core/print_codegen.cpp
        src/internal/codegen/llvm/core/types.cpp
        src/internal/codegen/llvm/core/operators.cpp
        src/internal/codegen/llvm/core/utils.cpp
        src/internal/codegen/llvm/core/runtime/builtins.cpp
        src/internal/codegen/llvm/core/runtime/system.cpp
        src/internal/codegen/llvm/core/intrinsics.cpp
        # LLVM optimizations
        src/internal/codegen/llvm/optimizations/optimization_manager.cpp
        src/internal/codegen/llvm/optimizations/peephole/peephole_optimizer.cpp
        src/internal/codegen/llvm/optimizations/inst_combine/inst_combiner.cpp
        src/internal/codegen/llvm/optimizations/vectorization/vectorizer.cpp
        src/internal/codegen/llvm/optimizations/loop_unrolling/loop_unroller.cpp
        # LLVM native backend
        src/internal/codegen/llvm/native/codegen.cpp
        src/internal/codegen/llvm/native/target.cpp
        src/internal/codegen/llvm/native/loop_detector.cpp
        # JIT backend
        src/internal/codegen/llvm/jit/jit_engine.cpp
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
        set(CM_RUNTIME_SOURCE ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime.c)
        set(CM_RUNTIME_OUTPUT ${CMAKE_BINARY_DIR}/lib/cm_runtime.o)

        add_custom_command(
            OUTPUT ${CM_RUNTIME_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CM_RUNTIME_CLANG} -c ${CM_RUNTIME_SOURCE} -o ${CM_RUNTIME_OUTPUT} -O2 -ffunction-sections -fdata-sections ${CM_RUNTIME_ARCH_FLAG}
            DEPENDS ${CM_RUNTIME_SOURCE}
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_alloc.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_file.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_asm.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_format.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_io.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_time.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_platform.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_print.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/native/runtime_slice.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_slice_core.inc
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_hof_core.inc
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
            set(CM_RUNTIME_WASM_SOURCE ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm/runtime_wasm.c)
            add_custom_command(
                OUTPUT ${CM_RUNTIME_WASM_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
                COMMAND ${CM_WASM_CLANG} -c ${CM_RUNTIME_WASM_SOURCE} -o ${CM_RUNTIME_WASM_OUTPUT}
                    --target=wasm32-wasi -O2 -ffunction-sections -fdata-sections
                    -nostdlib -D__wasi__
                    -I${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm
                DEPENDS ${CM_RUNTIME_WASM_SOURCE}
                    ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm/runtime_format.c
                    ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm/runtime_math.c
                    ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm/runtime_print.c
                    ${CMAKE_SOURCE_DIR}/src/internal/codegen/llvm/wasm/runtime_slice.c
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_slice_core.inc
                ${CMAKE_SOURCE_DIR}/src/internal/codegen/common/runtime_hof_core.inc
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

    # テスト一覧の検出をビルド時（POST_BUILD）ではなくctest実行時（PRE_TEST）に行う。
    # POST_BUILDだと並列makeの最中に各テストバイナリを実行してJSONを生成するため、CI高負荷時に
    # 出力の取りこぼしで "string(JSON ...) failed parsing" （空/不正JSON）が散発する。
    # PRE_TESTなら検出はctest時に移り、ビルド自体はバイナリを実行しないので安定する。
    set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

        # Unit tests - Lexer
        # （エスケープ診断がi18nメッセージ本文を参照するためCM_I18N_SOURCESも必要）
        add_executable(lexer_test
            tests/unit/lexer_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(lexer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(lexer_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(lexer_test gtest_main cm_frontend)
        gtest_discover_tests(lexer_test PROPERTIES LABELS "unit")

        # Integration tests - HIR Lowering
        # （Cmプログラムを tests/regression/cases/hir_lowering/ から読み込む）
        add_executable(hir_lowering_test
            tests/regression/hir_lowering_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(hir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(hir_lowering_test PRIVATE
            CM_HIR_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/hir_lowering")
        target_include_directories(hir_lowering_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(hir_lowering_test gtest_main cm_frontend)
        gtest_discover_tests(hir_lowering_test PROPERTIES LABELS "regression")

        # Integration tests - HIR型不変条件の監査（typed-hir-single-source）
        add_executable(hir_type_audit_test
            tests/regression/hir_type_audit_test.cpp
            src/internal/hir/type_audit.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_TYPECHECK_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(hir_type_audit_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(hir_type_audit_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(hir_type_audit_test gtest_main cm_frontend)
        gtest_discover_tests(hir_type_audit_test PROPERTIES LABELS "regression")

        # Integration tests - 数値縮小診断の文脈網羅（局所処理調査F系）
        # （Cmプログラムを tests/regression/cases/narrowing_diag/ から読み込む）
        add_executable(narrowing_diag_test
            tests/regression/narrowing_diag_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_TYPECHECK_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(narrowing_diag_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(narrowing_diag_test PRIVATE
            CM_NARROWING_DIAG_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/narrowing_diag")
        target_include_directories(narrowing_diag_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(narrowing_diag_test gtest_main cm_frontend)
        gtest_discover_tests(narrowing_diag_test PROPERTIES LABELS "regression")

        # Integration tests - MIR Lowering
        # （Cmプログラムを tests/regression/cases/mir_lowering/ から読み込む）
        add_executable(mir_lowering_test
            tests/regression/mir_lowering_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_TYPECHECK_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(mir_lowering_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(mir_lowering_test PRIVATE
            CM_MIR_LOWERING_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/mir_lowering")
        target_include_directories(mir_lowering_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(mir_lowering_test gtest_main cm_frontend)
        gtest_discover_tests(mir_lowering_test PROPERTIES LABELS "regression")

        # Integration tests - MIR Optimization
        # （Cmプログラムを tests/regression/cases/mir_optimization/ から読み込む）
        add_executable(mir_optimization_test
            tests/regression/mir_optimization_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(mir_optimization_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(mir_optimization_test PRIVATE
            CM_MIR_OPT_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/mir_optimization")
        target_include_directories(mir_optimization_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(mir_optimization_test gtest_main cm_frontend)
        gtest_discover_tests(mir_optimization_test PROPERTIES LABELS "regression")

        # Integration tests - SV Codegen
        add_executable(sv_codegen_test
            tests/regression/sv_codegen_test.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_TYPECHECK_SOURCES}
            ${CM_HIR_LOWERING_SOURCES}
            ${CM_MIR_SOURCES}
            ${CM_I18N_SOURCES}
            src/internal/codegen/buffered_codegen.cpp
            src/internal/codegen/sv/codegen.cpp
            src/internal/codegen/sv/analyze.cpp
            src/internal/codegen/sv/analyze/globals.cpp
            src/internal/codegen/sv/analyze/ports.cpp
            src/internal/codegen/sv/analyze/clock.cpp
            src/internal/codegen/sv/analyze/declarations.cpp
            src/internal/codegen/sv/analyze/function.cpp
            src/internal/codegen/sv/self_param.cpp
            src/internal/codegen/sv/emit_control.cpp
            src/internal/codegen/sv/validation.cpp
            src/internal/codegen/sv/testbench.cpp
            src/internal/codegen/sv/constraints.cpp
            src/internal/codegen/sv/expr_tree.cpp
        )
        set_target_properties(sv_codegen_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        # ゴールデンテスト入力のCmソース断片ディレクトリ
        target_compile_definitions(sv_codegen_test PRIVATE
            CM_SV_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/sv")
        target_include_directories(sv_codegen_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(sv_codegen_test gtest_main cm_frontend)
        gtest_discover_tests(sv_codegen_test PROPERTIES LABELS "regression")

        # Integration tests - Formatter
        # （Cmプログラムを tests/regression/cases/formatter/ から読み込む）
        add_executable(formatter_test
            tests/regression/formatter_test.cpp
            src/internal/fmt/formatter.cpp
            ${CM_I18N_SOURCES}
        )
        set_target_properties(formatter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_compile_definitions(formatter_test PRIVATE
            CM_FORMATTER_CASE_DIR="${CMAKE_SOURCE_DIR}/tests/regression/cases/formatter")
        target_include_directories(formatter_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_include_directories(formatter_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(formatter_test gtest_main)
        gtest_discover_tests(formatter_test PROPERTIES LABELS "regression")

        # Unit tests - Error handling
        add_executable(error_test
            tests/unit/error_test.cpp
        )
        set_target_properties(error_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(error_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(error_test gtest_main)
        gtest_discover_tests(error_test PROPERTIES LABELS "unit")

        # Unit tests - メッセージi18nカタログ（en/ja切替・フォールバック）
        add_executable(i18n_test
            tests/unit/i18n_test.cpp
            ${CM_I18N_SOURCES}
        )
        set_target_properties(i18n_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(i18n_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(i18n_test gtest_main)
        gtest_discover_tests(i18n_test PROPERTIES LABELS "unit")

        # Unit tests - 命名規則判定（check/lint --strict のL001）
        add_executable(naming_rules_test
            tests/unit/naming_rules_test.cpp
        )
        set_target_properties(naming_rules_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(naming_rules_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(naming_rules_test gtest_main)
        gtest_discover_tests(naming_rules_test PROPERTIES LABELS "unit")

        # Unit tests - 型キー可逆エンコーディング（単相化の型同一性。C7/C8/C9）
        add_executable(typekey_test
            tests/unit/typekey_test.cpp
            src/internal/syntax/ast/typekey.cpp
        )
        set_target_properties(typekey_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(typekey_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(typekey_test gtest_main)
        gtest_discover_tests(typekey_test PROPERTIES LABELS "unit")

        # Regression tests - derive/withソース展開（合成ソースのスナップショット検証）
        add_executable(derive_expansion_test
            tests/regression/derive_expansion_test.cpp
            src/internal/macro/derive.cpp
            ${CM_LEXER_SOURCES}
            ${CM_AST_SOURCES}
            ${CM_PARSER_SOURCES}
            ${CM_I18N_SOURCES}
        )
        set_target_properties(derive_expansion_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(derive_expansion_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(derive_expansion_test gtest_main)
        gtest_discover_tests(derive_expansion_test PROPERTIES LABELS "regression")

        # Unit tests - レイアウト問い合わせAPI（layout-query-unification。スライス格納/配列実ストライドの2意味論）
        add_executable(layout_test
            tests/unit/layout_test.cpp
        )
        set_target_properties(layout_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(layout_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(layout_test gtest_main)
        gtest_discover_tests(layout_test PROPERTIES LABELS "unit")

        # Unit tests - MIRモジュール分割（H14。モジュール名正規化・グローバル所有権・型定義の全配布）
        add_executable(mir_splitter_test
            tests/unit/mir_splitter_test.cpp
            src/internal/mir/mir_splitter.cpp
            src/internal/mir/nodes.cpp
            ${CM_AST_SOURCES}
        )
        set_target_properties(mir_splitter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(mir_splitter_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(mir_splitter_test gtest_main)
        gtest_discover_tests(mir_splitter_test PROPERTIES LABELS "unit")

        # Unit tests - MIR最適化パス
        # （手組みMIRに対して各パスの.cppを単体で検証。
        # フロントエンド・lowering非依存のため、パス実装+ノード+解析のみをリンクする）
        set(CM_MIR_PASS_SOURCES
            src/internal/mir/nodes.cpp
            ${CM_AST_SOURCES}
            src/internal/mir/analysis/dominators.cpp
            src/internal/mir/analysis/loop_analysis.cpp
            src/internal/mir/passes/core/base.cpp
            src/internal/mir/passes/convergence/manager.cpp
            src/internal/mir/passes/instrumentation/undefined.cpp
            src/internal/mir/passes/instrumentation/bounds.cpp
            src/internal/mir/passes/scalar/sccp.cpp
            src/internal/mir/passes/scalar/folding.cpp
            src/internal/mir/passes/scalar/propagation.cpp
            src/internal/mir/passes/cleanup/dce.cpp
            src/internal/mir/passes/cleanup/dse.cpp
            src/internal/mir/passes/cleanup/string_reassign_free.cpp
            src/internal/mir/passes/cleanup/simplify_cfg.cpp
            src/internal/mir/passes/cleanup/program_dce.cpp
            src/internal/mir/passes/interprocedural/inlining.cpp
            src/internal/mir/passes/interprocedural/tail_call_elimination.cpp
            src/internal/mir/passes/loop/licm.cpp
            src/internal/mir/passes/loop/const_unroll.cpp
            src/internal/mir/passes/redundancy/gvn.cpp
        )
        add_executable(mir_pass_test
            tests/unit/mir_pass_test.cpp
            ${CM_MIR_PASS_SOURCES}
        )
        set_target_properties(mir_pass_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
        target_include_directories(mir_pass_test BEFORE PRIVATE ${GTEST_INCLUDE_DIR})
        target_link_libraries(mir_pass_test gtest_main cm_frontend)
        gtest_discover_tests(mir_pass_test PROPERTIES LABELS "unit")

        # Integration tests - MIR Interpreter (disabled until MirBuilder is implemented)
        # add_executable(mir_interpreter_test
        #     tests/regression/mir_interpreter_test.cpp
        #     src/internal/syntax/lexer/token.cpp
        #     src/internal/syntax/parser/parser_stmt.cpp
        #     src/internal/syntax/parser/expr/*.cpp
        #     src/internal/syntax/parser/module/*.cpp
        # )
        # target_link_libraries(mir_interpreter_test GTest::gtest_main cm_frontend)
        # gtest_discover_tests(mir_interpreter_test PROPERTIES LABELS "unit")
endif()

# Installation
# install(TARGETS cm RUNTIME DESTINATION bin)
