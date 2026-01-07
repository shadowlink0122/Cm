[English](transpiler_overload_strategy.en.html)

# トランスパイラ オーバーロード実装戦略

## 概要

Cm言語の `overload` システムを、異なる特性を持つターゲット言語へ変換する戦略を定義します。

## ターゲット言語の特性

| 言語 | オーバーロード | 制約 | 推奨戦略 |
|------|--------------|------|----------|
| **Rust** | ❌ 不可 | トレイトは可能 | 名前マングリング or トレイト |
| **TypeScript** | ✅ 可能 | シグネチャのみ | オーバーロードシグネチャ |
| **WebAssembly** | ❌ 不可 | 関数名は一意 | 名前マングリング |
| **C++** | ✅ 可能 | ネイティブ対応 | そのまま出力 |

## 1. Rust トランスパイル戦略

### 戦略A: 名前マングリング（推奨）

最もシンプルで確実な方法。全ての関数に一意の名前を付ける。

```cm
// Cm ソース
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
overload string add(string a, string b) { return a + b; }

// 使用例
int x = add(1, 2);
double y = add(1.0, 2.0);
string z = add("hello", "world");
```

```rust
// Rust 出力
#[inline]
fn add_i32_i32(a: i32, b: i32) -> i32 { a + b }

#[inline]
fn add_f64_f64(a: f64, b: f64) -> f64 { a + b }

#[inline]
fn add_String_String(a: String, b: String) -> String {
    format!("{}{}", a, b)
}

// 呼び出しサイトでの変換
let x = add_i32_i32(1, 2);
let y = add_f64_f64(1.0, 2.0);
let z = add_String_String("hello".to_string(), "world".to_string());
```

#### マングリング規則

```
関数名_型1_型2_...

型マッピング:
- int -> i32
- double -> f64
- string -> String
- Vec<T> -> Vec_T
- T* -> ptr_T
- const T& -> ref_T
```

### 戦略B: トレイトベース（複雑なケース）

演算子オーバーロードやメソッドに適している。

```cm
// Cm ソース
struct Complex {
    double real;
    double imag;
}

Complex operator+(const Complex& a, const Complex& b) {
    return Complex{a.real + b.real, a.imag + b.imag};
}

Complex operator+(const Complex& a, double b) {
    return Complex{a.real + b, a.imag};
}
```

```rust
// Rust 出力
#[derive(Clone, Copy)]
struct Complex {
    real: f64,
    imag: f64,
}

impl std::ops::Add for Complex {
    type Output = Complex;

    fn add(self, other: Complex) -> Complex {
        Complex {
            real: self.real + other.real,
            imag: self.imag + other.imag,
        }
    }
}

// カスタムトレイトで追加の演算
trait AddScalar {
    fn add_scalar(self, scalar: f64) -> Self;
}

impl AddScalar for Complex {
    fn add_scalar(self, scalar: f64) -> Complex {
        Complex {
            real: self.real + scalar,
            imag: self.imag,
        }
    }
}
```

### 戦略C: マクロディスパッチ（実験的）

```rust
// Rust マクロで型に基づくディスパッチ
macro_rules! add {
    ($a:expr, $b:expr) => {{
        // 型推論に基づく選択
        let a = $a;
        let b = $b;
        _add_dispatch(a, b)
    }};
}

#[inline]
fn _add_dispatch<T>(a: T, b: T) -> T
where
    T: CmAdd
{
    a.cm_add(b)
}

trait CmAdd {
    fn cm_add(self, other: Self) -> Self;
}

// 各型に実装
impl CmAdd for i32 { /* ... */ }
impl CmAdd for f64 { /* ... */ }
impl CmAdd for String { /* ... */ }
```

## 2. TypeScript トランスパイル戦略

TypeScriptはオーバーロードシグネチャをネイティブサポート。

```cm
// Cm ソース
overload void print(int x) { /* ... */ }
overload void print(double x) { /* ... */ }
overload void print(string s) { /* ... */ }
overload void print(bool b) { /* ... */ }
```

```typescript
// TypeScript 出力
function print(x: number): void;
function print(s: string): void;
function print(b: boolean): void;
function print(value: any): void {
    if (typeof value === 'number') {
        console.log(`Number: ${value}`);
    } else if (typeof value === 'string') {
        console.log(`String: ${value}`);
    } else if (typeof value === 'boolean') {
        console.log(`Boolean: ${value}`);
    }
}
```

### コンストラクタオーバーロード

```cm
// Cm ソース
impl<T> Vec<T> {
    self() { /* ... */ }
    overload self(size_t capacity) { /* ... */ }
    overload self(T fill, size_t count) { /* ... */ }
}
```

