# Cm言語 統一オーバーロードシステム設計

## 基本方針

Cm言語では**明示的な `overload` 修飾子**を使用してオーバーロードを許可します。
これはC++の暗黙的オーバーロードとは異なり、開発者の意図を明確にし、
トランスパイラ対応を容易にします。

## overload修飾子の意味

`overload` 修飾子は「同じ名前で異なるシグネチャの関数を許可する」ことを意味します。
- **完全な上書き（override）ではない**
- **パラメータ型による実装の分岐を許可**
- **最初の宣言から `overload` が必要**

## 1. 基本ルール

```cm
// エラー：overloadなしで同名関数
int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }  // エラー！

// OK：overload修飾子で明示的に許可
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }  // OK
overload string add(string a, string b) { return a + b; }  // OK
```

### 重要な原則

1. **全てに `overload` が必要**: 最初の宣言から必須
2. **シグネチャが同じ場合はエラー**: 戻り値の型だけ違う場合も不可
3. **デフォルト引数との併用可能**: ただし曖昧性に注意

## 2. コンストラクタのオーバーロード

```cm
impl<T> Vec<T> {
    // デフォルトコンストラクタ（overload不要）
    self() {
        this.data = null;
        this.size = 0;
        this.capacity = 0;
    }

    // 追加のコンストラクタには overload が必要
    overload self(size_t initial_cap) {
        this.data = new T[initial_cap];
        this.size = 0;
        this.capacity = initial_cap;
    }

    overload self(T fill_value, size_t count = 10) {
        this.data = new T[count];
        this.size = count;
        this.capacity = count;
        for (size_t i = 0; i < count; i++) {
            this.data[i] = fill_value;
        }
    }

    overload self(const Vec<T>& other) {  // コピーコンストラクタ
        // ...
    }

    overload self(Vec<T>&& other) {  // ムーブコンストラクタ
        // ...
    }

    // デストラクタ（オーバーロード不可、1つのみ）
    ~self() {
        if (this.data != null) {
            delete[] this.data;
        }
    }
}
```

## 3. メソッドのオーバーロード

```cm
impl<T> Vec<T> for Container<T> {
    // 基本メソッド
    void push(T item) {
        if (size >= capacity) {
            grow();
        }
        data[size++] = item;
    }

    // オーバーロード版
    overload void push(T item, size_t times) {
        for (size_t i = 0; i < times; i++) {
            push(item);
        }
    }

    // 可変長引数版
    overload void push(T... items) {
        for (T item : items) {
            push(item);
        }
    }
}
```

## 4. トランスパイラ戦略

### 4.1 Rust への変換

Rustは関数オーバーロードをサポートしないため、以下の戦略を使用：

#### 戦略A: トレイトベース

```cm
// Cm
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
overload string add(string a, string b) { return a + b; }
```

```rust
// Rust変換結果
trait AddOp<T> {
    type Output;
    fn add(self, other: T) -> Self::Output;
}

impl AddOp<i32> for i32 {
    type Output = i32;
    fn add(self, other: i32) -> i32 { self + other }
}

impl AddOp<f64> for f64 {
    type Output = f64;
    fn add(self, other: f64) -> f64 { self + other }
}

impl AddOp<String> for String {
    type Output = String;
    fn add(self, other: String) -> String {
        format!("{}{}", self, other)
    }
}
```

#### 戦略B: 名前マングリング

```rust
// Rust変換結果（名前マングリング）
fn add_i32_i32(a: i32, b: i32) -> i32 { a + b }
fn add_f64_f64(a: f64, b: f64) -> f64 { a + b }
fn add_String_String(a: String, b: String) -> String {
    format!("{}{}", a, b)
}

// ディスパッチャー（型推論で呼び分け）
macro_rules! add {
    ($a:expr, $b:expr) => {
        match (&$a, &$b) {
            (a, b) if ... => add_i32_i32(*a, *b),
            (a, b) if ... => add_f64_f64(*a, *b),
            (a, b) => add_String_String(a.clone(), b.clone()),
        }
    }
}
```

### 4.2 TypeScript への変換

TypeScriptはオーバーロードシグネチャをネイティブサポート：

```typescript
// TypeScript変換結果
function add(a: number, b: number): number;
function add(a: string, b: string): string;
function add(a: any, b: any): any {
    if (typeof a === 'number' && typeof b === 'number') {
        return a + b;
    }
    if (typeof a === 'string' && typeof b === 'string') {
        return a + b;
    }
    throw new Error('Invalid arguments');
}
```

### 4.3 WebAssembly への変換

WebAssemblyは名前マングリングを使用：

```wat
;; WebAssembly変換結果
(func $add_i32_i32 (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add)

(func $add_f64_f64 (param f64 f64) (result f64)
    local.get 0
    local.get 1
    f64.add)
```

## 5. コンストラクタのトランスパイル

### Rust への変換

