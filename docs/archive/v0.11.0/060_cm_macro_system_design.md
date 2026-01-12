# Cm言語マクロシステム設計書（完全Cm構文統一版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 最終設計v3

## エグゼクティブサマリー

Cm言語のマクロシステムを**完全にCm構文と統一**します。`@`プレフィックスを排除し、既存のAST構造にシームレスに統合することで、学習コストゼロの自然なマクロシステムを実現します。

## 1. 設計理念

### 1.1 基本方針

1. **完全なCm構文統一**: `@`などの特殊記法を排除
2. **AST完全統合**: 既存のAST構造をそのまま使用
3. **段階的型チェック**: 展開後に通常の型チェック
4. **ゼロ学習コスト**: 新しい構文なし

### 1.2 構文の完全一貫性

| 要素 | 通常の関数 | マクロ |
|------|-----------|--------|
| 定義 | `int add(int a, int b)` | `macro add(int a, int b)` |
| ジェネリック | `T max<T>(T a, T b)` | `macro max<T>(T a, T b)` |
| 制御構造 | `if`, `for`, `while` | **同じ** `if`, `for`, `while` |
| 型操作 | N/A | `typeof()`, `sizeof()` |

## 2. マクロ定義構文（純粋なCm）

### 2.1 基本構造

```cm
// マクロ定義：完全にCm構文
macro vec() {
    return Vector<int>();
}

macro vec(int first) {
    Vector<int> v;
    v.push(first);
    return v;
}

// 可変長引数版（通常のfor文）
macro vec(int first, int... rest) {
    Vector<int> v;
    v.push(first);
    for (int r : rest) {  // 通常のfor文！
        v.push(r);
    }
    return v;
}
```

### 2.2 ジェネリックマクロ

```cm
// ジェネリックマクロも自然な構文
macro min<T>(T a, T b) {
    return (a < b) ? a : b;
}

macro create_vector<T>(T first, T... rest) {
    Vector<T> v;
    v.push(first);
    for (T element : rest) {  // 通常のfor文
        v.push(element);
    }
    return v;
}
```

## 3. 特殊な要素は組み込み関数のみ

### 3.1 コンパイル時組み込み関数

```cm
// 必要最小限の組み込み関数
stringify(expr)       // 式を文字列化
typeof(expr)          // 型情報取得
sizeof(type)          // サイズ取得
type_name<T>()        // 型名取得
is_same_type<T, U>() // 型比較
```

### 3.2 実例

```cm
// assertマクロ（純粋なCm構文）
macro assert(bool condition, string message = "") {
    if (message == "") {  // 通常のif文
        if (!condition) {
            fprintf(stderr, "Assertion failed: %s at %s:%d\n",
                   stringify(condition),  // 組み込み関数
                   __FILE__, __LINE__);
            abort();
        }
    } else {
        if (!condition) {
            fprintf(stderr, "Assertion failed: %s\n  Message: %s at %s:%d\n",
                   stringify(condition), message,
                   __FILE__, __LINE__);
            abort();
        }
    }
}
```

### 3.3 型に基づく条件分岐

```cm
// デバッグ出力（型判定も通常のif）
macro debug<T>(string name, T value) {
    fprintf(stderr, "[DEBUG] %s = ", name);

    // コンパイル時型判定（組み込み関数使用）
    if (is_same_type<T, int>()) {
        fprintf(stderr, "%d\n", value);
    }
    else if (is_same_type<T, float>()) {
        fprintf(stderr, "%f\n", value);
    }
    else if (is_same_type<T, string>()) {
        fprintf(stderr, "%s\n", value);
    }
    else {
        fprintf(stderr, "<%s>\n", type_name<T>());
    }
}
```

## 4. AST統合設計

### 4.1 ASTノード定義

```cpp
// AST定義（既存構造を拡張）
class ASTNode {
    enum NodeType {
        FUNCTION_DECL,
        STRUCT_DECL,
        MACRO_DECL,      // マクロ定義（新規）
        MACRO_CALL,      // マクロ呼び出し（新規）
        FUNCTION_CALL,
        // ...
    };
};

// マクロ定義ノード（関数定義とほぼ同じ）
class MacroDecl : public ASTNode {
    string name;
    vector<GenericParam> type_params;
    vector<Parameter> params;
    BlockStatement body;  // 通常のAST！
};

// マクロ呼び出しノード
class MacroCall : public ASTNode {
    string name;
    vector<Type> type_args;
    vector<Expression> args;
    SourceLocation call_site;  // エラー報告用
};
```

### 4.2 コンパイルパイプライン

```
ソースコード
    ↓
[Lexer] → [Parser] → AST（マクロ含む）
    ↓
[マクロ収集]  ← Pass 1: マクロ定義を登録
    ↓
[マクロ展開]  ← Pass 2: 呼び出しを展開
    ↓
[型解決・HIR生成]
    ↓
[型チェック]  ← 展開後の通常の型チェック
    ↓
[MIR生成]
```

### 4.3 展開タイミングの詳細

