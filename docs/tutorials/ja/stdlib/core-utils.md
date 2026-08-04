---
title: std::core
---

# std::core / std::debug — コアユーティリティ

汎用関数、型エイリアス、アサーション・パニック機能を提供する基盤モジュールです。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 汎用関数（ジェネリック）

```cm
import std::core::{min, max, clamp, abs};

int a = min(3, 7);           // 3
int b = max(3, 7);           // 7
int c = clamp(15, 0, 10);    // 10
int d = abs(-42);            // 42
```

| 関数 | 型 | 説明 |
|------|-----|------|
| `min<T>(a, b)` | ジェネリック | 小さい方を返す |
| `max<T>(a, b)` | ジェネリック | 大きい方を返す |
| `clamp<T>(val, min, max)` | ジェネリック | 値を範囲内に制限 |
| `abs(x)` | int/long/float/double | 絶対値 |

---

## 型エイリアス

Rust風の固定サイズ型名が使用できます。

```cm
import std::core::{i32, f64, u8};

i32 x = 42;
f64 pi = 3.14;
u8 byte = 255;
```

| エイリアス | 実体 | サイズ |
|-----------|------|--------|
| `i8` / `u8` | `tiny` / `utiny` | 1バイト |
| `i16` / `u16` | `short` / `ushort` | 2バイト |
| `i32` / `u32` | `int` / `uint` | 4バイト |
| `i64` / `u64` | `long` / `ulong` | 8バイト |
| `f32` / `f64` | `float` / `double` | 4/8バイト |
| `usize` / `isize` | `uint` / `int` | プラットフォーム依存 |

---

## panic

プログラムを異常終了させます。

```cm
import std::core::panic;

panic("unreachable code");  // メッセージを出力して終了
```

---

## std::debug — アサーション（v0.17.0で追加）

```cm
import std::debug::{assert, assert_eq, assert_ne, panic};

int main() {
    assert(1 + 1 == 2, "math is broken");
    assert_eq(2 + 3, 5);       // 一致しなければ期待値・実測値を表示して異常終了
    assert_ne("abc", "abd");
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `assert(cond, msg)` | 条件が偽ならメッセージを表示して異常終了 |
| `assert_eq<T: Eq>(left, right)` | 不一致なら両方の値を表示して異常終了 |
| `assert_ne<T: Eq>(left, right)` | 一致なら両方の値を表示して異常終了 |
| `panic(msg)` | メッセージを表示して即座に異常終了 |

`assert_eq` / `assert_ne` は `<T: Eq>` ジェネリックで、プリミティブ・`string`・`with Eq` 構造体に使えます。
成功時は何も出力しません（`#[test]` 関数や回帰テストの検証に向きます）。

---

**関連:** [入出力](io.html) · [メモリ管理](mem.html) · [数学関数](math.html)

---

<!-- nav -->
← 前: [std::math — 数学関数](math.html) ｜ [目次](index.html) ｜ 次: [native::http - HTTP通信ライブラリ](http.html) →
