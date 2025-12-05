# プラットフォーム・ターゲット設計

## 概要

Cmは **明示的なターゲット指定** により、ネイティブ/Web/Bare-metalを切り替えます。Rustの `no_std` に相当する概念を導入します。

## ターゲット種別

| ターゲット | 説明 | std | async | FFI |
|-----------|------|-----|-------|-----|
| `native` | デスクトップ/サーバー | ✅ | ✅ tokio | C/Rust |
| `web` | ブラウザ/Node.js | ✅ | ✅ Promise | TS |
| `baremetal` | OS/組み込み | ❌ | ✅ カスタム | なし |

## ファイル拡張子によるターゲット

| 拡張子 | ターゲット | 説明 |
|--------|-----------|------|
| `.cm` | auto | 設定ファイルに従う |
| `.cm.native` | native | ネイティブ専用 |
| `.cm.web` | web | Web専用 |
| `.cm.baremetal` | baremetal | Bare-metal専用 |

```
src/
├── main.cm              # 共通エントリ
├── platform.cm.native   # ネイティブ専用
├── platform.cm.web      # Web専用
└── kernel.cm.baremetal  # Bare-metal専用
```

## 設定ファイル (Cm.toml)

```toml
[package]
name = "my-app"
version = "0.1.0"

# デフォルトターゲット
default-target = "native"

# ネイティブターゲット
[target.native]
backend = "rust"
std = true
async = true
ffi = ["c", "rust"]

# Webターゲット
[target.web]
backend = "typescript"   # または "wasm"
std = true
async = true
ffi = ["ts"]             # TSモジュールimport可能

# Bare-metalターゲット
[target.baremetal]
backend = "rust"
std = false              # no_std相当
async = false
ffi = []
linker_script = "kernel.ld"
```

---

## 標準ライブラリ階層

```
cm-core          # 全ターゲット共通（型、基本関数）
├── cm-std       # std有効時（IO、ネットワーク、ファイル）
│   ├── cm-async # async有効時（Future、ランタイム）
│   └── cm-web   # web専用（DOM、fetch）
└── cm-baremetal # baremetal専用（割り込み、MMIO）
```

---

## TSモジュールのimport (Web FFI)

### 基本構文

```cpp
// Cm (web専用)
extern "ts" {
    // npmパッケージ
    import "react" {
        fn createElement(tag: String, props: any) -> Element;
        fn useState<T>(initial: T) -> (T, fn(T));
    }
    
    // ローカルtsファイル
    import "./utils.ts" {
        fn formatDate(date: Date) -> String;
    }
}
```

### 生成TypeScript

```typescript
// 生成コード
import { createElement, useState } from 'react';
import { formatDate } from './utils';

// Cm関数から呼び出し可能
```

### React使用例

```cpp
// Cm (src/app.cm.web)
extern "ts" {
    import "react" as React;
    import "react-dom/client" as ReactDOM;
}

Element App() {
    const [count, setCount] = React::useState(0);
    
    return React::createElement("div", null,
        React::createElement("p", null, "Count: ${count}"),
        React::createElement("button", 
            { onClick: () => setCount(count + 1) },
            "Increment"
        )
    );
}

void main() {
    auto root = ReactDOM::createRoot(
        document.getElementById("root")
    );
    root.render(React::createElement(App, null));
}
```

---

## C/Rust FFI (Native)

```cpp
// Cm (src/native.cm.native)
extern "C" {
    void* malloc(size_t size);
    void free(void* ptr);
    int printf(const char* fmt, ...);
}

extern "Rust" {
    #[crate = "tokio"]
    async fn spawn<T>(future: Future<T>) -> T;
}
```

---

## 条件付きコンパイル

```cpp
// Cm
#[cfg(target = "native")]
void platform_init() {
    // ネイティブ初期化
}

#[cfg(target = "web")]
void platform_init() {
    // Web初期化
}

#[cfg(target = "baremetal")]
void platform_init() {
    // Bare-metal初期化
}

// 共通コード
void main() {
    platform_init();
    // ...
}
```

---

## ビルドコマンド

```bash
# ターゲット指定
cm build --target native
cm build --target web
cm build --target baremetal

# 設定ファイルのデフォルト使用
cm build
```

---

## まとめ

| 機能 | native | web | baremetal |
|------|--------|-----|-----------|
| std | ✅ | ✅ | ❌ |
| async | ✅ | ✅ | ❌ |
| new/delete | ✅ | ✅ (無視) | ✅ |
| shared<T> | ✅ | ✅ (無視) | ❌ |
| FFI (C/Rust) | ✅ | ❌ | - |
| FFI (TS) | ❌ | ✅ | ❌ |
| GC | ❌ | JS任せ | ❌ |
