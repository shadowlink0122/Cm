# Cm言語 大規模ライブラリアーキテクチャ設計

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: アーキテクチャ設計
関連文書: 060_cm_macro_system_design.md, 061_pin_library_design.md

## エグゼクティブサマリー

Pinライブラリのような大規模で複雑な機能を持つライブラリを、適切に構造化し、保守・拡張可能にするためのアーキテクチャ設計を提案します。モジュールシステム、ビルドシステム、テスト戦略を含む包括的な設計です。

## 1. 大規模ライブラリの課題

### 1.1 現状の問題点

1. **モジュール間の依存関係が複雑**
2. **名前空間の衝突**
3. **ビルド時間の増大**
4. **テストの肥大化**
5. **ドキュメント管理の困難**
6. **バージョン管理の複雑化**

### 1.2 設計目標

- **モジュラリティ**: 独立性の高いモジュール構成
- **階層化**: 明確なレイヤー分離
- **拡張性**: 新機能追加の容易さ
- **保守性**: 理解しやすく修正しやすい
- **パフォーマンス**: ビルド時間と実行時性能の最適化

## 2. モジュールシステム強化

### 2.1 階層的モジュール構造

```
std/
├── core/           # 言語コア機能
│   ├── mem/        # メモリ管理
│   ├── ptr/        # ポインタ操作
│   └── marker/     # マーカートレイト
├── alloc/          # アロケータ
│   ├── global/     # グローバルアロケータ
│   └── custom/     # カスタムアロケータ
├── collections/    # データ構造
│   ├── vec/        # ベクター
│   ├── map/        # マップ
│   └── set/        # セット
├── pin/            # Pin（メモリ固定）
│   ├── core/       # コア機能
│   ├── macros/     # マクロ
│   └── async/      # 非同期統合
├── sync/           # 同期プリミティブ
│   ├── mutex/      # ミューテックス
│   ├── rwlock/     # 読み書きロック
│   └── atomic/     # アトミック操作
└── async/          # 非同期
    ├── future/     # Future
    ├── stream/     # Stream
    └── executor/   # 実行器
```

### 2.2 モジュール宣言の拡張

```cm
// std/pin/core/module.cm
module std::pin::core {
    // モジュールメタデータ
    #[version = "0.11.0"]
    #[author = "Cm Standard Library Team"]
    #[license = "MIT OR Apache-2.0"]

    // 依存関係の明示
    requires {
        std::core::mem   >= "0.10.0";
        std::core::ptr   >= "0.10.0";
        std::core::marker >= "0.10.0";
    }

    // 機能フラグ
    features {
        default = ["alloc"];
        alloc = [];        // アロケータ依存機能
        no_std = [];       // no_std環境対応
        unstable = [];     // 実験的機能
    }

    // 公開APIバージョニング
    api {
        #[since = "0.11.0"]
        export struct Pin;

        #[since = "0.11.0"]
        export interface Unpin;

        #[deprecated(since = "0.12.0", note = "Use Pin::new_unchecked")]
        export fn unsafe_pin;
    }
}
```

### 2.3 条件付きコンパイル

```cm
// 機能フラグによる条件付きコンパイル
#[cfg(feature = "alloc")]
export fn boxed_pin<T>(value: T) -> Pin<Box<T>> {
    Box::pin(value)
}

#[cfg(not(feature = "no_std"))]
import std::io::println;

#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
export fn platform_specific_optimization() {
    // Linux x86_64専用の最適化
}

#[cfg(any(debug, test))]
export fn debug_assertions() {
    // デバッグ/テスト時のみの検証
}
```

## 3. ビルドシステム設計

### 3.1 増分ビルド対応

```toml
# cm-build.toml
[package]
name = "std"
version = "0.11.0"

[build]
incremental = true
cache_dir = ".cm-cache"
parallel = true
jobs = 8

[profile.dev]
opt_level = 0
debug = true
incremental = true

[profile.release]
opt_level = 3
debug = false
lto = true
strip = true

[profile.test]
opt_level = 2
debug = true
sanitizers = ["address", "undefined"]
```

### 3.2 モジュール依存グラフ

```cm
// ビルドシステムが自動生成
// .cm-cache/deps.graph
digraph dependencies {
    "std::pin::core" -> "std::core::mem";
    "std::pin::core" -> "std::core::ptr";
    "std::pin::macros" -> "std::pin::core";
    "std::pin::async" -> "std::pin::core";
    "std::pin::async" -> "std::async::future";
}
```

