# インライン化とマクロの違い

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 技術解説

## エグゼクティブサマリー

**インライン化とマクロは全く異なる概念です。** インライン化は「コンパイラの最適化技術」であり、マクロは「コード生成/変換メカニズム」です。この文書では、両者の本質的な違いを明確に説明します。

## 概念の比較表

| 特性 | インライン化 | C++マクロ | Rustマクロ |
|------|------------|-----------|------------|
| **処理タイミング** | コンパイル時（最適化フェーズ） | プリプロセス時 | コンパイル時（構文解析後） |
| **対象** | 関数 | 任意のテキスト | ASTノード |
| **型安全性** | ✅ 完全に型安全 | ❌ 型チェックなし | ✅ 型安全 |
| **デバッグ** | 可能（デバッグ情報保持） | 困難（元コード消失） | 可能（展開追跡可） |
| **スコープ** | 通常のスコープ規則 | スコープ無視 | 衛生的スコープ |
| **目的** | パフォーマンス最適化 | コード置換 | コード生成 |

## 1. インライン化（Inline Expansion）

### 定義
インライン化は、**関数呼び出しを関数本体で置き換える最適化技術**です。

### 動作原理

```cpp
// 元のコード
inline int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}

// インライン化後（コンパイラが内部的に変換）
int main() {
    int result = 3 + 4;  // 関数呼び出しが展開された
    return result;
}
```

### 特徴
1. **型安全性を保持**
   - 関数の型チェックは通常通り実行
   - 引数の型変換も適切に処理

2. **最適化の一種**
   - コンパイラが判断（ヒントは与えられる）
   - 常に展開されるとは限らない

3. **デバッグ情報を保持**
   - スタックトレースに元の関数名が表示可能
   - ブレークポイントも設定可能

### 各言語でのインライン化

#### C++
```cpp
// 明示的なインライン指定
inline int square(int x) { return x * x; }

// 強制インライン（コンパイラ依存）
__forceinline int abs(int x) { return x < 0 ? -x : x; }

// クラスメンバ関数は暗黙的にinline
class Point {
    int x, y;
public:
    int getX() const { return x; }  // 暗黙的にinline
};
```

#### Rust
```rust
// インライン化ヒント
#[inline]
fn add(a: i32, b: i32) -> i32 {
    a + b
}

// 強制インライン化
#[inline(always)]
fn max(a: i32, b: i32) -> i32 {
    if a > b { a } else { b }
}

// インライン化禁止
#[inline(never)]
fn debug_log(msg: &str) {
    println!("DEBUG: {}", msg);
}
```

## 2. C++のマクロ（プリプロセッサマクロ）

### 定義
C++のマクロは、**プリプロセッサによるテキスト置換**です。

### 動作原理

```cpp
// マクロ定義
#define ADD(a, b) ((a) + (b))
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int main() {
    int result = ADD(3, 4);        // テキスト置換
    int sq = SQUARE(2 + 3);        // 危険！
    int m = MAX(i++, j++);         // 非常に危険！
    return result;
}

// プリプロセッサ後（実際にコンパイラが見るコード）
int main() {
    int result = ((3) + (4));
    int sq = ((2 + 3) * (2 + 3));  // 5 * 5 = 25（期待通り）
    int m = ((i++) > (j++) ? (i++) : (j++));  // 二重インクリメント！
    return result;
}
```

### 問題点

1. **型安全性なし**
```cpp
#define ADD(a, b) ((a) + (b))

ADD("hello", "world");  // コンパイルは通るが意味不明
ADD(ptr, 5);           // ポインタ演算になる
```

2. **副作用の重複**
```cpp
#define SQUARE(x) ((x) * (x))

int i = 5;
int result = SQUARE(i++);  // (i++) * (i++) → 未定義動作！
```

3. **デバッグ困難**
```cpp
#define COMPLEX_MACRO(x) \
    do { \
        if (x > 0) { \
            process(x); \
        } \
    } while(0)

// エラーが発生してもマクロ展開後の行番号しか分からない
```

4. **スコープ問題**
```cpp
#define SWAP(a, b) { int temp = a; a = b; b = temp; }

// tempという変数名が衝突する可能性
```

## 3. Rustのマクロ（構文マクロ）

### 定義
Rustのマクロは、**コンパイル時にASTレベルでコードを生成する仕組み**です。

### 3.1 宣言的マクロ（macro_rules!）

```rust
// マクロ定義
macro_rules! add {
    ($a:expr, $b:expr) => {
        $a + $b
    };
}

macro_rules! vec_init {
    ( $( $x:expr ),* ) => {
        {
            let mut temp_vec = Vec::new();
            $(
                temp_vec.push($x);
            )*
            temp_vec
        }
    };
}

fn main() {
    let result = add!(3, 4);  // マクロ呼び出し
    let v = vec_init![1, 2, 3, 4, 5];
}
```

### 3.2 手続き的マクロ（Procedural Macros）

```rust
// derive マクロ
#[derive(Debug, Clone, PartialEq)]
struct Point {
    x: i32,
    y: i32,
}

// 属性マクロ
#[tokio::main]
async fn main() {
    // 非同期ランタイムのセットアップコードが自動生成
}

// 関数風マクロ
println!("Hello, {}!", name);  // フォーマット文字列の検証付き
```

