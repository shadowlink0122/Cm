[English](rust.en.html)

# Rust Codegen 設計

## 概要

HIRからRustコードへの変換規則を定義します。

## 型マッピング

| Cm | Rust | 備考 |
|----|------|------|
| `int` | `i64` | 64bit整数 |
| `float` | `f64` | 64bit浮動小数点 |
| `bool` | `bool` | |
| `char` | `char` | Unicode |
| `String` | `String` | 所有権あり |
| `&String` | `&str` | 借用 |
| `Array<T>` | `Vec<T>` | 動的配列 |
| `Option<T>` | `Option<T>` | そのまま |
| `Result<T, E>` | `Result<T, E>` | そのまま |

## 構文変換

### 関数定義

```cpp
// Cm (C++風)
int add(int a, int b) {
    return a + b;
}

// Rust
fn add(a: i64, b: i64) -> i64 {
    a + b
}
```

### 構造体

```cpp
// Cm
struct Point {
    int x;
    int y;
};

// Rust
#[derive(Clone, Debug)]
struct Point {
    x: i64,
    y: i64,
}
```

### インターフェース → trait

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

// Rust
trait Printable {
    fn print(&self);
}

impl Printable for Point {
    fn print(&self) {
        println!("({}, {})", self.x, self.y);
    }
}
```

### ジェネリクス

```cpp
// Cm
T identity<T>(T value) {
    return value;
}

// Rust
fn identity<T>(value: T) -> T {
    value
}
```

### パターンマッチ

```cpp
// Cm
int getValue(Option<int> opt) {
    match (opt) {
        Some(v) => return v;
        None => return 0;
    }
}

// Rust
fn get_value(opt: Option<i64>) -> i64 {
    match opt {
        Some(v) => v,
        None => 0,
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

// Rust
async fn fetch_data(url: String) -> String {
    let res = http::get(&url).await;
    res.body
}
```

## 所有権マッピング

| Cm | Rust | 説明 |
|----|------|------|
| `T` (値渡し) | `T` | 所有権移動 |
| `&T` | `&T` | 不変借用 |
| `&mut T` | `&mut T` | 可変借用 |
| (自動) | `Clone` | 必要時に自動Clone |

## 名前変換

| Cm | Rust |
|----|------|
| `snake_case` 関数 | `snake_case` |
| `PascalCase` 型 | `PascalCase` |
| `SCREAMING_CASE` 定数 | `SCREAMING_CASE` |
| `::` モジュールパス | `::` |

## 生成ファイル構造

```
target/rust/
├── Cargo.toml       # 自動生成
├── src/
│   ├── lib.rs       # ライブラリ
│   ├── main.rs      # バイナリ
│   └── modules/     # モジュール
└── tests/
```

## 現時点での制限

> [!NOTE]
> 以下は実装進行に応じて詳細化予定

- ライフタイム推論: 自動推論、複雑なケースはエラー
- マクロ: Cm側マクロ → Rust関数に展開
- unsafe: 明示的なunsafeブロック必要