# リグレッションテスト

## 概要

Cmのリグレッションテストは、全バックエンド（インタプリタ、Rust、TypeScript）で同一の結果を保証します。

## ディレクトリ構造

```
tests/regression/
├── README.md
├── basic/              # 基本構文
│   ├── hello.cm
│   ├── hello.expected
│   └── hello.meta.toml
├── types/              # 型システム
├── control_flow/       # 制御フロー
├── generics/           # ジェネリクス
├── async/              # async（インタプリタ限定）
└── platform_specific/
    ├── ffi/            # FFI（ネイティブ限定）
    └── wasm/           # WASM限定
```

## テストファイル形式

### *.cm (テストコード)

```cpp
// tests/regression/basic/hello.cm
void main() {
    println("Hello, World!");
}
```

### *.expected (期待出力)

```
Hello, World!
```

### *.meta.toml (メタ情報)

```toml
[test]
name = "hello_world"
description = "Basic println test"

# テスト対象バックエンド
backends = ["interpreter", "rust", "typescript"]

# 全バックエンドで同一出力を期待
expect_same_output = true

# タイムアウト（秒）
timeout = 10
```

---

## クロスバックエンド一致要件

### 同一結果が必須

```cpp
// tests/regression/types/arithmetic.cm
void main() {
    println(1 + 2);       // 3
    println(10 / 3);      // 3 (整数除算)
    println(10.0 / 3.0);  // 3.333...
}
```

```toml
# arithmetic.meta.toml
[test]
backends = ["interpreter", "rust", "typescript"]
expect_same_output = true
```

### プラットフォーム固有テスト

```toml
# tests/regression/async/fetch.meta.toml
[test]
name = "async_fetch"
backends = ["interpreter"]  # async はインタプリタのみ（Phase 1）

# または
[test.skip]
rust = "async not implemented"
typescript = "async not implemented"
```

```toml
# tests/regression/platform_specific/ffi/malloc.meta.toml
[test]
name = "ffi_malloc"
backends = ["rust"]  # FFI はネイティブのみ

[test.platforms]
linux = true
macos = true
windows = false  # Windows未対応
```

---

## テスト実行

```bash
# 全リグレッションテスト
cm test regression

# 特定カテゴリ
cm test regression/basic
cm test regression/types

# 特定バックエンド
cm test regression --backend interpreter
cm test regression --backend rust

# クロスバックエンド検証
cm test regression --cross-verify
```

---

## クロスバックエンド検証フロー

```
┌─────────────────────────────────────────────────────────────┐
│                Cross-Backend Verification                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  test.cm                                                    │
│     │                                                       │
│     ├──→ [Interpreter] ──→ output_interp                   │
│     │                                                       │
│     ├──→ [Rust Codegen] ──→ [rustc] ──→ output_rust        │
│     │                                                       │
│     └──→ [TS Codegen] ──→ [node] ──→ output_ts             │
│                                                             │
│     Compare: output_interp == output_rust == output_ts     │
│                                                             │
│     If all equal: ✅ PASS                                   │
│     If differ:    ❌ FAIL (backend inconsistency)           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## サンプルテスト

### basic/hello.cm

```cpp
void main() {
    println("Hello, World!");
}
```

### types/option.cm

```cpp
int getValue(Option<int> opt) {
    match (opt) {
        Some(v) => return v;
        None => return 0;
    }
}

void main() {
    println(getValue(Some(42)));  // 42
    println(getValue(None));      // 0
}
```

### control_flow/loop.cm

```cpp
void main() {
    for (int i = 0; i < 5; i++) {
        println(i);
    }
}
```

---

## CI統合

```yaml
# .github/workflows/ci.yml
regression-test:
  runs-on: ubuntu-latest
  steps:
    - name: Cross-backend verification
      run: cm test regression --cross-verify
```