```cpp
class CompilerPipeline {
    void compile(SourceFile* file) {
        // 1. パース（マクロも通常のASTとして）
        AST* ast = parser.parse(file);

        // 2. マクロ収集フェーズ
        MacroRegistry registry;
        for (auto node : ast->top_level_decls) {
            if (node->type == MACRO_DECL) {
                registry.register_macro(node);
            }
        }

        // 3. マクロ展開フェーズ
        MacroExpander expander(registry);
        ast = expander.expand_all(ast);

        // 4. 通常の型チェック（展開済みAST）
        TypeChecker checker;
        checker.check(ast);

        // 5. HIR/MIR生成
        auto hir = generate_hir(ast);
        auto mir = generate_mir(hir);
    }
};
```

## 5. マクロ展開の実装

### 5.1 展開器の設計

```cpp
class MacroExpander {
    MacroRegistry& registry;

public:
    AST* expand_all(AST* ast) {
        // トップダウンで展開
        return expand_node(ast);
    }

private:
    ASTNode* expand_node(ASTNode* node) {
        if (node->type == MACRO_CALL) {
            return expand_macro_call(node);
        }

        // 子ノードを再帰的に展開
        for (auto& child : node->children) {
            child = expand_node(child);
        }
        return node;
    }

    ASTNode* expand_macro_call(MacroCall* call) {
        // 1. マクロ定義を取得
        auto macro = registry.find(call->name);

        // 2. 型推論（必要なら）
        auto type_args = infer_type_args(call, macro);

        // 3. マクロ本体をインスタンス化
        auto instantiated = instantiate_macro(
            macro,
            type_args,
            call->args
        );

        // 4. 展開結果を返す（通常のAST）
        return instantiated;
    }
};
```

### 5.2 型推論

```cpp
// 引数から型パラメータを推論
vector<Type> infer_type_args(MacroCall* call, MacroDecl* macro) {
    if (macro->type_params.empty()) {
        return {};  // ジェネリックでない
    }

    // 引数の型から推論
    TypeInferer inferer;
    for (int i = 0; i < call->args.size(); i++) {
        auto arg_type = get_type_of_expr(call->args[i]);
        auto param_type = macro->params[i].type;
        inferer.unify(param_type, arg_type);
    }

    return inferer.get_substitution();
}
```

## 6. 実例：標準マクロライブラリ

### 6.1 基本マクロ（純粋なCm構文）

```cm
// std/macros.cm

// 最小値（通常の三項演算子）
macro min<T>(T a, T b) {
    return (a < b) ? a : b;
}

// スワップ（通常の代入文）
macro swap<T>(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// ベクター初期化（通常のfor文）
macro vec_of<T>(T... elements) {
    Vector<T> v;
    for (T elem : elements) {  // 純粋なCm構文
        v.push(elem);
    }
    return v;
}
```

### 6.2 Pin関連マクロ

```cm
// Pinマクロ（特殊構文なし）
macro pin<T>(T& value) {
    Pin<T> pinned(value);
    return pinned;
}

macro pin_box<T>(T value) {
    Box<T> boxed = Box<T>::new(value);
    Pin<Box<T>> pinned(boxed);
    return pinned;
}
```

## 7. エラー処理とデバッグ

### 7.1 エラー位置の保持

```cpp
// 展開時に元の位置情報を保持
struct ExpandedNode {
    ASTNode* node;
    SourceLocation original_location;  // マクロ呼び出し位置
    SourceLocation expanded_location;  // 展開後の位置
};
```

### 7.2 エラーメッセージ

```
error: type mismatch in expanded macro 'vec_of'
  --> main.cm:10:15
   |
10 | Vector<int> v = vec_of(1, 2.0, 3);
   |                        ^   ^^^ float
   |                        int
   |
   = note: in expansion of macro 'vec_of'
   = note: all elements must have the same type T
```

## 8. 利点

### 8.1 実装の簡潔性

- パーサーの変更最小限（`macro`キーワード追加のみ）
- 既存のAST構造をそのまま使用
- 型チェッカーの変更不要

### 8.2 ユーザビリティ

- **学習コストゼロ**: 新しい構文なし
- **デバッグが容易**: 通常のコードと同じ
- **IDEサポート**: 既存のツールが使える

### 8.3 保守性

- 特殊な`@`ディレクティブなし
- マクロも通常の関数と同じ扱い
- テストが書きやすい

## 9. 実装ロードマップ

### Phase 1: 基本実装
1. `macro`キーワードの認識
2. MacroDecl/MacroCall ASTノード
3. 単純な展開

### Phase 2: 型システム統合
1. 型推論
2. ジェネリックマクロ
3. 型安全性の保証

### Phase 3: 組み込み関数
1. `stringify()`, `typeof()`
2. `type_name<T>()`, `is_same_type<T,U>()`
3. コンパイル時評価

## まとめ

この設計により：

1. **完全なCm構文統一**: 特殊記法ゼロ
2. **AST完全統合**: 既存インフラを100%活用
3. **型安全性**: 展開後の通常の型チェック
4. **学習コストゼロ**: Cm構文がそのまま使える
5. **実装が簡潔**: パーサーの変更最小限

特殊な構文を一切導入せず、**純粋なCm構文だけで強力なマクロシステム**を実現します。

---

**作成者:** Claude Code
**ステータス:** 最終設計v3
**主な変更:** `@`プレフィックス完全排除、AST統合詳細追加