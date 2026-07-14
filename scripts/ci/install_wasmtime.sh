#!/usr/bin/env bash
# wasmtimeのインストールと検証（CI/ローカル共用）
#
# インストール方法はOSごとに信頼性の高い経路を使う:
# - macOS: brew（install.shはGitHubリリースから取得するため、共有ランナーの
#   APIレート制限で失敗することがある。2026-07-15にこの失敗で391テストが
#   無検証スキップになる事故が発生）
# - Linux: 公式install.sh（bytecodealliance/actions/wasmtime/setup@v1 も
#   レート制限でHTMLエラーを返すことがあるため直接実行）
#
# 検証: インストール後にwasmtimeが見つからない場合、nodeがあれば警告を出して
# 継続する（テストランナーはnodeのWASIラッパーへフォールバックし、欠落時は
# SKIPではなくFAILになるため無検証の緑にはならない）。nodeも無ければ失敗
set -euo pipefail

GH_PATH="${GITHUB_PATH:-/dev/null}"

case "$(uname -s)" in
    Darwin)
        if ! command -v wasmtime >/dev/null 2>&1; then
            brew install wasmtime || {
                echo "::warning::brew install wasmtime に失敗。公式install.shを試行します"
                curl https://wasmtime.dev/install.sh -sSf | bash || true
                echo "$HOME/.wasmtime/bin" >> "$GH_PATH"
                export PATH="$HOME/.wasmtime/bin:$PATH"
            }
        fi
        ;;
    Linux)
        if ! command -v wasmtime >/dev/null 2>&1; then
            curl https://wasmtime.dev/install.sh -sSf | bash || true
            echo "$HOME/.wasmtime/bin" >> "$GH_PATH"
            export PATH="$HOME/.wasmtime/bin:$PATH"
        fi
        ;;
esac

# 検証（インストール失敗の静かな素通りを防ぐ）
if command -v wasmtime >/dev/null 2>&1; then
    wasmtime --version
    echo "✅ wasmtime インストール・検証完了"
elif command -v node >/dev/null 2>&1; then
    echo "::warning::wasmtimeをインストールできませんでした。テストはnodeのWASIラッパーで実行されます"
    node --version
else
    echo "::error::wasmtimeもnodeも利用できません（WASMテストを実行できません）"
    exit 1
fi
