# TypeScript Codegen 設計

## 概要

HIRからTypeScriptコードへの変換規則を定義します。React等の既存ライブラリとの統合も考慮します。

## 型マッピング

| Cm | TypeScript | 備考 |
|----|------------|------|
| `int` | `number` | 整数・浮動小数点統合 |
| `float` | `number` | |
| `bool` | `boolean` | |
| `char` | `string` | 1文字 |
| `String` | `string` | |
| `Array<T>` | `T[]` | |
| `Map<K, V>` | `Map<K, V>` | |
| `Option<T>` | `T \| null` | |
| `Result<T, E>` | `T \| E` | Union型 |
| `void` | `void` | |
| `any` | `any` | 非推奨 |

## 構文変換

### 関数定義

```cpp
// Cm (C++風)
int add(int a, int b) {
    return a + b;
}

// TypeScript
function add(a: number, b: number): number {
    return a + b;
}
```

### 構造体 → interface/class

```cpp
// Cm
struct Point {
    int x;
    int y;
};

// TypeScript (interfaceモード)
interface Point {
    x: number;
    y: number;
}

// TypeScript (classモード)
class Point {
    constructor(public x: number, public y: number) {}
}
```

### インターフェース

```cpp
// Cm
interface Printable {
    void print();
};

impl Printable for Point {
    void print() {
        println("(${this.x}, ${this.y})");
    }
};

// TypeScript
interface Printable {
    print(): void;
}

class Point implements Printable {
    constructor(public x: number, public y: number) {}
    
    print(): void {
        console.log(`(${this.x}, ${this.y})`);
    }
}
```

### ジェネリクス

```cpp
// Cm
T identity<T>(T value) {
    return value;
}

// TypeScript
function identity<T>(value: T): T {
    return value;
}
```

### パターンマッチ → switch + 型ガード

```cpp
// Cm
int getValue(Option<int> opt) {
    match (opt) {
        Some(v) => return v;
        None => return 0;
    }
}

// TypeScript
function getValue(opt: number | null): number {
    if (opt !== null) {
        return opt;
    } else {
        return 0;
    }
}

// またはタグ付きユニオン
type Option<T> = { tag: "Some"; value: T } | { tag: "None" };

function getValue(opt: Option<number>): number {
    switch (opt.tag) {
        case "Some": return opt.value;
        case "None": return 0;
    }
}
```

### async/await

```cpp
// Cm
async String fetchData(String url) {
    Response res = await http::get(url);
    return res.body;
}

// TypeScript
async function fetchData(url: string): Promise<string> {
    const res = await http.get(url);
    return res.body;
}
```

---

## React統合

### Cmコンポーネント定義

```cpp
// Cm
import react;

struct Props {
    String name;
    int count;
};

Component<Props> Counter(Props props) {
    const [count, setCount] = useState(props.count);
    
    return (
        <div>
            <p>Hello, ${props.name}!</p>
            <p>Count: ${count}</p>
            <button onClick={() => setCount(count + 1)}>
                Increment
            </button>
        </div>
    );
}
```

### 生成TypeScript

```typescript
import React, { useState } from 'react';

interface Props {
    name: string;
    count: number;
}

const Counter: React.FC<Props> = (props) => {
    const [count, setCount] = useState(props.count);
    
    return (
        <div>
            <p>Hello, {props.name}!</p>
            <p>Count: {count}</p>
            <button onClick={() => setCount(count + 1)}>
                Increment
            </button>
        </div>
    );
};

export default Counter;
```

### 既存TSライブラリの使用

```cpp
// Cm
import "npm:lodash" as _;

int main() {
    auto arr = [1, 2, 3, 4, 5];
    auto doubled = _.map(arr, (x) => x * 2);
    println(doubled);
    return 0;
}
```

```typescript
// 生成TypeScript
import _ from 'lodash';

function main(): number {
    const arr = [1, 2, 3, 4, 5];
    const doubled = _.map(arr, (x) => x * 2);
    console.log(doubled);
    return 0;
}
```

---

## 生成ファイル構造

```
target/typescript/
├── package.json     # 自動生成
├── tsconfig.json    # 自動生成
├── src/
│   ├── index.ts
│   └── modules/
└── dist/            # tscビルド後
```

## 設定オプション

```toml
# Cm.toml
[target.typescript]
module = "esm"           # esm | commonjs
strict = true            # strict mode
jsx = "react-jsx"        # react | react-jsx | preserve
target = "ES2020"
outDir = "target/typescript"
```

## 現時点での制限

> [!NOTE]
> 以下は実装進行に応じて詳細化予定

- JSX: React固有構文の変換ルール
- npm連携: 外部パッケージの型定義取得
- Source Map: デバッグ用マッピング