### 3.3 並列ビルド戦略

```cpp
// src/build/parallel_builder.cpp
class ParallelModuleBuilder {
    struct BuildTask {
        std::string module_name;
        std::vector<std::string> dependencies;
        BuildStatus status;
    };

    void build_all() {
        // トポロジカルソート
        auto sorted = topological_sort(modules);

        // 並列実行可能なモジュールをグループ化
        auto levels = compute_build_levels(sorted);

        // レベルごとに並列ビルド
        for (const auto& level : levels) {
            std::vector<std::future<void>> futures;

            for (const auto& module : level) {
                futures.push_back(
                    std::async(std::launch::async,
                        [this, module] { build_module(module); }
                    )
                );
            }

            // 同レベルのビルド完了を待つ
            for (auto& f : futures) {
                f.wait();
            }
        }
    }
};
```

## 4. テストフレームワーク

### 4.1 階層的テスト構造

```cm
// tests/pin/unit/test_basic.cm
#[test_module]
module tests::pin::unit {
    use std::pin::*;

    #[test]
    fn test_pin_creation() {
        let mut val = 42;
        let pinned = pin!(val);
        assert_eq!(*pinned.get_ref(), 42);
    }

    #[test(should_panic)]
    fn test_pin_invariant_violation() {
        // パニックを期待
    }

    #[bench]
    fn bench_pin_overhead(b: &mut Bencher) {
        b.iter(|| {
            let mut val = 42;
            let pinned = pin!(val);
            black_box(pinned);
        });
    }
}
```

### 4.2 統合テスト

```cm
// tests/pin/integration/test_async.cm
#[integration_test]
async fn test_pinned_future() {
    use std::pin::*;
    use std::async::*;

    let future = async {
        yield_now().await;
        42
    };

    let pinned = Box::pin(future);
    let result = pinned.await;
    assert_eq!(result, 42);
}
```

### 4.3 プロパティベーステスト

```cm
// tests/pin/property/test_safety.cm
#[property_test]
fn prop_pin_address_stability(
    #[strategy(0..1000)] size: usize,
    #[strategy(any_string())] data: String,
) {
    let mut container = Container::new(size, data);
    let addr_before = &container as *const _ as usize;

    let pinned = pin!(container);
    let addr_after = &*pinned.get_ref() as *const _ as usize;

    prop_assert_eq!(addr_before, addr_after);
}
```

## 5. ドキュメントシステム

### 5.1 インラインドキュメント

```cm
/// Pinはメモリ上で値が移動しないことを保証する型です。
///
/// # 概要
///
/// 自己参照構造体や非同期処理において、メモリアドレスが
/// 変わらないことを保証する必要があります。
///
/// # 例
///
/// ```cm
/// use std::pin::*;
///
/// let mut value = 42;
/// let pinned = pin!(value);
/// assert_eq!(*pinned.get_ref(), 42);
/// ```
///
/// # 安全性
///
/// `Pin`は以下の不変条件を保証します：
/// - ピン留めされた値は移動しない
/// - 内部可変性は`Unpin`な型のみ
///
/// # パニック
///
/// 不正な操作を行うと以下の場合にパニックします：
/// - デバッグモードでのアドレス変更検出時
#[must_use = "Pin should be used to ensure memory safety"]
export struct Pin<P: Deref> {
    // ...
}
```

### 5.2 自動ドキュメント生成

```bash
# ドキュメント生成
cm doc --output docs/api

# 出力例
docs/api/
├── index.html
├── std/
│   ├── pin/
│   │   ├── index.html
│   │   ├── struct.Pin.html
│   │   ├── trait.Unpin.html
│   │   └── macro.pin.html
└── search-index.js
```

## 6. バージョニングとリリース

### 6.1 セマンティックバージョニング

```toml
# バージョン管理ポリシー
[versioning]
# MAJOR.MINOR.PATCH
# MAJOR: 破壊的変更
# MINOR: 後方互換性のある機能追加
# PATCH: バグ修正

current = "0.11.0"
minimum_supported = "0.10.0"

[compatibility]
# APIの互換性マトリクス
"0.11.x" = { compatible_with = ["0.10.x"] }
"0.10.x" = { compatible_with = ["0.9.x"] }
```

### 6.2 機能の段階的公開

```cm
// 実験的機能
#[unstable(feature = "pin_raw", issue = "42")]
export fn pin_raw<T>(ptr: *mut T) -> Pin<*mut T> {
    // 実験的実装
}

