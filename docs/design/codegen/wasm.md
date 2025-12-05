# WASM Codegen 設計

## 概要

CmからWebAssemblyへの変換設計です。Rust経由と直接生成の2つのアプローチを定義します。

## 生成方式

### Phase 1: Rust経由

```
Cm → Rust → wasm-pack → WASM + JS glue
```

### Phase 2: 直接生成（将来）

```
Cm → MIR → WASM (直接)
```

---

## Rust経由の変換

### Cargo.toml生成

```toml
[package]
name = "cm-wasm-app"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib", "rlib"]

[dependencies]
wasm-bindgen = "0.2"
js-sys = "0.3"
web-sys = { version = "0.3", features = ["console", "Window", "Document"] }

[profile.release]
opt-level = "s"
lto = true
```

### wasm-bindgen統合

```cpp
// Cm
#[wasm_export]
int add(int a, int b) {
    return a + b;
}

#[wasm_export]
void greet(String name) {
    js::console::log("Hello, ${name}!");
}
```

```rust
// 生成Rust
use wasm_bindgen::prelude::*;

#[wasm_bindgen]
pub fn add(a: i64, b: i64) -> i64 {
    a + b
}

#[wasm_bindgen]
pub fn greet(name: &str) {
    web_sys::console::log_1(&format!("Hello, {}!", name).into());
}
```

---

## JavaScript Interop

### JSからCm関数呼び出し

```javascript
// JavaScript
import init, { add, greet } from './pkg/cm_wasm_app.js';

async function main() {
    await init();
    console.log(add(1, 2));  // 3
    greet("World");          // Hello, World!
}
```

### CmからJS API呼び出し

```cpp
// Cm
import wasm::js;

void main() {
    // DOM操作
    auto doc = js::document();
    auto elem = doc.getElementById("app");
    elem.innerHTML = "<h1>Hello from Cm!</h1>";
    
    // fetch API
    auto response = await js::fetch("https://api.example.com/data");
    auto json = await response.json();
    println(json);
}
```

---

## WASM固有のライブラリ

### cm-wasm 標準ライブラリ

```cpp
// Cm
import cm_wasm::dom;
import cm_wasm::fetch;
import cm_wasm::storage;

void main() {
    // DOM
    auto elem = dom::querySelector("#app");
    dom::addEventListener(elem, "click", onClick);
    
    // LocalStorage
    storage::setItem("key", "value");
    auto val = storage::getItem("key");
    
    // Fetch
    auto res = await fetch::get("https://api.example.com");
}
```

### 提供モジュール

| モジュール | 機能 |
|-----------|------|
| `cm_wasm::dom` | DOM操作 |
| `cm_wasm::fetch` | HTTP リクエスト |
| `cm_wasm::storage` | LocalStorage/SessionStorage |
| `cm_wasm::canvas` | Canvas 2D/WebGL |
| `cm_wasm::audio` | WebAudio API |
| `cm_wasm::worker` | Web Workers |

---

## React/Vue連携

### Cmコンポーネント → React

```cpp
// Cm
import cm_wasm::react;

#[react_component]
Component Counter(int initial) {
    const [count, setCount] = useState(initial);
    
    return html`
        <div>
            <p>Count: ${count}</p>
            <button onClick=${() => setCount(count + 1)}>+</button>
        </div>
    `;
}
```

```javascript
// 使用側 (JavaScript)
import { Counter } from './pkg/cm_wasm_app.js';

function App() {
    return <Counter initial={0} />;
}
```

---

## メモリ管理

### WASM Linear Memory

```cpp
// Cm
#[wasm_memory]
Array<byte> buffer = new Array<byte>(1024);

// JSからアクセス
// const memory = wasmInstance.exports.memory;
// const buffer = new Uint8Array(memory.buffer);
```

### 文字列受け渡し

```cpp
// Cm → JS: 自動変換
String message = "Hello";
js::console::log(message);  // wasm-bindgenが変換

// JS → Cm: 自動変換
#[wasm_export]
void processString(String input) {
    println(input);
}
```

---

## ビルド設定

```toml
# Cm.toml
[target.wasm]
backend = "wasm"
opt-level = "s"          # size優先
features = ["dom", "fetch"]
out-dir = "pkg"

[target.wasm.web-sys]
features = [
    "console",
    "Document",
    "Element",
    "Window",
]
```

## 現時点での制限

> [!NOTE]
> 以下は実装進行に応じて詳細化予定

- スレッド: SharedArrayBuffer対応（Phase 2）
- SIMD: wasm-simd対応（Phase 2）
- GC: WASM GC proposal対応（将来）
