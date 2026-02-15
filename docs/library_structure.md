# Cm ライブラリ構成

## 概要

Cmのライブラリは `libs/` 配下に4つの名前空間で分類され、各バックエンドで使えるライブラリが明確化されています。

## ディレクトリ構造

```
Cm/libs/
├── std/                    # 全バックエンド共通
│   ├── core/               # 型エイリアス, ユーティリティ
│   ├── math/               # 整数数学関数
│   ├── collections/        # Vector, HashMap, Queue
│   ├── iter/               # イテレータ
│   ├── mem/                # メモリ管理
│   └── io/console/         # print/println
│
├── native/                 # JIT/LLVM Native専用
│   ├── math/               # 三角関数等 (libm依存)
│   ├── io/                 # ファイルI/O, ストリーム
│   ├── sync/               # 同期 + sync_runtime.cpp
│   ├── thread/             # スレッド + thread_runtime.cpp
│   ├── net/                # ネットワーク + net_runtime.cpp
│   ├── http/               # HTTP + http_runtime.cpp
│   └── gpu/                # GPU + gpu_runtime.mm
│
├── wasm/                   # WASM専用（将来拡張）
└── web/                    # Web/JS専用（将来拡張）
```

## 名前空間一覧

| 名前空間 | 対応バックエンド | 説明 |
|---------|---------------|------|
| `std` | 全バックエンド | プラットフォーム非依存のコア機能 |
| `native` | JIT, LLVM Native | OS依存機能（ファイルI/O、スレッド等） |
| `wasm` | WASM | WASM固有API（将来拡張） |
| `web` | JS/Web | Web固有API（将来拡張） |

## `std` - 全バックエンド共通

```
import std::io::println;           // 基本出力
import std::io::input;             // 基本入力
import std::core::{min, max};      // ユーティリティ
import std::math::{gcd, factorial}; // 整数数学関数
import std::collections::Vector;    // コレクション
import std::iter::Range;           // イテレータ
import std::mem::{alloc, size_of}; // メモリ管理
```

### モジュール一覧

- **std::io** - 基本入出力 (print, println, eprint, eprintln, input)
- **std::core** - 型エイリアス, min/max/clamp/abs, panic
- **std::math** - 整数数学関数 (gcd, lcm, pow, factorial, fibonacci, is_prime, abs)
- **std::collections** - Vector, HashMap, Queue
- **std::iter** - イテレータ, Range, アダプタ
- **std::mem** - メモリ管理 (alloc, dealloc, size_of, type_name, align_of)

## `native` - JIT/LLVM Native専用

```
import native::math::{sin, cos, sqrt, PI};  // 三角関数等
import native::io::{read_file, write_file};  // ファイルI/O
import native::sync::{Mutex, AtomicInt};     // 同期プリミティブ
import native::thread::{spawn, join};        // スレッド
import native::net::*;                       // ネットワーク
import native::http::*;                      // HTTP
import native::gpu::*;                       // GPU (Metal)
```

### モジュール一覧

- **native::math** - 三角関数, 対数, 冪乗, 丸め, 定数 (PI, E, INFINITY)
- **native::io** - ファイルI/O, ストリーム, バッファ付きI/O, エラー型
- **native::sync** - Mutex, RwLock, Atomic, Channel + C++ランタイム
- **native::thread** - spawn, join, sleep_ms + C++ランタイム
- **native::net** - TCP通信, イベント多重化
- **native::http** - HTTPクライアント (REST API)
- **native::gpu** - Metal GPU計算