// 安定化プロセス
#[stable(since = "0.11.0")]
export fn pin_new<T: Unpin>(value: &mut T) -> Pin<&mut T> {
    // 安定版実装
}

// 非推奨化
#[deprecated(since = "0.12.0", note = "Use Pin::new instead")]
export fn create_pin<T>(value: &mut T) -> Pin<&mut T> {
    Pin::new(value)
}
```

## 7. パフォーマンス最適化

### 7.1 コンパイル時最適化

```cm
// リンク時最適化（LTO）対応
#[inline(always)]
#[no_mangle]
export fn critical_path_function() {
    // クリティカルパス
}

// プロファイルガイド最適化（PGO）
#[hot]
export fn frequently_called() {
    // 頻繁に呼ばれる
}

#[cold]
export fn error_handler() {
    // めったに呼ばれない
}
```

### 7.2 ビルドキャッシュ

```yaml
# .cm-cache/cache-manifest.yaml
modules:
  - name: std::pin::core
    hash: sha256:abc123...
    size: 45KB
    dependencies:
      - std::core::mem@0.10.0
      - std::core::ptr@0.10.0
    artifacts:
      object: std_pin_core.o
      metadata: std_pin_core.meta
    last_modified: 2026-01-11T10:30:00Z
```

## 8. CI/CD統合

### 8.1 継続的インテグレーション

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        cm-version: [0.10.0, 0.11.0, nightly]

    steps:
      - uses: actions/checkout@v2

      - name: Install Cm
        uses: cm-lang/setup-cm@v1
        with:
          cm-version: ${{ matrix.cm-version }}

      - name: Build
        run: cm build --all-features

      - name: Test
        run: cm test --all

      - name: Benchmark
        run: cm bench --save-baseline

      - name: Doc
        run: cm doc --check
```

### 8.2 品質ゲート

```toml
# quality-gates.toml
[coverage]
minimum = 80  # 最低80%のテストカバレッジ

[performance]
max_regression = 5  # 5%以上の性能劣化は許可しない

[size]
max_growth = 10  # バイナリサイズ10%以上の増加は警告

[complexity]
max_cyclomatic = 10  # 循環的複雑度の上限
```

## 9. エラー処理とデバッグ

### 9.1 構造化エラー

```cm
// std/pin/errors.cm
module std::pin::errors;

export enum PinError {
    InvalidOperation {
        operation: String,
        reason: String,
        #[help]
        suggestion: String,
    },

    InvariantViolation {
        expected: String,
        actual: String,
        location: SourceLocation,
    },

    UnsafeOperation {
        description: String,
        #[note]
        safety_requirements: Vec<String>,
    },
}

impl PinError for Display {
    fn fmt(&self, f: &mut Formatter) -> Result {
        match self {
            InvalidOperation { operation, reason, suggestion } => {
                write!(f, "Invalid operation '{}': {}\n", operation, reason)?;
                write!(f, "  help: {}", suggestion)
            },
            // ...
        }
    }
}
```

### 9.2 デバッグサポート

```cm
// デバッグビルド時の追加機能
#[cfg(debug)]
module std::pin::debug {
    // メモリレイアウト可視化
    export fn visualize_pin_layout<T>(pin: &Pin<&T>) {
        println!("Pin Layout for {}:", type_name::<T>());
        println!("  Address: {:p}", pin.get_ref());
        println!("  Size: {} bytes", size_of::<T>());
        println!("  Alignment: {} bytes", align_of::<T>());
    }

    // 不変条件チェック
    export fn assert_pin_invariants<T>(pin: &Pin<&mut T>) {
        // アドレス追跡
        // スタック/ヒープ判定
        // etc.
    }
}
```

## 10. まとめ

この大規模ライブラリアーキテクチャにより：

1. **スケーラビリティ**: 数十万行規模のライブラリも管理可能
2. **保守性**: モジュール間の独立性で変更の影響を最小化
3. **品質保証**: 自動テストとCI/CDによる継続的品質管理
4. **パフォーマンス**: 増分ビルドと最適化で高速化
5. **ドキュメント**: 自動生成と一元管理

Pinライブラリのような複雑な機能も、この設計により効率的に開発・保守できます。

---

**作成者:** Claude Code
**ステータス:** アーキテクチャ設計
**実装優先度:** 高