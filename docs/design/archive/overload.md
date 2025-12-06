# Cm言語 オーバーロードシステム設計

## 概要

Cm言語のオーバーロードシステムは、関数の多重定義を明示的に管理し、トランスパイラ互換性を保証する設計です。

## 基本原則

### 1. 明示的オーバーロード宣言

```cm
// overload修飾子が必須
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
overload string add(string a, string b) { return a + b; }
```

**重要**: 同じ名前の関数を複数定義する場合、`overload`修飾子が必須です。

### 2. オーバーロード解決規則

優先順位（高→低）：
1. 完全一致
2. 暗黙的な数値変換（int → double）
3. ユーザー定義の暗黙変換
4. 可変長引数

曖昧な場合はコンパイルエラー。

## トランスパイラ戦略

### Rustバックエンド

```rust
// 名前マングリング
fn add_i32_i32(a: i32, b: i32) -> i32 { a + b }
fn add_f64_f64(a: f64, b: f64) -> f64 { a + b }
fn add_String_String(a: String, b: String) -> String {
    format!("{}{}", a, b)
}

// ディスパッチャー（必要に応じて）
macro_rules! add {
    ($a:expr, $b:expr) => {
        match (type_of($a), type_of($b)) {
            (i32, i32) => add_i32_i32($a, $b),
            (f64, f64) => add_f64_f64($a, $b),
            _ => compile_error!("No matching overload")
        }
    }
}
```

### TypeScriptバックエンド

```typescript
// 関数オーバーロード宣言
function add(a: number, b: number): number;
function add(a: string, b: string): string;
function add(a: any, b: any): any {
    if (typeof a === "number" && typeof b === "number") {
        return a + b;
    } else if (typeof a === "string" && typeof b === "string") {
        return a + b;
    }
    throw new Error("No matching overload");
}
```

### WASMバックエンド

```wat
;; 名前マングリング方式
(func $add_i32_i32 (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add)

(func $add_f64_f64 (param f64 f64) (result f64)
    local.get 0
    local.get 1
    f64.add)
```

## コンストラクタオーバーロード

```cm
struct Vec<T> {
    T* data;
    size_t size;
    size_t capacity;
}

impl<T> Vec<T> {
    // デフォルトコンストラクタ
    overload self() {
        this.data = nullptr;
        this.size = 0;
        this.capacity = 0;
    }

    // 容量指定コンストラクタ
    overload self(size_t cap) {
        this.data = alloc<T>(cap);
        this.size = 0;
        this.capacity = cap;
    }

    // コピーコンストラクタ
    overload self(const Vec<T>& other) {
        this.capacity = other.capacity;
        this.size = other.size;
        this.data = alloc<T>(this.capacity);
        memcpy(this.data, other.data, sizeof(T) * this.size);
    }
}
```

## 演算子オーバーロード

```cm
struct Complex {
    double real;
    double imag;
}

impl Complex {
    // 加算演算子
    overload operator+(const Complex& other) -> Complex {
        return Complex{
            this.real + other.real,
            this.imag + other.imag
        };
    }

    // 乗算演算子
    overload operator*(const Complex& other) -> Complex {
        return Complex{
            this.real * other.real - this.imag * other.imag,
            this.real * other.imag + this.imag * other.real
        };
    }
}
```

## ジェネリック関数のオーバーロード

```cm
// 一般的なmax関数
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

// 特殊化版（パフォーマンス最適化）
overload int max(int a, int b) {
    // ビット演算による最適化実装
    return a ^ ((a ^ b) & -(a < b));
}
```

## MIRでの表現

MIRレベルでは、すべてのオーバーロード関数は名前マングリングによって一意の識別子を持ちます：

```
Function: add_i32_i32
Function: add_f64_f64
Function: add_String_String
```

## エラー処理

```cm
// 曖昧なオーバーロード
overload void process(int x, double y) { }
overload void process(double x, int y) { }

// エラー：process(1, 2) は曖昧
// 両方の関数が暗黙変換で呼び出し可能
```

## 実装状況

| 機能 | インタープリタ | Rust | TypeScript | WASM |
|------|--------------|------|------------|------|
| 基本オーバーロード | ❌ | 🔧 | 🔧 | 🔧 |
| コンストラクタ | ❌ | 🔧 | 🔧 | ❌ |
| 演算子 | ❌ | 🔧 | 🔧 | ❌ |
| ジェネリック | ❌ | 🔧 | 🔧 | ❌ |

## まとめ

Cm言語のオーバーロードシステムは：
- **明示的**: `overload`修飾子で意図を明確化
- **安全**: 曖昧性を許さない厳密な解決規則
- **互換性**: 各バックエンドの特性を活かした実装
- **拡張可能**: 将来の機能追加を考慮した設計