### Rustマクロの特徴

1. **衛生的（Hygienic）**
```rust
macro_rules! using_temp {
    ($val:expr) => {
        {
            let temp = $val;  // tempは外部と衝突しない
            temp * 2
        }
    };
}

let temp = 10;
let result = using_temp!(5);  // 問題なし
println!("{}", temp);  // 10のまま
```

2. **型安全**
```rust
macro_rules! typed_add {
    ($a:expr, $b:expr) => {
        {
            let a: i32 = $a;  // 型を強制
            let b: i32 = $b;
            a + b
        }
    };
}

// typed_add!("hello", "world");  // コンパイルエラー！
```

3. **パターンマッチング**
```rust
macro_rules! match_type {
    (int $name:ident) => { let $name: i32 = 0; };
    (string $name:ident) => { let $name: String = String::new(); };
    (bool $name:ident) => { let $name: bool = false; };
}

match_type!(int x);     // let x: i32 = 0;
match_type!(string s);  // let s: String = String::new();
```

## 4. インライン化 vs マクロ

### 使用例での比較

#### タスク: 2つの値の最大値を求める

**C++ インライン関数**
```cpp
template<typename T>
inline T max(T a, T b) {
    return a > b ? a : b;
}

int result = max(x++, y++);  // x++とy++は各1回評価
```

**C++ マクロ**
```cpp
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int result = MAX(x++, y++);  // 危険！複数回評価される
```

**Rust インライン関数**
```rust
#[inline]
fn max<T: Ord>(a: T, b: T) -> T {
    if a > b { a } else { b }
}

let result = max(x, y);  // 安全で最適化される
```

**Rust マクロ**
```rust
macro_rules! max {
    ($a:expr, $b:expr) => {
        {
            let a_val = $a;  // 一度だけ評価
            let b_val = $b;
            if a_val > b_val { a_val } else { b_val }
        }
    };
}

let result = max!(x + 1, y + 1);  // 安全
```

## 5. いつどちらを使うべきか

### インライン化を使うべき場合

1. **パフォーマンス最適化**
```cpp
// 小さく頻繁に呼ばれる関数
inline int abs(int x) {
    return x < 0 ? -x : x;
}
```

2. **テンプレート関数**
```cpp
template<typename T>
inline T min(T a, T b) {
    return a < b ? a : b;
}
```

3. **アクセサメソッド**
```cpp
class Rectangle {
    int width, height;
public:
    inline int getWidth() const { return width; }
    inline int getHeight() const { return height; }
};
```

### マクロを使うべき場合

#### C++の場合（なるべく避ける）

1. **条件コンパイル**
```cpp
#ifdef DEBUG
    #define LOG(msg) std::cerr << msg << std::endl
#else
    #define LOG(msg)
#endif
```

2. **定数定義**（C++11以前）
```cpp
#define PI 3.14159265359
#define MAX_BUFFER_SIZE 1024
```

#### Rustの場合

1. **可変長引数**
```rust
macro_rules! print_all {
    ($($arg:expr),*) => {
        $(
            println!("{}", $arg);
        )*
    };
}

print_all!(1, 2, 3, "hello", true);
```

2. **DSL（ドメイン特化言語）**
```rust
macro_rules! html {
    (<$tag:ident> $($content:tt)* </$tag2:ident>) => {
        // HTMLを生成
    };
}

let page = html! {
    <div>
        <h1>Title</h1>
        <p>Content</p>
    </div>
};
```

3. **コード生成**
```rust
macro_rules! create_function {
    ($func_name:ident) => {
        fn $func_name() {
            println!("Function {} was called", stringify!($func_name));
        }
    };
}

create_function!(foo);
create_function!(bar);
```

## 6. Cm言語での提案

### インライン化（実装提案済み）
```cm
#[inline]
int add(int a, int b) {
    return a + b;
}

#[inline(always)]
int square(int x) {
    return x * x;
}
```

### マクロ（将来的な可能性）
```cm
// Rust風の安全なマクロ
macro_rules! assert {
    ($cond:expr, $msg:expr) => {
        if (!$cond) {
            panic($msg);
        }
    };
}

// 使用例
assert!(x > 0, "x must be positive");
```

## まとめ

### インライン化
- **目的**: パフォーマンス最適化
- **対象**: 関数
- **安全性**: 型安全
- **タイミング**: コンパイル時の最適化フェーズ
- **制御**: コンパイラへのヒント

### C++マクロ
- **目的**: テキスト置換
- **対象**: 任意のテキスト
- **安全性**: 危険（型チェックなし）
- **タイミング**: プリプロセス時
- **制御**: 必ず展開

### Rustマクロ
- **目的**: コード生成
- **対象**: AST（構文木）
- **安全性**: 型安全・衛生的
- **タイミング**: コンパイル時
- **制御**: パターンベース

**結論**: インライン化とマクロは異なる目的のための異なるツールです。インライン化は最適化のため、マクロはメタプログラミングのための機能です。

---

**作成者:** Claude Code
**ステータス:** 技術解説
**関連文書:** 040_inline_expansion_design.md