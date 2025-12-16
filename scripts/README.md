# CI環境テストスクリプト

GitHub ActionsのCI環境（Ubuntu 24.04 + LLVM 18）をDockerで再現し、ローカルで動作確認するためのスクリプト群です。

## 前提条件

- Docker がインストールされていること
- Docker デーモンが起動していること

## スクリプト一覧

### 1. `test-in-docker.sh`
**完全なCI環境テストを実行**

```bash
./scripts/test-in-docker.sh
```

実行内容：
1. Dockerイメージのビルド（Ubuntu 24.04 + LLVM 18）
2. CMake設定
3. プロジェクトビルド
4. RPATHの確認
5. 全テストスイートの実行
   - Unit Tests (C++ Google Test)
   - Interpreter Tests
   - LLVM Native Tests
   - LLVM WASM Tests

### 2. `docker-shell.sh`
**Docker CI環境で対話シェルを起動**

```bash
./scripts/docker-shell.sh
```

対話シェルでできること：
```bash
# ビルド
cmake -B build -DCM_USE_LLVM=ON
cmake --build build -j$(nproc)

# 個別テスト実行
./build/bin/lexer_test
make tip
make tlp

# RPATH確認
ldd ./cm
readelf -d ./cm | grep RPATH

# デバッグ
gdb ./cm
```

## Dockerイメージ詳細

### イメージ名
`cm-ci-test`

### 環境仕様
- **OS**: Ubuntu 24.04 LTS
- **LLVM**: 18
- **CMake**: 最新版（apt）
- **Google Test**: libgtest-dev
- **Wasmtime**: 最新版

### 環境変数
```
PATH=/usr/lib/llvm-18/bin:$PATH
LLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
```

## 使用例

### CI環境でビルドのみテスト
```bash
docker run --rm \
    -v "$PWD:/workspace" \
    -w /workspace \
    cm-ci-test \
    bash -c "cmake -B build -DCM_USE_LLVM=ON && cmake --build build"
```

### 特定のテストのみ実行
```bash
docker run --rm \
    -v "$PWD:/workspace" \
    -w /workspace \
    cm-ci-test \
    bash -c "make tip"  # インタプリタテストのみ
```

### イメージの削除
```bash
docker rmi cm-ci-test
```

## トラブルシューティング

### イメージビルドが失敗する
```bash
# キャッシュをクリアして再ビルド
docker build --no-cache -f Dockerfile.ci -t cm-ci-test .
```

### コンテナ内でビルドが失敗する
```bash
# 対話シェルで調査
./scripts/docker-shell.sh

# コンテナ内で
cmake -B build -DCM_USE_LLVM=ON -DCMAKE_VERBOSE_MAKEFILE=ON
cmake --build build --verbose
```

### RPATHが設定されているか確認
```bash
docker run --rm -v "$PWD:/workspace" -w /workspace cm-ci-test \
    readelf -d ./cm | grep RPATH
```

期待される出力：
```
 0x000000000000001d (RUNPATH)            Library runpath: [/usr/lib/llvm-18/lib]
```

## CI環境との違い

### 同じ点
- OS: Ubuntu 24.04
- LLVM: 18
- ビルドツール: CMake, make, clang
- テストツール: Google Test, wasmtime

### 異なる点
- CI: GitHub Actions Runner
- Docker: ローカルコンテナ
- パフォーマンス: ローカルの方が高速

## 参考

- Dockerfile: `Dockerfile.ci`
- CI設定: `.github/workflows/ci.yml`
- プロジェクトルート: `/workspace` (コンテナ内)
