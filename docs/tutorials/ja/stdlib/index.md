---
title: 標準ライブラリ
parent: Tutorials
---

# Cm 標準ライブラリ (Native向け)

Cm標準ライブラリのモジュール群です。
ランタイム連携が必要なモジュール（io・mem・net・thread等）はNative (LLVM) 中心で、純Cm実装のモジュール（json・path・bytes・collections・strings等）は全バックエンドで使えます。

> **注意:** 各ページ冒頭の「対応バックエンド」を確認してください（無印はNative中心）。

**最終更新:** 2026-02-08

---

## 基盤モジュール

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::io` | 入出力 (println, input) | [入出力](io.html) |
| `std::fs` | ファイルシステム操作 (read_file/write_file・Result API) | [入出力](io.html) |
| `std::mem` | メモリ管理 (alloc, size_of, Allocator, スマートポインタ, Arena) | [メモリ管理](mem.html) |
| `std::math` | 数学関数 (sin, sqrt, PI, gcd等) | [数学関数](math.html) |
| `std::core` | ユーティリティ (min, max, clamp, 型エイリアス) | [コア](core-utils.html) |
| `std::env` / `std::process` | 環境変数・args・サブプロセス (Native/JIT) | [OS連携](os.html) |
| `std::path` / `std::bytes` | パス操作・バイト詰め (純Cm・全バックエンド) | [OS連携](os.html) |
| `std::json` | JSONパース・直列化 (純Cm・全バックエンド) | [JSON](json.html) |
| `std::debug` | assert / assert_eq / assert_ne / panic | [コア](core-utils.html) |
| `std::iter` | Range・range/range_to・for-in対応イテレータ | [コア](core-utils.html) |
| `std::core::time` | now_ms・sleep_ms・Timer（旧std.core.asyncから改名） | [コア](core-utils.html) |

---

## ネットワーク

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `native::http` | HTTP/HTTPSクライアント・サーバ | [HTTP通信](http.html) |
| `native::net` | TCP/UDP/DNS/poll | [TCP/UDP通信](network/tcp.html) |

---

## 並行処理

[並行処理の概要と使い分け](concurrency/index.html)

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `native::thread` | スレッド生成・join・sleep | [スレッド](concurrency/thread.html) |
| `native::sync::mutex` | Mutex・RwLock | [Mutex](concurrency/mutex.html) |
| `native::sync::channel` | Go風バウンデッドチャネル | [Channel](concurrency/channel.html) |
| `native::sync::atomic` | アトミック操作 | [Atomic](concurrency/atomic.html) |


---

## GPU

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `native::gpu` | Apple Metal GPGPU | [GPU計算](gpu.html) |

---

## コレクション

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::collections::vector` | `Vector<T>` 動的配列 | [Vector](collections/vector.html) |
| `std::collections::queue` | `Queue<T>` FIFO | [Queue](collections/queue.html) |
| `std::collections::hashmap` | `HashMap<K,V>` ハッシュマップ（getはOption返し） | [HashMap](collections/hashmap.html) |
| `std::collections::treemap` | `TreeMap<K,V>` 順序付きマップ（AVL木・O(log n)・remove/keys_in_order対応） | [TreeMap](collections/treemap.html) |
| `std::collections::treeset` / `hashset` | `TreeSet<T>` 順序付き集合・`HashSet<T>` ハッシュ集合（v0.17.2） | [TreeSet / HashSet](collections/sets.html) |
| `std::collections::strmap` / `strset` | `StringMap<V>` / `StringSet` 文字列キー・内容ハッシュ（v0.17.2） | [HashMap内のStringMap節](collections/hashmap.html) |
| `std::slices` | スライスのヒープソート sort / sort_by（v0.17.2） | [Vector内のソート節](collections/vector.html) |

---

## 文字列

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::strings::builder` | `StringBuilder` 可変文字列バッファ（償却O(1)追記） | [StringBuilder](strings/builder.html) |
| `std::strings::chars` / `parse` / `format` / `hash` / `intern` | 文字分類・数値解析・基数フォーマット・内容ハッシュ・interning（v0.17.2） | [文字分類・数値解析・フォーマット](strings/chars-parse.html) |
| 文字列組み込み | `len()`（コードポイント数）/ `byte_len()`（バイト数） | [文字列の長さ](strings/length.html) |

---

## 標準ライブラリの拡張

独自のNativeモジュールを C/C++/Objective-C++ で作成する方法:

→ **[拡張ガイド](extending.html)** — extern, use libc, ObjC++, インラインアセンブリ

---

## 関連

- [FFI (C言語連携)](../advanced/ffi.html)
- [LLVMバックエンド](../compiler/native/index.html)
- [インラインアセンブリ](../advanced/ffi.html)

---

<!-- nav -->
← 前: [スレッド (native::thread)](../advanced/thread.html) ｜ [目次](../index.html) ｜ 次: [std::io — 入出力](io.html) →
