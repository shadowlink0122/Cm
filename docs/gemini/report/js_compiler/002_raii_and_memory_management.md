[English](002_raii_and_memory_management.en.html)

# JSコンパイラ詳細設計: 002 RAIIとメモリ管理

## 1. 構造体とクラスのマッピング

Cmの `struct` は JavaScriptの `class` にマッピングします。

### 1.1 クラス定義
```rust
// Cm
struct Point { x: int; y: int; }
```

```javascript
// JS
class Point {
    constructor(x, y) {
        this.x = x;
        this.y = y;
        this.__moved = false; // 移動セマンティクス用フラグ
    }

    // デストラクタ（明示的に呼び出される）
    destructor() {
        if (this.__moved) return;
        // リソース解放処理...
    }
}
```

## 2. RAIIの実現 (Scope & Destructors)

Cmではスコープを抜ける変数は自動的にデストラクタが呼ばれます。JSでは `try...finally` ブロックを用いてこれを実現します。

### 2.1 ブロック変換アルゴリズム

**Cmソース:**
```rust
{
    let p = Point(10, 20);
    p.x = 30;
    // ... p を使用
} // ここで ~Point() が呼ばれる
```

**JS出力:**
```javascript
{
    let p = new Point(10, 20);
    try {
        p.x = 30;
        // ... p を使用
    } finally {
        p.destructor();
    }
}
```

### 2.2 ネストしたスコープ
変数が定義されるたびに `try...finally` がネストするとコードが深くなりすぎるため、関数全体またはブロック全体を一つの `try...finally` で囲み、管理オブジェクトを使用するアプローチも検討しましたが、**「変数の寿命の正確さ」** を優先し、基本は変数が宣言されたスコープに対して `try...finally` を生成します。

最適化として、デストラクタを持たない型（`int`だけの構造体など）については `try...finally` を省略します。

## 3. 移動セマンティクス (Move Semantics)

JSは参照コピー（共有）が基本ですが、Cmは所有権の移動があります。

### 3.1 Moveの実装
所有権が移動した場合、元の変数のデストラクタは無効化される必要があります。

**Cm:**
```rust
let a = File::open("test");
let b = a; // Move: aの所有権はbへ。aは無効。
```

**JS:**
```javascript
let a = File.open("test");
let b = null;
try {
    b = a;       // JSとしては参照コピー
    a.__moved = true; // aを無効化フラグでマーク
    
    // ...
} finally {
    if (b) b.destructor();
    a.destructor(); // __movedがtrueなので何もしない
}
```

### 3.2 関数へのMove引数
関数に値を渡す場合も同様です。

```javascript
function takeFile(f) {
    try {
        // ...
    } finally {
        f.destructor(); // 関数内で所有権が尽きる場合、ここで解放
    }
}

// 呼び出し側
let f = new File(...);
try {
    takeFile(f);
    f.__moved = true; // 呼び出し後に無効化
} finally {
    f.destructor();
}
```

## 4. `Box<T>` とヒープ割り当て
JSでは全てのオブジェクトがヒープにあるため、`Box<T>` は単なる透過的なラッパー、あるいは `T` そのものとして扱えます。Cmの意味論上の `Box` は JSでは除去しても問題ありませんが、ポインタのセマンティクスを維持するために薄いラッパーを残す場合があります。