[English](function_overloading_v2.en.html)

# Cm言語 関数オーバーロード設計 v2

## 問題：ターゲット言語の制約

### Rust
- **関数オーバーロード不可**
- トレイトとジェネリクスで解決
- 名前マングリングが必要

### TypeScript
- **オーバーロードシグネチャ**をサポート
- 実装は単一
- ユニオン型で代替可能

### WebAssembly
- **名前マングリング**必須
- 型情報は失われる

## 解決策：パラメータベースの自動判別

### オプション1: 自動オーバーロード解決（C++風）

```cm
// 同名関数を自動的に許可（パラメータが異なれば）
int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }
string add(string a, string b) { return a + b; }

// エラー：同じシグネチャ
int calculate(int x) { return x * 2; }
int calculate(int y) { return y * 3; }  // エラー！
```

**トランスパイル結果:**

```rust
// Rust: 名前マングリング
fn add_i32_i32(a: i32, b: i32) -> i32 { a + b }
fn add_f64_f64(a: f64, b: f64) -> f64 { a + b }
fn add_String_String(a: String, b: String) -> String {
    format!("{}{}", a, b)
}
```

```typescript
// TypeScript: オーバーロードシグネチャ
function add(a: number, b: number): number;
function add(a: string, b: string): string;
function add(a: any, b: any): any {
    return a + b;
}
```

### オプション2: 明示的な実装選択（Rust風トレイト）

```cm
// トレイトベースのオーバーロード
trait Add<T> {
    T add(T other);
}

impl Add<int> for int {
    int add(int other) {
        return this + other;
    }
}

impl Add<string> for string {
    string add(string other) {
        return this + other;
    }
}

// 使用
int x = 5.add(3);        // 8
string s = "a".add("b"); // "ab"
```

### オプション3: ジェネリック特殊化

```cm
// ジェネリック関数
<T> T add(T a, T b) {
    return a + b;  // デフォルト実装
}

// 特殊化（specializationキーワード）
specialization string add(string a, string b) {
    return a.concat(b);  // 文字列用の特別な実装
}

specialization Vec<T> add(Vec<T> a, Vec<T> b) {
    Vec<T> result = a;
    result.append(b);
    return result;
}
```

**トランスパイル結果:**

```rust
// Rust: ジェネリックとトレイト境界
fn add<T: std::ops::Add<Output = T>>(a: T, b: T) -> T {
    a + b
}

// 特殊化（現在はnightly機能）
impl Add for String {
    fn add(self, other: String) -> String {
        format!("{}{}", self, other)
    }
}
```

```typescript
// TypeScript: 条件型とオーバーロード
function add<T>(a: T, b: T): T;
function add(a: string, b: string): string;
function add(a: number, b: number): number;
function add<T>(a: T, b: T): T {
    if (typeof a === 'string') {
        return (a as any).concat(b);
    }
    return (a as any) + (b as any);
}
```

## 推奨：ハイブリッドアプローチ

### 1. デフォルト動作

```cm
// 異なるパラメータ型なら自動的に許可
int max(int a, int b) { return a > b ? a : b; }
double max(double a, double b) { return a > b ? a : b; }

// 同じシグネチャはエラー
void process(int x) { }
void process(int y) { }  // エラー！
```

### 2. コンストラクタ

```cm
impl<T> Vec<T> {
    // パラメータが異なれば自動的にオーバーロード
    self() { }
    self(size_t capacity) { }
    self(T fill, size_t count) { }
    self(const Vec<T>& other) { }
    self(Vec<T>&& other) { }
}
```

### 3. 演算子オーバーロード

```cm
// 演算子は自動的にオーバーロード可能
Complex operator+(Complex a, Complex b) { }
Complex operator+(Complex a, double b) { }
Complex operator+(double a, Complex b) { }
```

## トランスパイル戦略

### Rustへの変換

```cm
// Cm
void print(int x) { println(x); }
void print(string s) { println(s); }
void print(double d) { println(d); }
```

```rust
// Rust: トレイトベース
trait Print {
    fn print(&self);
}

impl Print for i32 {
    fn print(&self) { println!("{}", self); }
}

impl Print for String {
    fn print(&self) { println!("{}", self); }
}

impl Print for f64 {
    fn print(&self) { println!("{}", self); }
}

// または名前マングリング
fn print_i32(x: i32) { println!("{}", x); }
fn print_String(s: String) { println!("{}", s); }
fn print_f64(d: f64) { println!("{}", d); }
```

### TypeScriptへの変換

```cm
// Cm
<T> T identity(T x) { return x; }
int identity(int x) { return x * 2; }  // 特殊化
```

```typescript
// TypeScript
function identity<T>(x: T): T;
function identity(x: number): number;
function identity<T>(x: T): T {
    if (typeof x === 'number') {
        return (x * 2) as T;
    }
    return x;
}
```

## 名前解決規則

### 優先順位

1. **完全一致**: 型変換不要
2. **ユーザー定義変換**: コンストラクタ、変換演算子
3. **標準変換**: int→double、配列→ポインタ
4. **ジェネリック**: テンプレートインスタンス化

### 曖昧性の解決

```cm
// 曖昧性エラー
void func(int x, double y) { }
void func(double x, int y) { }

func(1, 2);  // エラー：どちらも変換が必要

// 解決策：明示的なキャスト
func(1, (double)2);    // OK: 最初の関数
func((double)1, 2);    // OK: 2番目の関数
```

## 実装例：print関数

```cm
// 様々な型に対するprint
void print(int x) {
    printf("%d", x);
}

void print(double x) {
    printf("%f", x);
}

void print(string s) {
    printf("%s", s.c_str());
}

void print(bool b) {
    printf("%s", b ? "true" : "false");
}

// ジェネリック版（フォールバック）
<T: Display> void print(T x) {
    cout << x;
}

// 可変長引数版
void print(auto... args) {
    ((cout << args << " "), ...);
}
```

## メリット

1. **自然な記述**: C++プログラマーに馴染みやすい
2. **型安全**: コンパイル時に解決
3. **互換性**: 各ターゲット言語の機能にマッピング可能
4. **最適化**: インライン化や特殊化が可能

## デメリットと対策

### 問題：Rustへのトランスパイル

**対策**:
- トレイトベースの実装に変換
- または名前マングリング

### 問題：コード膨張

**対策**:
- ジェネリック版を基本とし、特殊化は最小限に
- リンカーでの重複除去

### 問題：デバッグの複雑性

**対策**:
- マングル前の名前を保持
- ソースマップの生成

## まとめ

1. **基本方針**: パラメータ型が異なれば同名関数を許可
2. **`overload`キーワードは不要**: 自動判別
3. **トランスパイル**: 各言語の機能にマッピング
   - Rust: トレイト or 名前マングリング
   - TypeScript: オーバーロードシグネチャ
   - WASM: 名前マングリング