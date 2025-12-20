# HIR (High-level Intermediate Representation) 設計

## 概要

HIR（High-level Intermediate Representation）は、Cm言語コンパイラにおけるソースコードとバックエンドの間に位置する中間表現です。

**設計原則**: Rust/TypeScript両方にトランスパイル可能な「共通部分集合」で表現する。

## マルチターゲット対応

### Rust/TypeScript共通表現

```
┌─────────────────────────────────────────────────────────────┐
│ HIR = Rust ∩ TypeScript の共通表現                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ ✅ 共通概念（HIRで直接表現）                                 │
│   - 構造体                                                  │
│   - ジェネリクス                                            │
│   - interface/trait                                         │
│   - async/await                                             │
│   - 列挙型（タグ付きユニオン）                               │
│                                                             │
│ ⚠️ 差異がある概念（バックエンドで変換）                      │
│   - Option<T> → Rust: Option<T> / TS: T | null             │
│   - Result<T,E> → Rust: Result<T,E> / TS: T | Error        │
│   - パターンマッチ → Rust: match / TS: switch + 型ガード   │
│                                                             │
│ 🔧 Rust専用（ヒントとして保持、TSでは無視）                  │
│   - 所有権/借用                                             │
│   - ライフタイム                                            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 変換例 (Cm → Rust/TS)

```cpp
// Cm (C++風構文)
Option<String> process(Option<String> data) {
    match (data) {
        Some(s) => return Some(s.length());
        None => return None;
    }
}

// → Rust
fn process(data: Option<String>) -> Option<i64> {
    match data {
        Some(s) => Some(s.len() as i64),
        None => None,
    }
}

// → TypeScript
function process(data: string | null): number | null {
    if (data !== null) {
        return data.length;
    } else {
        return null;
    }
}
```

## HIRの特徴

### 1. 完全な型情報

HIRのすべての式・文には型情報が付与されています。

```
// AST (型情報なし)
BinaryExpr {
    op: Add,
    left: Ident("x"),
    right: Literal(1)
}

// HIR (型情報あり)
HirBinaryExpr {
    op: Add,
    left: HirIdent { name: "x", ty: Int },
    right: HirLiteral { value: 1, ty: Int },
    ty: Int
}
```

### 2. 脱糖（Desugaring）

HIRでは糖衣構文が基本形式に変換されます。

#### 例: for文の脱糖

```cpp
// Cm ソースコード
for (int i = 0; i < 10; i++) {
    println(i);
}

// HIR (脱糖後)
{
    int i = 0;
    loop {
        if (!(i < 10)) { break; }
        println(i);
        i = i + 1;
    }
}
```

#### 例: メソッド呼び出しの脱糖

```cpp
// Cm ソースコード
obj.method(arg)

// HIR (脱糖後)
Type::method(obj, arg)
```

### 3. 名前解決済み

HIRでは、すべての識別子が完全に解決されています。

```cpp
// Cm ソースコード
import math;
int x = math::sqrt(4);

// HIR
int x = ::math::sqrt(4: Int);
```

## HIRノードの設計

### 基本構造（C++20）

```cpp
// 型定義（マルチターゲット対応）
struct HirType {
    enum Kind {
        Primitive,    // int, float, bool, string
        Struct,       // struct
        Enum,         // enum (タグ付きユニオン)
        Generic,      // T, U
        Function,     // (A) -> B
        Optional,     // Option<T> → Rust: Option / TS: T | null
        Result,       // Result<T, E> → Rust: Result / TS: T | Error
        Reference,    // &T, &mut T (Rust向けヒント)
    };
    Kind kind;
    std::vector<HirType> params;  // ジェネリクスパラメータ
    
    // Rust向けヒント（TSでは無視）
    std::optional<Mutability> mutability;
    std::optional<Lifetime> lifetime;
};

// HIRノードの基底
struct HirNode {
    Span span;        // ソース位置情報
    HirType type;     // 型情報
};
```

### 式ノード

```cpp
// リテラル
struct HirLiteral : HirNode {
    enum Kind { Int, Float, String, Bool, Char };
    Kind kind;
    std::variant<int64_t, double, std::string, bool, char> value;
};

// 変数参照
struct HirVarRef : HirNode {
    std::string name;
};

// 二項演算
struct HirBinaryExpr : HirNode {
    enum Op { Add, Sub, Mul, Div, Mod, /* ... */ };
    Op op;
    std::unique_ptr<HirExpr> lhs;
    std::unique_ptr<HirExpr> rhs;
};

// 関数呼び出し
struct HirCall : HirNode {
    std::string func_name;  // 完全修飾名
    std::vector<std::unique_ptr<HirExpr>> args;
};

// パターンマッチ（Rust: match, TS: switch変換）
struct HirMatch : HirNode {
    std::unique_ptr<HirExpr> scrutinee;
    std::vector<HirMatchArm> arms;
};
```

### 文ノード

```cpp
// 変数宣言（C++風）
struct HirLet : HirStmt {
    std::string name;
    HirType declared_type;  // int x = ... の int 部分
    std::unique_ptr<HirExpr> init;
    bool is_const;  // const vs 通常変数
};

// 関数定義（C++風）
struct HirFunction {
    HirType return_type;        // 戻り値型が先
    std::string name;
    std::vector<HirParam> params;
    std::unique_ptr<HirBlock> body;
    bool is_async;
    Visibility visibility;
    std::vector<GenericParam> generics;
};
```

### トップレベル項目

```cpp
// 構造体定義（C++風）
struct HirStruct {
    std::string name;
    std::vector<HirField> fields;  // Type name; 形式
    std::vector<GenericParam> generics;
};

// インターフェース定義
struct HirInterface {
    std::string name;
    std::vector<HirFunctionSignature> methods;
    std::vector<GenericParam> generics;
};

// 実装
struct HirImpl {
    std::string target_type;
    std::optional<std::string> interface_name;
    std::vector<HirFunction> methods;
};
```

## Cm/Cb 構文例

```cpp
// 関数定義（C++風：戻り値型が先）
int add(int a, int b) {
    return a + b;
}

// ジェネリクス
T identity<T>(T value) {
    return value;
}

// 構造体
struct Point {
    int x;
    int y;
};

// インターフェースと実装
interface Printable {
    void print();
};

impl Printable for Point {
    void print() {
        println("(${this.x}, ${this.y})");
    }
};

// async/await
async String fetchData(String url) {
    Response res = await http::get(url);
    return res.body;
}

// パターンマッチ
int getValue(Option<int> opt) {
    match (opt) {
        Some(v) => return v;
        None => return 0;
    }
}
```

## バックエンド出力

### Rust出力

```cpp
class RustEmitter {
public:
    // int add(int a, int b) → fn add(a: i64, b: i64) -> i64
    void emit(const HirFunction& func);
    void emit(const HirStruct& s);
    void emit(const HirInterface& i);  // → trait
};
```

### TypeScript出力

```cpp
class TypeScriptEmitter {
public:
    // int add(int a, int b) → function add(a: number, b: number): number
    void emit(const HirFunction& func);
    void emit(const HirStruct& s);      // → interface
    void emit(const HirInterface& i);   // → interface
};
```

## 参考資料

- [Rust HIR](https://rustc-dev-guide.rust-lang.org/hir.html)
- [TypeScript AST](https://ts-ast-viewer.com/)
- "Engineering a Compiler" by Keith Cooper and Linda Torczon
