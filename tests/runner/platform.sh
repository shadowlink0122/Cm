#!/bin/bash
# unified_test_runner.sh から source されるプラットフォーム判定モジュール。
# バックエンドとプラットフォームディレクトリの対応（get_platform_dirs）と //! platform: ディレクティブ判定（check_platform_directive）を提供する。

# バックエンドに応じたプラットフォームディレクトリの解決
# common/ は全バックエンドで実行
# llvm/ は llvm, jit で実行
# wasm/ は llvm-wasm で実行
# js/ は js で実行
# baremetal/ は llvm-baremetal で実行
# uefi/ は llvm-uefi で実行
# jit/ は jit で実行
get_platform_dirs() {
    local backend="$1"
    case "$backend" in
        interpreter)
            echo "common"
            ;;
        jit)
            echo "common llvm jit"
            ;;
        llvm)
            echo "common llvm"
            ;;
        llvm-wasm)
            echo "common wasm"
            ;;
        llvm-uefi)
            echo "uefi"
            ;;
        llvm-baremetal)
            echo "baremetal"
            ;;
        js)
            echo "common js"
            ;;
        sv)
            echo "sv"
            ;;
        *)
            echo "common"
            ;;
    esac
}

# プラットフォームディレクティブチェック
# //! platform: js|web  (or形式)
# //! platform: !native (not形式)
# 戻り値: 0=マッチ(実行可), 1=不一致(スキップ)
# 標準出力: マッチしなかった場合のスキップ理由
check_platform_directive() {
    local test_file="$1"
    local backend="$2"

    # ファイル先頭5行から //! platform: ディレクティブを検索（macOS互換）
    local directive
    directive=$(head -5 "$test_file" | grep '//! *platform:' | sed 's|.*//! *platform: *||' | head -1)

    # ディレクティブがなければマッチ扱い
    if [ -z "$directive" ]; then
        return 0
    fi

    # 空白を除去
    directive=$(echo "$directive" | tr -d '[:space:]')

    # バックエンド名をプラットフォーム名にマッピング
    local platform
    case "$backend" in
        llvm-baremetal) platform="baremetal" ;;
        llvm-uefi)      platform="uefi" ;;
        llvm-wasm)      platform="wasm" ;;
        llvm)           platform="native" ;;
        interpreter)    platform="native" ;;
        jit)            platform="native" ;;
        js)             platform="js" ;;
        *)              platform="$backend" ;;
    esac

    # NOT形式: !platform|platform2
    if [[ "$directive" == !* ]]; then
        # !を除去
        local negated="${directive#!}"
        # |で分割してチェック
        IFS='|' read -ra platforms <<< "$negated"
        for p in "${platforms[@]}"; do
            if [ "$platform" = "$p" ]; then
                echo "Platform directive excludes $platform"
                return 1
            fi
        done
        return 0
    fi

    # OR形式: platform|platform2
    IFS='|' read -ra platforms <<< "$directive"
    for p in "${platforms[@]}"; do
        if [ "$platform" = "$p" ]; then
            return 0
        fi
    done

    echo "Platform directive requires $directive (current: $platform)"
    return 1
}