```typescript
// TypeScript 出力
class Vec<T> {
    private data: T[];
    private size: number;
    private capacity: number;

    constructor();
    constructor(capacity: number);
    constructor(fill: T, count: number);
    constructor(...args: any[]) {
        if (args.length === 0) {
            this.data = [];
            this.size = 0;
            this.capacity = 0;
        } else if (args.length === 1) {
            const capacity = args[0] as number;
            this.data = new Array(capacity);
            this.size = 0;
            this.capacity = capacity;
        } else if (args.length === 2) {
            const [fill, count] = args;
            this.data = new Array(count).fill(fill);
            this.size = count;
            this.capacity = count;
        }
    }
}
```

## 3. WebAssembly トランスパイル戦略

WebAssemblyは名前マングリングが必須。

```cm
// Cm ソース
overload int max(int a, int b) { return a > b ? a : b; }
overload double max(double a, double b) { return a > b ? a : b; }
```

```wat
;; WebAssembly テキスト形式
(module
    ;; int版
    (func $max_i32_i32 (param i32 i32) (result i32)
        local.get 0
        local.get 1
        i32.gt_s
        if (result i32)
            local.get 0
        else
            local.get 1
        end
    )

    ;; double版
    (func $max_f64_f64 (param f64 f64) (result f64)
        local.get 0
        local.get 1
        f64.gt
        if (result f64)
            local.get 0
        else
            local.get 1
        end
    )

    ;; エクスポート
    (export "max_i32_i32" (func $max_i32_i32))
    (export "max_f64_f64" (func $max_f64_f64))
)
```

### JavaScript バインディング

```javascript
// JavaScript グルーコード
class CmRuntime {
    constructor(wasmModule) {
        this.wasm = wasmModule.instance.exports;
    }

    // 型に基づくディスパッチ
    add(a, b) {
        if (typeof a === 'number' && Number.isInteger(a)) {
            return this.wasm.add_i32_i32(a, b);
        } else if (typeof a === 'number') {
            return this.wasm.add_f64_f64(a, b);
        } else if (typeof a === 'string') {
            // 文字列は特別な処理が必要
            return this._addStrings(a, b);
        }
    }

    _addStrings(a, b) {
        // メモリ操作を通じて文字列を処理
        // ...
    }
}
```

## 4. 実装上の考慮事項

### コンパイラの実装

```cpp
// compiler.hpp
enum class TargetLang {
    Rust,
    TypeScript,
    WebAssembly,
    Cpp
};

class OverloadResolver {
    TargetLang target;

public:
    std::string mangle_name(const FunctionSignature& sig) {
        switch (target) {
            case TargetLang::Rust:
            case TargetLang::WebAssembly:
                return create_mangled_name(sig);

            case TargetLang::TypeScript:
            case TargetLang::Cpp:
                return sig.name; // そのまま使用
        }
    }

    void emit_function(const FunctionDecl& func) {
        switch (target) {
            case TargetLang::Rust:
                emit_rust_function(func);
                break;
            case TargetLang::TypeScript:
                emit_ts_overload_signature(func);
                break;
            // ...
        }
    }
};
```

### 呼び出しサイトの変換

```cpp
// 呼び出しサイトでの解決
void transform_call(CallExpr& call) {
    if (target == TargetLang::Rust ||
        target == TargetLang::WebAssembly) {
        // オーバーロード解決して正確な関数名に変換
        auto resolved = resolve_overload(call);
        call.callee = mangle_name(resolved);
    }
    // TypeScriptとC++はそのまま
}
```

## 5. 最適化

### インライン化

```rust
// 小さな関数は積極的にインライン化
#[inline(always)]
fn add_i32_i32(a: i32, b: i32) -> i32 { a + b }
```

### LTO（Link Time Optimization）

```toml
# Cargo.toml
[profile.release]
lto = true  # 重複コードの除去
```

## 6. エラーハンドリング

### 型推論失敗時

```cm
// Cm
auto result = add(get_value(), 42);  // get_value()の型が不明
```

```
エラー: オーバーロード解決に失敗しました
  候補:
    - add(int, int) -> int
    - add(double, double) -> double

  get_value()の戻り値型を明示してください:
    int result = add((int)get_value(), 42);
```

## まとめ

1. **Rust/WASM**: 名前マングリングが基本戦略
2. **TypeScript**: ネイティブのオーバーロード機能を活用
3. **呼び出しサイト**: コンパイル時に正確な関数を解決
4. **最適化**: インライン化とLTOで性能を確保
5. **エラー**: 明確なエラーメッセージで開発者を支援