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

## std::iter — 範囲とイテレータ

`std::iter` は範囲（`Range`）とイテレータを提供します（v0.17.0でモジュール自体のコンパイル不能を修正し、for-in対応を追加しました）:

```cm
import std::io::println;
import std::iter::*;

int main() {
    // range(start, end, step = 1): 半開区間 [start, end)
    for (int v in range(1, 4)) {
        println("{v}");            // 1 2 3
    }
    // range_to(end): 0からendまで（range(0, end)の別名）
    for (int x in range_to(3)) {
        println("{x}");            // 0 1 2
    }
    const Range r = range(2, 11, 3);
    println("{len(r)}");           // 3（要素数）
    println("{at(r, 1)}");         // 5（n番目の値）
    return 0;
}
```

| API | 説明 |
|------|------|
| `range(start, end, step = 1)` | 半開区間のRange（stepは省略可） |
| `range_to(end)` | `range(0, end)` の別名 |
| `len(r)` / `is_empty(r)` / `at(r, n)` | 要素数・空判定・n番目の値 |
| `Range.iter()` | for-inプロトコル対応イテレータ |
| `IntArrayIterator` | ポインタベースのint配列イテレータ |

## std::core::time — 時刻とタイマー

`std::core::time` は時刻取得とタイマーを提供します（旧`std.core.async`はasyncが予約語のためimport不能でした。v0.17.0で改名）:

| API | 説明 |
|------|------|
| `now_ms()` | 現在時刻（ミリ秒） |
| `sleep_ms(ms)` / `sleep_sec(sec)` | ブロッキングスリープ |
| `Timer::start()` / `t.elapsed_ms()` / `t.reset()` | 経過時間計測 |

---

**関連:** [入出力](io.html) · [メモリ管理](mem.html) · [数学関数](math.html)

---

<!-- nav -->
← 前: [std::math — 数学関数](math.html) ｜ [目次](index.html) ｜ 次: [native::http - HTTP通信ライブラリ](http.html) →
