FROM ubuntu:24.04

# メタデータ
LABEL maintainer="Cm Language Project"
LABEL description="Cm Compiler Development Environment"

# 非対話モード
ENV DEBIAN_FRONTEND=noninteractive

# 基本ツール
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    wget \
    pkg-config \
    # GCC
    gcc-13 \
    g++-13 \
    # Clang
    clang-17 \
    clang-format-17 \
    clang-tidy-17 \
    lld-17 \
    # テスト・カバレッジ
    lcov \
    # その他
    python3 \
    && rm -rf /var/lib/apt/lists/*

# デフォルトコンパイラ設定
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-17 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-17 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-17 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-17 100

# 作業ディレクトリ
WORKDIR /workspace

# デフォルトコマンド
CMD ["bash"]
