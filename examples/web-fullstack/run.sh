#!/bin/bash
# Cm フルスタックWebサンプルをTypeScriptバックエンドで実行する
set -e
cd "$(dirname "$0")"
CM="${CM:-cm}"
echo "=== Running Cm full-stack web app (React SSR + Express + Postgres) ==="
NODE_PATH="$PWD/vendor" "$CM" run --target=ts src/app.cm
