# Cm言語 インクリメンタルコンパイル設計

## 概要

Rustのような高速なインクリメンタルコンパイルを実現し、開発体験を向上させる。

## 設計方針

1. **モジュール単位のコンパイル**: 各モジュールを独立してコンパイル
2. **依存グラフ管理**: 変更の影響範囲を最小化
3. **キャッシュ活用**: MIR/オブジェクトファイルをキャッシュ
4. **並列コンパイル**: 独立したモジュールを並列処理
5. **FFI最適化**: ホットリロード可能な動的ライブラリ

## アーキテクチャ

```
┌─────────────────┐
│  Source (.cm)   │
└────────┬────────┘
         │ hash/timestamp チェック
         ▼
┌─────────────────┐
│  Cache Check    │──→ キャッシュヒット → 再利用
└────────┬────────┘
         │ キャッシュミス
         ▼
┌─────────────────┐
│  Parse → AST    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  HIR → MIR      │──→ MIRキャッシュ (.mir)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Target Generate │
├─────────┬───────┤
│   Rust  │  LLVM │
└─────────┴───────┘
         │
         ▼
┌─────────────────┐
│  Object Cache   │──→ オブジェクトキャッシュ (.o/.rlib)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Link        │
└─────────────────┘
```

## キャッシュ構造

```
.cm-cache/
├── fingerprints/     # ファイルハッシュ
│   └── <module-hash>.json
├── mir/              # MIRキャッシュ
│   └── <module-hash>.mir
├── rust/             # Rustコードキャッシュ
│   └── <module-hash>.rs
├── objects/          # オブジェクトファイル
│   └── <module-hash>.o
└── deps/             # 依存グラフ
    └── deps.json
```

## Rustトランスパイラ with FFI

### 1. 分割コンパイル戦略

```rust
// 各Cmモジュール → 独立したRustクレート
// std_io.cm → std_io.rs

#[no_mangle]
pub extern "C" fn cm_std_io_print(s: *const c_char) {
    let s = unsafe { CStr::from_ptr(s).to_string_lossy() };
    print!("{}", s);
}

// main.cm → main.rs
extern "C" {
    fn cm_std_io_print(s: *const c_char);
}

fn main() {
    let msg = CString::new("Hello").unwrap();
    unsafe { cm_std_io_print(msg.as_ptr()) };
}
```

### 2. 動的ライブラリによるホットリロード

```toml
# Cargo.toml for module
[lib]
name = "cm_std_io"
crate-type = ["cdylib"]  # 動的ライブラリ

[profile.dev]
opt-level = 0      # デバッグビルドは最適化なし
lto = false        # LTOなしで高速化
incremental = true # インクリメンタルコンパイル
```

### 3. ビルドシステム統合

```rust
// build.rs - Cargoビルドスクリプト
use std::process::Command;

fn main() {
    // 変更されたモジュールのみ再コンパイル
    let changed_modules = detect_changes();

    for module in changed_modules {
        compile_module(&module);
    }

    // 並列ビルド
    rayon::scope(|s| {
        for module in independent_modules {
            s.spawn(move |_| {
                cargo_build(&module);
            });
        }
    });
}
```

## 依存グラフ管理

```json
{
  "modules": {
    "main": {
      "path": "main.cm",
      "hash": "abc123...",
      "imports": ["std.io", "std.collections"],
      "exports": [],
      "last_compiled": "2025-01-01T00:00:00Z"
    },
    "std.io": {
      "path": "std/io.cm",
      "hash": "def456...",
      "imports": ["std.core"],
      "exports": ["print", "println"],
      "last_compiled": "2025-01-01T00:00:00Z"
    }
  },
  "build_order": ["std.core", "std.io", "main"]
}
```

## インクリメンタルコンパイルアルゴリズム

```rust
fn incremental_compile(changed_file: &Path) -> Result<()> {
    // 1. 変更ファイルの特定
    let module = identify_module(changed_file)?;

    // 2. 影響を受けるモジュールの特定
    let affected = find_affected_modules(&module)?;

    // 3. 再コンパイル順序の決定
    let build_order = topological_sort(affected)?;

    // 4. 並列可能なグループに分割
    let parallel_groups = group_by_dependencies(build_order)?;

    // 5. グループごとに並列コンパイル
    for group in parallel_groups {
        compile_parallel(group)?;
    }

    // 6. リンク（必要な場合のみ）
    if needs_relink(&affected) {
        link_executable()?;
    }

    Ok(())
}
```

## 最適化テクニック

### 1. ヘッダーファイル分離（Rust）

```rust
// cm_std_io.h - C FFIヘッダー
#ifndef CM_STD_IO_H
#define CM_STD_IO_H

void cm_std_io_print(const char* s);
void cm_std_io_println(const char* s);

#endif
```

### 2. プリコンパイル済みMIR

```rust
// MIRを事前にシリアライズ
let mir = compile_to_mir(&ast)?;
let serialized = bincode::serialize(&mir)?;
cache.store(&module_hash, &serialized)?;
```

### 3. 並列コード生成

```rust
// 関数単位で並列化
mir.functions.par_iter().map(|func| {
    generate_rust_function(func)
}).collect()
```

### 4. 増分リンク

```rust
// 変更された.oファイルのみ再リンク
let changed_objects = find_changed_objects()?;
if !changed_objects.is_empty() {
    incremental_link(changed_objects)?;
}
```

## パフォーマンス目標

| 操作 | 目標時間 |
|-----|---------|
| 単一ファイル変更の再コンパイル | < 100ms |
| 1000ファイルプロジェクトのフルビルド | < 10s |
| 型チェックのみ | < 50ms |
| ホットリロード | < 200ms |

## 実装フェーズ

### Phase 1: 基本的なキャッシュ
- [ ] ファイルハッシュによる変更検出
- [ ] MIRキャッシュ
- [ ] 単純な依存グラフ

### Phase 2: Rustトランスパイラ
- [ ] モジュール→Rustクレート変換
- [ ] FFIバインディング生成
- [ ] Cargoプロジェクト生成

### Phase 3: 並列化
- [ ] 並列パース
- [ ] 並列型チェック
- [ ] 並列コード生成

### Phase 4: 最適化
- [ ] 増分型推論
- [ ] 部分的MIR再生成
- [ ] プロファイルガイド最適化

## コマンドラインインターフェース

```bash
# インクリメンタルビルド（デフォルト）
cm build

# フルリビルド
cm build --fresh

# ウォッチモード（ファイル変更を監視）
cm watch

# 並列度指定
cm build -j 8

# キャッシュクリア
cm clean

# キャッシュ統計
cm cache stats
```

## 設定ファイル

```toml
# cm.toml
[build]
incremental = true
parallel = true
cache_dir = ".cm-cache"
target = "rust"  # or "native"

[rust]
edition = "2021"
opt_level = 2
lto = "thin"

[cache]
max_size = "1GB"
ttl = "7days"
compression = true
```