#!/bin/bash
# Cmだけで書いたフルスタックWebアプリをTypeScriptバックエンドで実行する
set -e
cd "$(dirname "$0")"
CM="${CM:-cm}"
echo "=== Running Cm full-stack web app (all logic in Cm; FFI only for pg/express) ==="
NODE_PATH="$PWD/vendor" "$CM" run --target=ts src/server.cm
