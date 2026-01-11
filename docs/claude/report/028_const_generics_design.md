# 定数ジェネリックパラメータ（Const Generics）設計

作成日: 2026-01-11
関連文書: 027_iterator_required_language_features.md

## 概要

定数ジェネリックパラメータは、コンパイル時定数を型パラメータとして扱う機能です。
固定長配列への`Iterable`実装や、コンパイル時サイズ検証に必要です。

## 構文設計

### 基本構文

```cm
// 定数パラメータの宣言（新構文）
struct Array<T, SIZE: const int> {
    T[SIZE] data;
}

// 関数での使用
<T, N: const int> T[N] create_array() { ... }

// implでの使用
impl<T, N: const int> T[N] for Iterable<T> { ... }
```

> [!IMPORTANT]
> **制約**: `T[N]`の`N`は必ずジェネリクスの定数パラメータでなければなりません。
> グローバル変数やローカル変数を使用した`T[n]`は不正です。

### 不正な例

```cm
// ❌ 不正: グローバル変数は使用不可
const int SIZE = 10;
impl T[SIZE] for Iterable<T> { ... }  // エラー

// ❌ 不正: ローカル変数は使用不可
int n = get_size();
int[n] arr;  // エラー（VLA）

// ✅ 正しい: const genericパラメータを使用
impl<T, N: const int> T[N] for Iterable<T> { ... }
```
    
    // Type: 制約リスト
    std::vector<std::string> constraints;
    
    // Const: 定数の型
    TypePtr const_type;  // int, uint, bool等
};

std::vector<GenericParam> generic_params;
```

## パーサー変更

### src/frontend/parser/parser.hpp

```cpp
// parse_generic_params()の拡張
GenericParam parse_generic_param() {
    GenericParam param;
    
    if (consume_if(TokenKind::KwConst)) {
        // const N: int
        param.kind = GenericParam::Kind::Const;
        param.name = expect(TokenKind::Identifier).text;
        expect(TokenKind::Colon);
        param.const_type = parse_type();
    } else {
        // T または T: Trait
        param.kind = GenericParam::Kind::Type;
        param.name = expect(TokenKind::Identifier).text;
        if (consume_if(TokenKind::Colon)) {
            param.constraints = parse_type_constraints();
        }
    }
    
    return param;
}
```

## 型チェッカー変更

### 定数式の評価

```cpp
// src/frontend/types/checking/expr.cpp
struct ConstExpr {
    TypePtr type;
    std::variant<int64_t, bool, char> value;
};

ConstExpr evaluate_const_expr(ast::Expr& expr) {
    // 定数畳み込み
    if (auto* lit = expr.as<ast::IntLiteral>()) {
        return {ast::make_int(), lit->value};
    }
    // ... 他の定数式
}
```

### ジェネリック具体化時のサイズ解決

```cpp
// impl<T, const N: int> T[N] for Iterable<T>
//                  ↓ 具体化
// impl int[5] for Iterable<int>

void instantiate_generic(const GenericParam& param, const TypeArg& arg) {
    if (param.kind == GenericParam::Kind::Const) {
        // Nを5に置換
        const_substitutions_[param.name] = evaluate_const_expr(arg);
    }
}
```

## HIR/MIR変更

### 定数パラメータの伝播

```cpp
// src/hir/nodes.hpp
struct HirGenericParam {
    enum class Kind { Type, Const };
    Kind kind;
    std::string name;
    TypePtr const_type;  // Constの場合
};
```

## コード生成

### LLVM

定数パラメータはコンパイル時に解決されるため、
LLVMレベルでは具体的な値として生成：

```llvm
; Array<int, 5>
%Array_int_5 = type { [5 x i32] }
```

## 使用例

### 固定長配列のイテレータ

```cm
impl<T, const N: int> T[N] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>(&self[0], N);
    }
}

// 使用
int[5] arr = [1, 2, 3, 4, 5];
for (int x in arr.iter()) {
    println("{x}");
}
```

### スタック配列

```cm
struct StackArray<T, const N: int> {
    T[N] data;
    int len;
}

impl<T, const N: int> StackArray<T, N> {
    self() { self.len = 0; }
    
    bool push(T value) {
        if (self.len >= N) { return false; }
        self.data[self.len] = value;
        self.len = self.len + 1;
        return true;
    }
}
```

## 実装優先度

| 項目 | 複雑度 | 必要性 |
|-----|-------|-------|
| パーサー拡張 | 中 | 必須 |
| AST拡張 | 中 | 必須 |
| 型チェッカー | 高 | 必須 |
| HIR/MIR | 中 | 必須 |
| 定数式評価 | 高 | 必須 |
| LLVM生成 | 低 | 必須 |

## 制限事項（初期実装）

1. **サポートする定数型**: `int`, `uint`, `bool` のみ
2. **定数式**: リテラルと単純な算術演算のみ
3. **デフォルト値**: 初期実装では非サポート

## テスト計画

```cm
// tests/test_programs/generics/const_generics.cm

struct Buffer<T, const SIZE: int> {
    T[SIZE] data;
}

int main() {
    Buffer<int, 10> buf;
    // ...
}
```
