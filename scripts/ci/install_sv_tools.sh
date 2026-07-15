#!/usr/bin/env bash
# SVシミュレーションツール（iverilog）のインストール
# macOSでのインストール失敗は警告に留める（SVテストはスキップされる）
set -euo pipefail

GH_OUTPUT="${GITHUB_OUTPUT:-/dev/null}"
case "$(uname -s)" in
    Linux)
        sudo apt-get install -y iverilog
        echo "sv_tools_available=true" >> "$GH_OUTPUT"
        ;;
    Darwin)
        if ! brew install icarus-verilog; then
            echo "::warning::iverilog installation failed on macOS, SV tests will be skipped"
            echo "sv_tools_available=false" >> "$GH_OUTPUT"
        else
            echo "sv_tools_available=true" >> "$GH_OUTPUT"
        fi
        ;;
esac
