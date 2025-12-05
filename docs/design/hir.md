# HIR (High-level Intermediate Representation) 設計

## 概要

HIR（High-level Intermediate Representation）は、Cm言語コンパイラにおけるソースコードとバックエンドの間に位置する中間表現です。

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

```
// ソースコード
for (int i = 0; i < 10; i++) {
    println(i);
}

// HIR (脱糖後)
{
    let i: Int = 0;
    loop {
        if !(i < 10) { break; }
        println(i);
        i = i + 1;
    }
}
```

#### 例: メソッド呼び出しの脱糖

```
// ソースコード
obj.method(arg)

// HIR (脱糖後)
Type::method(obj, arg)
```

### 3. 名前解決済み

HIRでは、すべての識別子が完全に解決されています。

```
// ソースコード
import math;
int x = math.sqrt(4);

// HIR
let x: Int = ::math::sqrt(4: Int);
```

## HIRノードの設計

### 基本構造

```cpp
// HIRノードの基底
struct HirNode {
    Span span;        // ソース位置情報
    Type type;        // 型情報
};

// 式ノードの基底
struct HirExpr : HirNode {
    // 式は必ず型を持つ
};

// 文ノードの基底
struct HirStmt : HirNode {
    // 文は型を持たない（Unit型）
};
```

### 式ノード

```cpp
// リテラル
struct HirLiteral : HirExpr {
    enum Kind { Int, Float, String, Bool, Char };
    Kind kind;
    std::variant<int64_t, double, std::string, bool, char> value;
};

// 変数参照
struct HirVarRef : HirExpr {
    std::string name;
    // 型はHirExpr::typeから取得
};

// 二項演算
struct HirBinaryExpr : HirExpr {
    enum Op { Add, Sub, Mul, Div, Mod, /* ... */ };
    Op op;
    std::unique_ptr<HirExpr> lhs;
    std::unique_ptr<HirExpr> rhs;
};

// 関数呼び出し
struct HirCall : HirExpr {
    std::string func_name;  // 完全修飾名
    std::vector<std::unique_ptr<HirExpr>> args;
};

// フィールドアクセス
struct HirFieldAccess : HirExpr {
    std::unique_ptr<HirExpr> base;
    std::string field_name;
    size_t field_index;  // 構造体内でのフィールドインデックス
};
```

### 文ノード

```cpp
// 変数宣言
struct HirLet : HirStmt {
    std::string name;
    Type declared_type;
    std::unique_ptr<HirExpr> init;  // nullable
};

// 代入
struct HirAssign : HirStmt {
    std::unique_ptr<HirExpr> target;  // 左辺値
    std::unique_ptr<HirExpr> value;
};

// ブロック
struct HirBlock : HirStmt {
    std::vector<std::unique_ptr<HirStmt>> stmts;
    std::unique_ptr<HirExpr> expr;  // 最後の式（あれば）
};

// 条件分岐
struct HirIf : HirStmt {
    std::unique_ptr<HirExpr> cond;
    std::unique_ptr<HirBlock> then_block;
    std::unique_ptr<HirBlock> else_block;  // nullable
};

// ループ
struct HirLoop : HirStmt {
    std::unique_ptr<HirBlock> body;
};

// break/continue
struct HirBreak : HirStmt {};
struct HirContinue : HirStmt {};

// return
struct HirReturn : HirStmt {
    std::unique_ptr<HirExpr> value;  // nullable
};
```

### トップレベル項目

```cpp
// 関数定義
struct HirFunction {
    std::string name;
    std::vector<std::pair<std::string, Type>> params;
    Type return_type;
    std::unique_ptr<HirBlock> body;
    bool is_async;
};

// 構造体定義
struct HirStruct {
    std::string name;
    std::vector<std::pair<std::string, Type>> fields;
};

// インターフェース定義
struct HirInterface {
    std::string name;
    std::vector<HirFunctionSignature> methods;
};

// 実装
struct HirImpl {
    std::string target_type;
    std::optional<std::string> interface_name;
    std::vector<HirFunction> methods;
};
```

## AST → HIR 変換（Lowering）

### 変換プロセス

1. **名前解決**: 識別子を完全修飾名に解決
2. **型推論**: 型を推論し、すべてのノードに付与
3. **脱糖**: 糖衣構文を基本形式に変換
4. **検証**: HIRレベルでの整合性検証

### 変換例

```cpp
// AST: ForStmt
struct AstForStmt {
    AstStmt* init;
    AstExpr* cond;
    AstStmt* update;
    AstBlock* body;
};

// 変換後: HirBlock + HirLoop
HirBlock {
    stmts: [
        lower(init),          // 初期化
        HirLoop {
            body: HirBlock {
                stmts: [
                    HirIf {           // 条件チェック
                        cond: !lower(cond),
                        then_block: HirBlock {
                            stmts: [HirBreak]
                        }
                    },
                    ...lower(body.stmts),  // 本体
                    lower(update)          // 更新
                ]
            }
        }
    ]
}
```

## HIRの利点

1. **シンプルなインタープリター**
   - 糖衣構文を扱う必要がない
   - 型情報が利用可能

2. **最適化の機会**
   - 定数畳み込み
   - 不要コード除去
   - インライン展開

3. **より良いエラーメッセージ**
   - 型情報を使った詳細なエラー
   - ソース位置の追跡

4. **将来の拡張性**
   - 複数バックエンドへの対応
   - コード生成への移行

## 参考資料

### コンパイラの中間表現

- [Rust HIR](https://rustc-dev-guide.rust-lang.org/hir.html) - Rustコンパイラの高レベル中間表現
- [Swift SIL](https://github.com/apple/swift/blob/main/docs/SIL.rst) - Swift Intermediate Language
- [LLVM IR](https://llvm.org/docs/LangRef.html) - 低レベル中間表現の標準的な参考

### 学術資料

- "Engineering a Compiler" by Keith Cooper and Linda Torczon - コンパイラ設計の教科書
- "Modern Compiler Implementation" by Andrew Appel - 中間表現の設計について詳述
