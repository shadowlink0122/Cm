# ジェネリック型制約の分離設計と実装方針

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 実装方針策定

## エグゼクティブサマリー

現在のCm言語のジェネリック制約構文を改善し、**型制約**（型の許容リスト）と**インターフェース境界**（実装要件）を明確に分離します。これにより構文の曖昧さを解消し、より直感的な言語設計を実現します。

## 1. 現状分析

### 1.1 現在の実装状況

`src/frontend/parser/parser.hpp`の`parse_generic_params_v2()`より：

```cpp
// 現在の構文パターン
<T>                    // 無制約の型パラメータ
<T: Interface>         // インターフェース境界
<T: I1 + I2>          // 複数インターフェース（AND）
<T: I1 | I2>          // インターフェースOR（未実装）
<N: const int>        // const generics
<T = DefaultType>     // デフォルト型
```

### 1.2 AST構造

`src/frontend/ast/decl.hpp`より：

```cpp
struct GenericParam {
    std::string name;
    GenericParamKind kind;  // Type or Const
    std::shared_ptr<Type> type;  // constの場合の型
    std::shared_ptr<TypeConstraint> constraint;  // 制約
    std::shared_ptr<Type> default_type;  // デフォルト値
};

struct TypeConstraint {
    enum Kind { Interface, And, Or };
    Kind kind;
    std::string interface_name;  // Interface制約の場合
    std::vector<std::shared_ptr<TypeConstraint>> constraints;  // And/Orの場合
};
```

### 1.3 問題点

1. **構文の曖昧性**: `T: X`が型制約なのかインターフェース境界なのか不明確
2. **ユニオン型との混同**: `T: int | double`と`typedef Number = int | double`の違いが不明瞭
3. **const genericsの特殊扱い**: `N: const int`の`:const`が他の用法と異なる
4. **where句の未実装**: Rustスタイルのwhere句がない

## 2. 新設計

### 2.1 設計原則

1. **型制約**: ジェネリック宣言時に型の許容リストを指定
2. **インターフェース境界**: where句でのみ指定
3. **const generics**: 専用構文で明確化
4. **デフォルト型**: `=`で指定（変更なし）

### 2.2 新構文仕様

```cm
// 型制約：許容する具体型のリスト
<T: int | double | float>     // Tはこれらの型のいずれか
<T: Number>                    // Tはユニオン型Numberのいずれか

// const generics：現行構文維持
<N: const int>                 // Nはコンパイル時定数
<Size: const size_t = 256>    // デフォルト値付き

// インターフェース境界：where句（ブロック直前）
<T> T max(T a, T b) where T: Comparable {  // whereはブロック直前
    return a > b ? a : b;
}

<T, U> void process(T t, U u)
where T: Clone + Debug, U: Iterator {  // 複数制約
    // ...
}

// 組み合わせ例
<T: int | double, N: const int>
T[N] create_array()
where T: Arithmetic + Clone {
    // ...
}
```

### 2.3 ユニオン型との統合

```cm
// ユニオン型定義
typedef Number = int | double | float;
typedef Primitive = int | bool | char;

// 型制約での使用
<T: Number> T add(T a, T b) { ... }        // Tは Number型のいずれか
<T: int | bool> T logical_op(T a, T b) { ... }  // 直接指定も可能

// where句は別
<T: Number> where T: Arithmetic
T compute(T x) { ... }  // NumberかつArithmeticを実装
```

## 3. 実装計画

### 3.1 パーサー変更

#### Phase 1: 追加変更（既存構文は維持）

```cpp
// parser.hpp に追加
std::shared_ptr<WhereClause> parse_where_clause() {
    if (!expect_keyword("where")) return nullptr;

    auto clause = std::make_shared<WhereClause>();
    do {
        auto param_name = expect_identifier();
        expect(TokenType::COLON);
        auto bounds = parse_interface_bounds();  // I1 + I2形式
        clause->add_bound(param_name, bounds);
    } while (accept(TokenType::COMMA));

    return clause;
}

// 型制約とインターフェース境界を区別
std::shared_ptr<TypeConstraint> parse_type_constraint() {
    // int | double | float 形式をパース
    auto types = parse_union_type();
    return std::make_shared<TypeListConstraint>(types);
}
```

#### Phase 2: AST拡張

