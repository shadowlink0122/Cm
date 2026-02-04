# WASM ランタイム設計

## 概要

CmのWASM環境固有の機能と、フロントエンドフレームワーク（React/Vue等）との統合設計です。

## WASM専用ライブラリ

### cm-wasm パッケージ

```toml
# Cm.toml
[dependencies]
cm-wasm = "1.0"

[dependencies.cm-wasm]
features = ["dom", "fetch", "react"]
```

### モジュール一覧

| モジュール | 機能 |
|-----------|------|
| `cm_wasm::dom` | DOM操作 |
| `cm_wasm::events` | イベントハンドリング |
| `cm_wasm::fetch` | HTTP リクエスト |
| `cm_wasm::storage` | LocalStorage/SessionStorage |
| `cm_wasm::canvas` | Canvas 2D |
| `cm_wasm::webgl` | WebGL |
| `cm_wasm::audio` | WebAudio API |
| `cm_wasm::worker` | Web Workers |
| `cm_wasm::react` | React統合 |
| `cm_wasm::vue` | Vue統合 |

---

## DOM操作

```cpp
// Cm
import cm_wasm::dom;
import cm_wasm::events;

void main() {
    // 要素取得
    auto app = dom::getElementById("app");
    auto buttons = dom::querySelectorAll(".btn");
    
    // 要素作成
    auto div = dom::createElement("div");
    div.className = "container";
    div.innerHTML = "<h1>Hello Cm!</h1>";
    
    // 追加
    app.appendChild(div);
    
    // イベント
    events::addEventListener(div, "click", (Event e) => {
        println("Clicked!");
    });
}
```

---

## React統合

### 関数コンポーネント

```cpp
// Cm
import cm_wasm::react;

#[component]
Element Counter(int initial) {
    const [count, setCount] = useState(initial);
    
    useEffect(() => {
        println("Counter mounted");
        return () => println("Counter unmounted");
    }, []);
    
    return html`
        <div class="counter">
            <p>Count: ${count}</p>
            <button onClick=${() => setCount(count + 1)}>+</button>
            <button onClick=${() => setCount(count - 1)}>-</button>
        </div>
    `;
}

// 使用
void main() {
    react::render(<Counter initial={0} />, dom::getElementById("root"));
}
```

### Hooks

```cpp
// Cm
import cm_wasm::react::hooks;

#[component]
Element TodoList() {
    const [todos, setTodos] = useState<Array<String>>([]);
    const [input, setInput] = useState("");
    
    const inputRef = useRef<Element>(null);
    
    const addTodo = useCallback(() => {
        if (input.length > 0) {
            setTodos([...todos, input]);
            setInput("");
        }
    }, [input, todos]);
    
    return html`
        <div>
            <input 
                ref=${inputRef}
                value=${input} 
                onInput=${(e) => setInput(e.target.value)} 
            />
            <button onClick=${addTodo}>Add</button>
            <ul>
                ${todos.map((t) => html`<li>${t}</li>`)}
            </ul>
        </div>
    `;
}
```

### 既存React/TSプロジェクトとの統合

```javascript
// JavaScript側
import { Counter } from 'cm-wasm-pkg';

function App() {
    return (
        <div>
            <h1>Mixed App</h1>
            <Counter initial={10} />  {/* Cmコンポーネント */}
        </div>
    );
}
```

---

## Vue統合

```cpp
// Cm
import cm_wasm::vue;

#[component]
struct Counter {
    count: int,
}

impl Counter {
    void increment() {
        this.count++;
    }
    
    Element render() {
        return html`
            <div>
                <p>{{ count }}</p>
                <button @click="increment">+</button>
            </div>
        `;
    }
}
```

---

## npm連携

### 既存ライブラリの使用

```cpp
// Cm
import "npm:lodash" as _;
import "npm:axios" as axios;
import "npm:dayjs" as dayjs;

async void main() {
    // lodash
    auto arr = [1, 2, 3];
    auto doubled = _.map(arr, (x) => x * 2);
    
    // axios
    auto response = await axios::get("https://api.example.com/data");
    println(response.data);
    
    // dayjs
    auto now = dayjs();
    println(now.format("YYYY-MM-DD"));
}
```

### 型定義

```cpp
// Cm
#[npm_types = "@types/lodash"]
import "npm:lodash" as _;

// 型情報が利用可能
auto result: Array<int> = _.map([1, 2, 3], (x) => x * 2);
```

---

## WASM固有の制限と対策

| 制限 | 対策 |
|------|------|
| 直接DOM操作不可 | JS glue経由 |
| メインスレッドブロック | async/Web Workers |
| ファイルシステムなし | IndexedDB/localStorage |
| ネットワーク制限 | CORS対応fetch |

---

## ビルド設定

```toml
# Cm.toml
[target.wasm]
backend = "wasm"
optimize = "size"        # size | speed

[target.wasm.features]
dom = true
react = true
fetch = true

[target.wasm.output]
format = "esm"           # esm | umd
name = "my-app"
```

---

## 生成ファイル

```
target/wasm/
├── pkg/
│   ├── my-app.js        # JS glue
│   ├── my-app.d.ts      # TypeScript型定義
│   ├── my-app_bg.wasm   # WASMバイナリ
│   └── package.json
└── www/                 # オプション: 開発サーバー用
    ├── index.html
    └── index.js
```