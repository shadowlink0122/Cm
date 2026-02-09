---
title: 標準ライブラリ
parent: Tutorials
---

# Cm 標準ライブラリ (Native向け)

Cm標準ライブラリはNative (LLVM) バックエンド向けの機能を提供します。  
すべてのモジュールは C/C++/Objective-C++ のバッキングコードで実装されています。

> **注意:** WASM/JSバックエンドでは使用できません。

**最終更新:** 2026-02-08

---

## 基盤モジュール

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::io` | 入出力 (println, input, ファイルI/O) | [入出力](io.html) |
| `std::mem` | メモリ管理 (alloc, size_of, Allocator) | [メモリ管理](mem.html) |
| `std::math` | 数学関数 (sin, sqrt, PI, gcd等) | [数学関数](math.html) |
| `std::core` | ユーティリティ (min, max, clamp, 型エイリアス) | [コア](core-utils.html) |

---

## ネットワーク

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::http` | HTTP/HTTPSクライアント・サーバ | [HTTP通信](http.html) |
| `std::net` | TCP/UDP/DNS/poll | [TCP/UDP通信](network/tcp.html) |

---

## 並行処理

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::thread` | スレッド生成・join・sleep | [スレッド](concurrency/thread.html) |
| `std::sync::mutex` | Mutex・RwLock | [Mutex](concurrency/mutex.html) |
| `std::sync::channel` | Go風バウンデッドチャネル | [Channel](concurrency/channel.html) |
| `std::sync::atomic` | アトミック操作 | [Atomic](concurrency/atomic.html) |

→ [並行処理の概要と使い分け](concurrency/)

---

## GPU

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::gpu` | Apple Metal GPGPU | [GPU計算](gpu.html) |

---

## コレクション

| モジュール | 説明 | ドキュメント |
|-----------|------|------------|
| `std::collections::vector` | `Vector<T>` 動的配列 | [Vector](collections/vector.html) |
| `std::collections::queue` | `Queue<T>` FIFO | [Queue](collections/queue.html) |
| `std::collections::hashmap` | `HashMap<K,V>` ハッシュマップ | [HashMap](collections/hashmap.html) |

---

## 標準ライブラリの拡張

独自のNativeモジュールを C/C++/Objective-C++ で作成する方法:

→ **[拡張ガイド](extending.html)** — extern, use libc, ObjC++, インラインアセンブリ

---

## 関連

- [FFI (C言語連携)](../advanced/ffi.html)
- [LLVMバックエンド](../compiler/llvm.html)
- [インラインアセンブリ](../advanced/ffi.html)