```cpp
// ast/decl.hpp に追加
struct WhereClause {
    struct Bound {
        std::string param_name;
        std::vector<std::string> interfaces;  // + で結合
    };
    std::vector<Bound> bounds;
};

// TypeConstraintを拡張
struct TypeConstraint {
    enum Kind {
        Interface,    // 既存（後方互換）
        TypeList,     // 新規：型の列挙
        And, Or       // 既存
    };

    // 新規：許容される具体型のリスト
    std::vector<std::shared_ptr<Type>> allowed_types;
};

// 関数宣言にwhere句を追加
struct FunctionDecl {
    // ... 既存フィールド ...
    std::shared_ptr<WhereClause> where_clause;  // 新規追加
};
```

### 3.2 型チェッカー変更

```cpp
// type_checker.cpp の変更
bool TypeChecker::check_generic_constraints(
    const GenericParam& param,
    const Type& actual_type,
    const WhereClause* where_clause) {

    // 1. 型制約のチェック（新規）
    if (param.constraint && param.constraint->kind == TypeConstraint::TypeList) {
        bool type_allowed = false;
        for (const auto& allowed : param.constraint->allowed_types) {
            if (types_equal(actual_type, *allowed)) {
                type_allowed = true;
                break;
            }
        }
        if (!type_allowed) {
            error("Type {} not in allowed list", actual_type.name);
            return false;
        }
    }

    // 2. インターフェース境界のチェック（where句から）
    if (where_clause) {
        for (const auto& bound : where_clause->bounds) {
            if (bound.param_name == param.name) {
                for (const auto& interface : bound.interfaces) {
                    if (!type_implements_interface(actual_type, interface)) {
                        error("Type {} doesn't implement {}",
                              actual_type.name, interface);
                        return false;
                    }
                }
            }
        }
    }

    return true;
}
```

## 4. 破壊的変更と移行戦略

### 4.1 破壊的変更

| 変更内容 | 既存コード | 新コード | 影響度 |
|---------|-----------|----------|--------|
| インターフェース境界の移動 | `<T: Comparable> T max(T a, T b) {}` | `<T> T max(T a, T b) where T: Comparable {}` | **高** |
| 複合境界の移動 | `<T: Clone + Debug>` | `<T> ... where T: Clone + Debug {}` | **高** |

### 4.2 追加変更（後方互換）

| 追加機能 | 構文 | 影響 |
|---------|------|------|
| 型リスト制約 | `<T: int \| double>` | なし（新機能） |
| where句 | `where T: Interface` | なし（新機能） |
| ユニオン型制約 | `<T: Number>` | なし（新機能） |

### 4.3 移行期間の互換性維持

```cpp
// Phase 1: 警告モード（v0.11.0）
<T: Comparable>  // 警告: Deprecated, use 'where T: Comparable'
                 // 動作は継続

// Phase 2: エラーモード（v0.12.0）
<T: Comparable>  // エラー: Use 'where T: Comparable' instead

// Phase 3: 完全移行（v1.0.0）
// 古い構文のサポート完全削除
```

## 5. 実装優先度

### 5.1 実装フェーズ

| フェーズ | 内容 | 期間 | 依存関係 |
|---------|------|------|----------|
| Phase 1 | where句パーサー実装 | 1週間 | なし |
| Phase 2 | 型リスト制約実装 | 1週間 | ユニオン型 |
| Phase 3 | 型チェッカー統合 | 2週間 | Phase 1,2 |
| Phase 4 | const generics新構文 | 3日 | なし |
| Phase 5 | 移行警告実装 | 3日 | Phase 3 |
| Phase 6 | ドキュメント更新 | 2日 | 全Phase |

### 5.2 テスト計画

```cm
// tests/generics/type_constraints.cm
<T: int | double> T add(T a, T b) { return a + b; }

void test_type_constraints() {
    assert(add(1, 2) == 3);           // OK: int
    assert(add(1.0, 2.0) == 3.0);     // OK: double
    // add("a", "b");                 // Error: string not allowed
}

// tests/generics/where_clause.cm
<T> T find_max(Vector<T> items)
where T: Comparable + Clone {
    // ... implementation ...
}

void test_where_clause() {
    Vector<int> nums = {1, 2, 3};
    assert(find_max(nums) == 3);      // OK: int implements both
}

// tests/generics/const_generics.cm
<N: const int, T: Number>
struct Matrix
where T: Arithmetic {
    T data[N][N];
};

void test_const_generics() {
    Matrix<3, double> m;              // OK
    // Matrix<"3", double> m;        // Error: "3" is not const int
}
```