```cm
// Cm
impl<T> Vec<T> {
    self() { /* ... */ }
    overload self(size_t cap) { /* ... */ }
    overload self(T fill, size_t count) { /* ... */ }
}
```

```rust
// Rust変換結果
impl<T> Vec<T> {
    pub fn new() -> Self { /* ... */ }
    pub fn with_capacity(cap: usize) -> Self { /* ... */ }
    pub fn from_fill(fill: T, count: usize) -> Self { /* ... */ }
}
```

### TypeScript への変換

```typescript
// TypeScript変換結果
class Vec<T> {
    constructor();
    constructor(cap: number);
    constructor(fill: T, count: number);
    constructor(...args: any[]) {
        if (args.length === 0) {
            // デフォルトコンストラクタ
        } else if (args.length === 1) {
            // 容量指定
        } else if (args.length === 2) {
            // fill値指定
        }
    }
}
```

## 6. 演算子オーバーロード

演算子は暗黙的にオーバーロード可能（`overload` 不要）：

```cm
struct Complex {
    double real;
    double imag;
}

// 演算子は暗黙的にoverload可能
Complex operator+(const Complex& a, const Complex& b) {
    return Complex{a.real + b.real, a.imag + b.imag};
}

// 異なる型との演算もOK
Complex operator+(const Complex& a, double b) {
    return Complex{a.real + b, a.imag};
}

Complex operator+(double a, const Complex& b) {
    return Complex{a + b.real, b.imag};
}
```

## 7. オーバーロード解決規則

### 優先順位（C++準拠）

1. **完全一致**: 型変換不要
2. **修飾子変換**: const/volatile の追加
3. **プロモーション**: int→long、float→double
4. **標準変換**: int→double、配列→ポインタ
5. **ユーザー定義変換**: コンストラクタ、変換演算子
6. **可変長引数**: 最も低い優先度

### 曖昧性エラー

```cm
overload void func(int a, double b) { }
overload void func(double a, int b) { }

func(1, 2);      // エラー：曖昧（両方とも1つの変換が必要）
func(1, 2.0);    // OK：最初の関数が選択
func(1.0, 2);    // OK：2番目の関数が選択
```

## 8. 実装詳細

### パーサー実装

```cpp
// parser.hpp
struct FunctionDecl {
    bool has_overload_modifier = false;
    std::string name;
    std::vector<Parameter> params;
    TypePtr return_type;
    BlockPtr body;
};

// parser.cpp
void Parser::parse_function() {
    bool is_overload = false;

    // overload修飾子のチェック
    if (check(TokenKind::KwOverload)) {
        is_overload = true;
        advance();
    }

    // 関数名の重複チェック
    if (!is_overload && symbol_table.has_function(name)) {
        error("Function '{}' already defined. Use 'overload' modifier", name);
    }

    // シグネチャの重複チェック
    if (is_overload) {
        auto sig = make_signature(params);
        if (symbol_table.has_exact_signature(name, sig)) {
            error("Function '{}' with same signature already defined", name);
        }
    }
}
```

### 名前マングリング

```cpp
// Cmシグネチャ → マングル名
std::string mangle_name(const std::string& name,
                       const std::vector<TypePtr>& param_types) {
    std::string mangled = "_Z" + std::to_string(name.length()) + name;
    for (const auto& type : param_types) {
        mangled += mangle_type(type);
    }
    return mangled;
}

// 例：
// add(int, int) -> _Z3addii
// add(double, double) -> _Z3adddd
// Vec<int>::push(int) -> _ZN3VecIiE4pushEi
```

## 9. エラー処理

### コンパイル時エラー

```cm
// エラー1: overload修飾子なし
void process(int x) { }
void process(double x) { }  // エラー: 'overload'が必要

// エラー2: 同一シグネチャ
overload void calc(int x) { }
overload void calc(int y) { }  // エラー: 同じシグネチャ

// エラー3: 戻り値の型だけ異なる
overload int get() { return 1; }
overload double get() { return 1.0; }  // エラー: パラメータが同じ
```

## 10. ベストプラクティス

### 推奨

```cm
// 明確な意図を持つオーバーロード
overload void print(int value);
overload void print(double value);
overload void print(string value);

// コンストラクタの適切な使い分け
impl File {
    self(string path);
    overload self(string path, string mode);
    overload self(int fd);
}
```

### 非推奨

```cm
// 過剰なオーバーロード（ジェネリクスを使うべき）
overload void process(int x);
overload void process(long x);
overload void process(short x);
overload void process(char x);

// 紛らわしいオーバーロード
overload void save(string data, bool compress);
overload void save(bool compress, string data);  // 混乱を招く
```

## まとめ

1. **`overload` 修飾子は必須**: 明示的な意図表明
2. **トランスパイラ対応**: 各言語の制約に応じた変換戦略
3. **型安全性**: コンパイル時の厳密なチェック
4. **C++互換の解決規則**: 予測可能な動作