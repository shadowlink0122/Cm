# ビルトイン型への impl 構文設計

作成日: 2026-01-11
関連文書: 027_iterator_required_language_features.md, 028_const_generics_design.md

## 概要

ビルトイン型（配列、スライス、プリミティブ型）に対してインターフェースを
実装できるようにする構文拡張です。イテレータシステムの完全実装に必須です。

## 対象型

| 型 | 構文 | 説明 |
|---|------|-----|
| 固定長配列 | `T[N]` | コンパイル時サイズ既知 |
| スライス | `T[]` | 動的サイズ |
| プリミティブ | `int`, `bool`等 | 基本型 |
| ポインタ | `T*` | ポインタ型 |

## 構文設計

### 配列・スライスへの impl

```cm
// スライスへの impl
impl<T> T[] for Iterable<T> {
    SliceIterator<T> iter() {
        return SliceIterator<T>(&self[0], len(self));
    }
}

// 固定長配列への impl（const generics必要）
impl<T, const N: int> T[N] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>(&self[0], N);
    }
}
```

### 代替構文案

```cm
// Rustスタイル
impl<T> [T] for Iterable<T> { ... }
impl<T, const N: int> [T; N] for Iterable<T> { ... }

// 明示的構文
impl<T> slice<T> for Iterable<T> { ... }
impl<T, const N: int> array<T, N> for Iterable<T> { ... }
```

**推奨**: 既存の`T[]`/`T[N]`構文を維持

### プリミティブ型への impl

```cm
impl int for Display {
    string to_string() {
        return __builtin_int_to_string(self);
    }
}

impl bool for Display {
    string to_string() {
        if (self) { return "true"; }
        return "false";
    }
}
```

## パーサー変更

### impl対象型の拡張

```cpp
// src/frontend/parser/parser.hpp

TypePtr parse_impl_target_type() {
    // ジェネリック型の場合
    if (is_generic_type()) {
        return parse_generic_type();
    }
    
    // 配列型: T[] または T[N]
    if (peek_is_array_suffix()) {
        TypePtr element_type = parse_type();
        if (consume_if(TokenKind::LBracket)) {
            if (consume_if(TokenKind::RBracket)) {
                // T[] - スライス
                return ast::make_slice_type(element_type);
            } else {
                // T[N] - 固定長配列
                auto size_expr = parse_expression();
                expect(TokenKind::RBracket);
                return ast::make_array_type(element_type, size_expr);
            }
        }
    }
    
    // プリミティブ型
    if (is_primitive_type()) {
        return parse_primitive_type();
    }
    
    // 通常の型名
    return parse_type_name();
}
```

### ImplDeclの拡張

```cpp
// src/frontend/ast/decl.hpp

struct ImplDecl {
    // 現在
    TypePtr target_type;
    
    // 追加情報
    enum class TargetKind {
        Named,      // struct Foo
        Array,      // T[N]
        Slice,      // T[]
        Primitive,  // int, bool等
        Pointer     // T*
    };
    TargetKind target_kind;
};
```

## 型チェッカー変更

### ビルトイン型の登録

```cpp
// src/frontend/types/checking/decl.cpp

void TypeChecker::register_builtin_impl(ast::ImplDecl& impl) {
    std::string type_key = make_builtin_type_key(impl.target_type);
    // "int[]", "int[5]", "int" などをキーとして登録
    
    builtin_impls_[type_key].push_back(&impl);
}

std::string make_builtin_type_key(TypePtr type) {
    if (is_slice_type(type)) {
        return type_to_string(element_type(type)) + "[]";
    }
    if (is_array_type(type)) {
        return type_to_string(element_type(type)) + 
               "[" + std::to_string(array_size(type)) + "]";
    }
    return type_to_string(type);
}
```

### メソッド解決

```cpp
// メソッド呼び出し時のビルトイン型チェック
MethodInfo* find_builtin_method(TypePtr type, const std::string& method_name) {
    std::string key = make_builtin_type_key(type);
    
    auto it = builtin_impls_.find(key);
    if (it != builtin_impls_.end()) {
        for (auto* impl : it->second) {
            // impl内のメソッドを検索
        }
    }
    
    // ジェネリックビルトインも検索
    // int[5] -> T[N] のマッチング
    return find_generic_builtin_method(type, method_name);
}
```

## HIR変更

### ビルトイン型implのlowering

```cpp
// src/hir/lowering/decl.cpp

HirDeclPtr HirLowering::lower_impl(ast::ImplDecl& impl) {
    auto hir_impl = std::make_unique<HirImpl>();
    
    if (impl.target_kind == ast::ImplDecl::TargetKind::Slice ||
        impl.target_kind == ast::ImplDecl::TargetKind::Array) {
        hir_impl->is_builtin_impl = true;
        hir_impl->builtin_kind = impl.target_kind;
    }
    
    // 以下通常のlowering
    // ...
}
```

## コード生成

### 関数名マングリング

```cpp
// ビルトイン型のマングリング規則
std::string mangle_builtin_method(TypePtr type, const std::string& method) {
    // int[] -> __slice_int__method
    // int[5] -> __array_int_5__method
    // int -> __int__method
}
```

### 生成例

```llvm
; impl<T> T[] for Iterable<T> の iter メソッド
define %SliceIterator @"__slice_int__iter"(%Slice* %self) {
    ; ...
}
```

## 実装フェーズ

### Phase 1: プリミティブ型への impl

```cm
impl int for SomeInterface { ... }
```

| 項目 | 複雑度 |
|-----|-------|
| パーサー | 低 |
| 型チェッカー | 中 |
| コード生成 | 低 |

### Phase 2: スライスへの impl

```cm
impl<T> T[] for Iterable<T> { ... }
```

| 項目 | 複雑度 |
|-----|-------|
| パーサー | 中 |
| 型チェッカー | 高 |
| ジェネリック解決 | 高 |

### Phase 3: 固定長配列への impl

```cm
impl<T, const N: int> T[N] for Iterable<T> { ... }
```

| 項目 | 複雑度 |
|-----|-------|
| const generics必須 | 028参照 |
| 型チェッカー | 高 |

## 使用例

### イテレータ実装

```cm
// スライスのイテレータ
impl<T> T[] for Iterable<T> {
    SliceIterator<T> iter() {
        return SliceIterator<T>(&self[0], len(self));
    }
}

// 使用
int[] nums = [1, 2, 3, 4, 5];
for (int n in nums.iter()) {
    println("{n}");
}
```

### プリミティブ型の拡張

```cm
impl int for Comparable {
    int compare(int other) {
        if (self < other) { return -1; }
        if (self > other) { return 1; }
        return 0;
    }
}
```

## 既存コードとの互換性

- 既存のimpl構文には影響なし
- 新機能はオプトイン

## テスト計画

```cm
// tests/test_programs/impl/builtin_impl.cm

interface Printable {
    void print();
}

impl int for Printable {
    void print() {
        println("{self}");
    }
}

int main() {
    int x = 42;
    x.print();  // "42"
    return 0;
}
```