## 6. 影響を受けるコンポーネント

### 6.1 パーサー
- ✅ `parse_generic_params_v2()` - 拡張必要
- ✅ `parse_function_decl()` - where句追加
- ✅ `parse_struct_decl()` - where句追加
- ✅ `parse_trait_decl()` - where句追加

### 6.2 AST
- ✅ `GenericParam` - constraint フィールド拡張
- ✅ `FunctionDecl` - where_clause追加
- ✅ `StructDecl` - where_clause追加
- ✅ `TypeConstraint` - TypeList追加

### 6.3 型システム
- ✅ `TypeChecker` - 制約検証ロジック変更
- ✅ `TypeInferer` - 型推論への影響
- ✅ `ConstraintSolver` - 新制約の処理

### 6.4 コード生成
- ⭕ 影響なし（型チェック後なので）

## 7. 実装例

### 7.1 標準ライブラリでの使用

```cm
// 型制約：数値型のみ
<T: int | double | float>
T abs(T value) {
    return value < 0 ? -value : value;
}

// インターフェース境界：where句（ブロック直前）
<T> T max(T a, T b)
where T: Comparable {
    return a > b ? a : b;
}

// 組み合わせ
<T: Number, N: const size_t>
struct Vector
where T: Arithmetic + Clone {
    T data[N];

    T sum() where T: Addition {  // メソッドレベルの追加制約
        T result = T::zero();
        for (int i = 0; i < N; i++) {
            result = result + data[i];
        }
        return result;
    }
};
```

### 7.2 ユーザーコード例

```cm
// 型定義
typedef SignedInt = int | tiny | short | long;
typedef UnsignedInt = uint | utiny | ushort | ulong;

// 符号付き整数のみ許可
<T: SignedInt>
T safe_negate(T value) {
    if (value == T::min()) {
        panic("Overflow: cannot negate minimum value");
    }
    return -value;
}

// インターフェース要件は別
<T> void log_and_save(T data, string filename)
where T: Serializable + Debug {
    debug_print(data);           // Debug要求
    file_write(filename, data);  // Serializable要求
}
```

## 8. エラーメッセージの改善

### 8.1 型制約違反

```
error: type constraint violation
  --> main.cm:10:15
   |
10 | int result = add("hello", "world");
   |              ^^^
   |
   = note: function 'add' requires T: int | double | float
   = note: but 'string' was provided
   = help: use a numeric type instead
```

### 8.2 インターフェース境界違反

```
error: interface bound not satisfied
  --> main.cm:15:20
   |
15 | Vector<MyStruct> v = sort(items);
   |                      ^^^^
   |
   = note: function 'sort' requires T: Comparable (from where clause)
   = note: but 'MyStruct' doesn't implement 'Comparable'
   = help: implement Comparable for MyStruct:
           impl Comparable for MyStruct { ... }
```

## 9. 利点

### 9.1 明確な責任分離
- **型制約**: どの具体型が許可されるか
- **インターフェース境界**: どの機能が必要か
- **const generics**: コンパイル時定数

### 9.2 可読性の向上
```cm
// Before（曖昧）
<T: Comparable, N: const int> T[N] sort_array(T[N] arr) { ... }

// After（明確）
<T, N: const int> T[N] sort_array(T[N] arr)
where T: Comparable {
    // ...
}
```

### 9.3 ユニオン型との一貫性
```cm
typedef Number = int | double;
<T: Number>  // 自然な構文
```

## 10. リスクと対策

### 10.1 既存コードの破壊

**リスク**: 大量の既存コードが動作しなくなる

**対策**:
1. 移行期間を設ける（2バージョン）
2. 自動マイグレーションツールの提供
3. 明確な移行ガイド

### 10.2 学習コスト

**リスク**: 新構文の学習が必要

**対策**:
1. 直感的な構文設計
2. 充実したドキュメント
3. エラーメッセージでの教育

## 11. まとめ

この設計により：

✅ **型制約とインターフェース境界の明確な分離**
✅ **ユニオン型との自然な統合**
✅ **const genericsの一貫した構文**
✅ **段階的な移行パス**
✅ **後方互換性の考慮**

実装は段階的に行い、既存コードへの影響を最小限に抑えながら、より表現力豊かで直感的なジェネリックシステムを実現します。

---

**作成者:** Claude Code
**レビュー待ち:** 実装開始前に要確認
**次ステップ:** Phase 1のwhere句パーサー実装から開始