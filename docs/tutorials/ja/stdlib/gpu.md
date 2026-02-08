---
title: GPU計算 (Metal)
---

# std::gpu - GPU計算 (Apple Metal)

Cm標準ライブラリのGPU計算モジュール。Apple Metal Frameworkを使用したGPGPU演算を提供します。

> **対応バックエンド:** Native (LLVM) のみ。macOS (Apple Silicon M1以上) 専用。

**最終更新:** 2026-02-08

---

## 概要

`std::gpu` はMetal Compute Shaderを使ってGPU上で大量のデータを並列処理するためのAPIです。

1. **デバイス** を作成
2. **バッファ** にデータを転送
3. **カーネル** (Metal Shading Language) をコンパイル
4. **dispatch** で実行
5. **結果** をバッファから読み出し

---

## 基本的な使い方

### ベクトル加算の例

```cm
import std::gpu::device_create;
import std::gpu::device_name;
import std::gpu::device_destroy;
import std::gpu::buffer_create_ints;
import std::gpu::buffer_write_ints;
import std::gpu::buffer_read_ints;
import std::gpu::buffer_destroy;
import std::gpu::kernel_create;
import std::gpu::kernel_destroy;
import std::gpu::dispatch;
import std::io::println;

int main() {
    // 1. GPUデバイス取得
    long dev = device_create();
    println("GPU: {device_name(dev)}");

    // 2. データ準備
    int N = 4;
    int a[4] = {1, 2, 3, 4};
    int b[4] = {10, 20, 30, 40};
    int c[4];

    // 3. GPUバッファ作成 & データ転送
    long buf_a = buffer_create_ints(dev, N as long);
    long buf_b = buffer_create_ints(dev, N as long);
    long buf_out = buffer_create_ints(dev, N as long);
    buffer_write_ints(buf_a, a as int*, N as long);
    buffer_write_ints(buf_b, b as int*, N as long);

    // 4. Metalカーネル定義 & コンパイル
    string kernel_src = "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "kernel void add(device const int* a [[buffer(0)]],\n"
        "                device const int* b [[buffer(1)]],\n"
        "                device int* out     [[buffer(2)]],\n"
        "                uint id [[thread_position_in_grid]]) {\n"
        "    out[id] = a[id] + b[id];\n"
        "}\n";
    long kernel = kernel_create(dev, kernel_src, "add");

    // 5. GPU上で実行
    dispatch(kernel, buf_a, buf_b, buf_out, N as long);

    // 6. 結果読み出し
    buffer_read_ints(buf_out, c as int*, N as long);
    for (int i = 0; i < N; i++) {
        println("c[{i}] = {c[i]}");  // 11, 22, 33, 44
    }

    // 7. リソース解放
    kernel_destroy(kernel);
    buffer_destroy(buf_a);
    buffer_destroy(buf_b);
    buffer_destroy(buf_out);
    device_destroy(dev);
    return 0;
}
```

---

## API一覧

### デバイス管理

| 関数 | 説明 |
|------|------|
| `device_create()` | GPUデバイス取得 (ハンドルを返す) |
| `device_name(dev)` | デバイス名取得 |
| `device_max_threads(dev)` | 最大スレッド数 |
| `device_memory_size(dev)` | GPUメモリサイズ |
| `device_destroy(dev)` | デバイス解放 |
| `is_available()` | GPU利用可能チェック |

### バッファ管理

| 関数 | 説明 |
|------|------|
| `buffer_create(dev, size)` | 汎用バッファ作成 |
| `buffer_create_ints(dev, count)` | int32バッファ作成 |
| `buffer_create_floats(dev, count)` | floatバッファ作成 |
| `buffer_write_ints(buf, data, count)` | int32データ書き込み |
| `buffer_read_ints(buf, data, count)` | int32データ読み出し |
| `buffer_write_floats(buf, data, count)` | floatデータ書き込み |
| `buffer_read_floats(buf, data, count)` | floatデータ読み出し |
| `buffer_write_longs(buf, data, count)` | longデータ書き込み |
| `buffer_read_longs(buf, data, count)` | longデータ読み出し |
| `buffer_size(buf)` | バッファサイズ取得 |
| `buffer_copy(src, dst, size)` | バッファ間コピー |
| `buffer_zero(buf)` | バッファゼロクリア |
| `buffer_destroy(buf)` | バッファ解放 |

### カーネル管理

| 関数 | 説明 |
|------|------|
| `kernel_create(dev, source, func_name)` | MSLソースからカーネルコンパイル |
| `kernel_destroy(kernel)` | カーネル解放 |

### 実行 (dispatch)

| 関数 | 説明 |
|------|------|
| `dispatch(kernel, a, b, out, count)` | 3バッファ版 (入力2 + 出力1) |
| `dispatch_1(kernel, buf, count)` | 1バッファ版 (in-place) |
| `dispatch_2(kernel, in, out, count)` | 2バッファ版 (入力1 + 出力1) |
| `dispatch_n(kernel, bufs, buf_count, count)` | Nバッファ版 |
| `dispatch_grid(kernel, a, b, out, grid_x, group_x)` | グリッドサイズ指定版 |
| `dispatch_async(kernel, a, b, out, count)` | 非同期実行 → コマンドハンドル |
| `command_wait(cmd)` | 非同期完了待機 |
| `command_is_completed(cmd)` | 非同期完了チェック |

### Float変換ヘルパー

| 関数 | 説明 |
|------|------|
| `float_to_bits(double)` | IEEE754 float → long |
| `bits_to_float(long)` | long → double |

---

## Metal Shading Languageの基本

カーネルはMetal Shading Language (MSL) で記述します。

```metal
#include <metal_stdlib>
using namespace metal;

// buffer(0), buffer(1), ... はdispatchに渡したバッファ順
kernel void my_kernel(
    device const int* input [[buffer(0)]],
    device int* output      [[buffer(1)]],
    uint id [[thread_position_in_grid]]
) {
    output[id] = input[id] * 2;
}
```

---

## 注意事項

- Apple Silicon (M1以上) のmacOSでのみ動作します
- `float` へのアクセスには `float_to_bits` / `bits_to_float` 変換が必要です
- カーネルソースは実行時にコンパイルされます

---

**最終更新:** 2026-02-08
