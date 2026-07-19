#!/bin/bash
# unified_test_runner.sh から source されるスイート定義モジュール。
# テストスイートのカテゴリ展開（expand_suite）とヘルプ表示（usage）を提供する。

# テストスイート定義
# 各スイートはカテゴリのグループを定義する（common/ 内のカテゴリ名）
expand_suite() {
    local suite="$1"
    case "$suite" in
        core)
            echo "basic types control_flow functions function_ptr loops literal auto const const_interpolation casting errors type_checking"
            ;;
        syntax)
            echo "array arrays array_higher_order dynamic_array slice string formatting enum match structs impl interface lambda chaining result must defer pointer ownership generics iterator"
            ;;
        stdlib)
            echo "collections io std allocator memory intrinsics preprocessor"
            ;;
        modules)
            echo "modules advanced_modules macro advanced"
            ;;
        platform)
            echo "gpu"
            ;;
        runtime)
            echo "file_io fs net thread sync"
            ;;
        all)
            echo ""
            ;;
        *)
            echo "Error: Unknown suite '$suite'" >&2
            echo "Valid suites: core, syntax, stdlib, modules, platform, runtime, all" >&2
            exit 1
            ;;
    esac
}

# ヘルプメッセージ
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -b, --backend <backend>    Test backend: interpreter|jit|typescript|rust|cpp|llvm|llvm-wasm|llvm-uefi|llvm-baremetal|js|sv (default: jit)"
    echo "  -c, --category <category>  Test categories (comma-separated, default: auto-detect from directories)"
    echo "  -s, --suite <suite>        Test suite: core|syntax|stdlib|modules|platform|runtime|all (default: all)"
    echo "  -v, --verbose              Show detailed output"
    echo "  -p, --parallel             Run tests in parallel (experimental)"
    echo "  -t, --timeout <seconds>    Test timeout in seconds (default: 5)"
    echo "  -n, --no-cache             キャッシュを無効化してテスト実行"
    echo "  --clean-cache              テスト前にキャッシュを削除"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Suites:"
    echo "  core     - 言語基盤テスト（全ターゲット共通）"
    echo "  syntax   - 構文機能テスト（配列・構造体・ジェネリクス等）"
    echo "  stdlib   - 標準ライブラリテスト"
    echo "  modules  - モジュール・マクロテスト"
    echo "  platform - ターゲット固有テスト（UEFI・ベアメタル・ASM等）"
    echo "  runtime  - OS依存ランタイムテスト（ファイルI/O・ネット・スレッド等）"
    echo "  all      - 全テスト（デフォルト）"
    echo ""
    echo "Categories are auto-detected from directories in tests/"
    exit 0
}
