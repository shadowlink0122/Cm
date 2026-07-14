#!/usr/bin/env bash
# コンパイラのCMake configure + ビルド（CI/ローカル共用）
# 使用法: build_compiler.sh <build_type> <arch> [--testing]
set -euo pipefail

BUILD_TYPE="${1:?build_type (Debug/Release) を指定してください}"
ARCH="${2:?arch (x86_64/arm64) を指定してください}"
TESTING_FLAG=""
[ "${3:-}" = "--testing" ] && TESTING_FLAG="-DBUILD_TESTING=ON"

CMAKE_EXTRA_FLAGS=""
if [ "$(uname -s)" = "Darwin" ]; then
    CMAKE_EXTRA_FLAGS="-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)"
fi

cmake -B build \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCM_USE_LLVM=ON \
    -DCM_TARGET_ARCH="${ARCH}" \
    ${TESTING_FLAG} \
    ${CMAKE_EXTRA_FLAGS}
cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
echo "✅ コンパイラビルド完了 (${BUILD_TYPE}, ${ARCH})"
