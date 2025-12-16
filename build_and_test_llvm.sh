#!/bin/bash
# LLVM バックエンドのビルドとテストスクリプト

set -e

echo "=== Building Cm compiler with LLVM backend ==="

# ビルドディレクトリ作成
mkdir -p build

# CMake設定（LLVM有効）
cd build
cmake .. -DCM_USE_LLVM=ON -DCMAKE_BUILD_TYPE=Debug

# ビルド
make -j4

cd ..

echo ""
echo "=== Testing LLVM backend ==="

# テストプログラムをインタプリタで実行
echo "1. Running with interpreter:"
./cm run tests/llvm/test_basic.cm > .tmp/interpreter_output.txt 2>&1 || true
cat .tmp/interpreter_output.txt

echo ""
echo "2. Compiling with LLVM backend:"
./cm compile tests/llvm/test_basic.cm --backend=llvm --target=native -o .tmp/test_llvm --debug

echo ""
echo "3. Running LLVM-compiled binary:"
if [ -f .tmp/test_llvm ]; then
    .tmp/test_llvm > .tmp/llvm_output.txt 2>&1 || true
    cat .tmp/llvm_output.txt

    echo ""
    echo "4. Comparing outputs:"
    if diff .tmp/interpreter_output.txt .tmp/llvm_output.txt > /dev/null; then
        echo "✅ SUCCESS: LLVM output matches interpreter output!"
    else
        echo "❌ FAILURE: Outputs differ"
        echo "Interpreter output:"
        cat .tmp/interpreter_output.txt
        echo ""
        echo "LLVM output:"
        cat .tmp/llvm_output.txt
    fi
else
    echo "❌ FAILURE: LLVM compilation failed"
fi

# クリーンアップ
rm -f .tmp/interpreter_output.txt .tmp/llvm_output.txt .tmp/test_llvm