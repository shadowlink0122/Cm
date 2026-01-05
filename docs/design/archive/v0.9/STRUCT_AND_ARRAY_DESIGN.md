# 構造体と配列の拡張設計

**作成日**: 2024-12-20  
**ステータス**: 設計中

---

## 1. 概要

このドキュメントは、Cm言語における構造体と配列の拡張機能の設計を定義します。

### 1.1 現在の実装状況

| 機能 | インタプリタ | LLVM | WASM | 備考 |
|------|------------|------|------|------|
| 基本的な構造体定義 | ✅ | ✅ | ✅ | |
| 単一フィールドアクセス (`p.x`) | ✅ | ✅ | ✅ | |
| ネストしたフィールドアクセス (`o.inner.x`) | ✅ | ✅ | ✅ | 読み取り |
| ネストしたフィールド代入 (`o.inner.x = 10`) | ✅ | ✅ | ✅ | 2024-12-20 実装 |
| 明示的構造体リテラル (`Point{x: 10, y: 20}`) | ✅ | ✅ | ✅ | 名前付きのみ |
| 暗黙的構造体リテラル (`{x: 10, y: 20}`) | ✅ | ✅ | ✅ | 2024-12-21 実装、型推論 |
| ネストした暗黙的リテラル | ✅ | ✅ | ✅ | 2024-12-21 実装 |
| impl内の暗黙的this (`return x`) | ✅ | ✅ | ✅ | 2024-12-20 実装 |
| thisキーワード (`return this`) | ✅ | ✅ | ⚠️ | パース可、値セマンティクス |
| 複合メソッド呼び出し (`o.inner.func()`) | ✅ | ✅ | ✅ | 動作確認済み |
| 配列リテラル (`[1, 2, 3]`) | ✅ | ✅ | ✅ | 2024-12-21 実装 |
| 構造体内の配列フィールド | ✅ | ✅ | ✅ | 2024-12-21 実装 |
| 構造体の配列 | ✅ | ✅ | ✅ | 2024-12-21 実装 |
| 配列内暗黙的構造体リテラル | ✅ | ✅ | ✅ | 2024-12-21 実装 |
| 構造体内のポインタフィールド | ⚠️ | ⚠️ | ⚠️ | 部分的 |

### 1.2 目標

1. **ネストしたフィールドへの直接代入** - `o.inner.x = 10` を完全サポート ✅
2. **構造体リテラル** - `Point{x: 10, y: 20}` による名前付き初期化のみ ✅
3. **配列リテラル** - `[1, 2, 3]` による初期化 ✅
4. **複合型のサポート** - 構造体内の配列、配列内の構造体 ✅
5. **複合メソッド呼び出し** - `s.mem.func()`, `arr[i].func()` ✅
6. **メソッドチェーン** - `obj.m1().m2().m3()` (制限あり)

---

## 2. 構文設計

### 2.1 構造体リテラル

#### 2.1.1 基本構文（名前付き初期化のみ）

```cm
// 明示的型名による初期化
Point p = Point{x: 10, y: 20};

// 暗黙的構造体リテラル（型推論）✅ 2024-12-21 実装
Point p = {x: 10, y: 20};

// 順序は自由
Point p = {y: 20, x: 10};

// 変数から
int x = 10;
Point p = {x: x, y: 20};
```

#### 2.1.2 ネストした構造体リテラル

```cm
struct Inner {
    int x;
    int y;
}

struct Outer {
    Inner inner;
    int z;
}

// 暗黙的構造体リテラル（推奨）✅
Outer o = {
    inner: {x: 10, y: 20},
    z: 30
};

// 明示的型名（従来形式）
Outer o = Outer{
    inner: Inner{x: 10, y: 20},
    z: 30
};
```

#### 2.1.3 構造体リテラルの文法

```ebnf
struct_literal = [type_name] "{" [field_init_list] "}" ;
field_init_list = field_init ("," field_init)* [","] ;
field_init = identifier ":" expression ;
```

### 2.2 配列リテラル

#### 2.2.1 基本構文

```cm
// 角括弧を使用
int[5] arr = [1, 2, 3, 4, 5];

// 代入での配列リテラル
int[3] arr;
arr = [10, 20, 30];

// 関数引数での使用
void process(int[3] data);
process([1, 2, 3]);

// サイズ推論（将来対応）
int[] arr = [1, 2, 3];  // int[3]
```

#### 2.2.2 構造体の配列リテラル

```cm
Point[3] points = [
    Point{x: 0, y: 0},
    Point{x: 10, y: 20},
    Point{x: 30, y: 40}
];
```

#### 2.2.3 配列リテラルの文法

```ebnf
array_literal = "[" [expression_list] "]" ;
expression_list = expression ("," expression)* [","] ;
```

### 2.3 ネストしたフィールドアクセス

#### 2.3.1 読み取り

```cm
struct Inner { int x; int y; }
struct Outer { Inner inner; int z; }

Outer o;
int val = o.inner.x;  // ネストした読み取り
```

#### 2.3.2 代入

```cm
o.inner.x = 10;       // ネストした代入
o.inner = Inner{10, 20};  // 中間構造体への代入
```

#### 2.3.3 配列を含むネスト

```cm
struct Data {
    int[3] values;
}

struct Container {
    Data data;
}

Container c;
c.data.values[0] = 100;  // 構造体→構造体→配列
int v = c.data.values[1];
```

---

## 3. 内部表現

### 3.1 HIR（High-level IR）

#### 3.1.1 構造体リテラル

```cpp
struct HirStructLiteral {
    std::string type_name;
    std::vector<std::pair<std::optional<std::string>, HirExprPtr>> fields;
    // field_name (optional), value
};
```

#### 3.1.2 配列リテラル

```cpp
struct HirArrayLiteral {
    hir::TypePtr element_type;
    std::vector<HirExprPtr> elements;
};
```

### 3.2 MIR（Mid-level IR）

#### 3.2.1 プロジェクション

```cpp
enum class ProjectionKind {
    Field,      // 構造体フィールド
    Index,      // 配列インデックス
    Deref,      // ポインタデリファレンス
    Downcast,   // 型キャスト（将来用）
};

struct PlaceProjection {
    ProjectionKind kind;
    size_t field_id;        // Field用
    LocalId index_local;    // Index用
};
```

#### 3.2.2 ネストしたアクセスの表現

```
o.inner.x = 10
↓
MirPlace {
    local: o
    projections: [Field(0), Field(0)]  // inner, x
}
```

### 3.3 インタプリタ値表現

```cpp
struct StructValue {
    std::string type_name;
    std::unordered_map<size_t, Value> fields;  // field_id → value
};

struct ArrayValue {
    std::vector<Value> elements;
};
```

---

## 4. 実装計画

### 4.1 Phase 1: ネストした代入の修正（優先度: 高）

**目標**: `o.inner.x = 10` を動作させる

**変更箇所**:
1. `stmt_lowering_impl.cpp` - ネストしたHirMemberの展開
2. `eval.hpp` - ネストしたストア処理

**テスト**:
```cm
struct Inner { int x; int y; }
struct Outer { Inner inner; int z; }

int main() {
    Outer o;
    o.inner.x = 10;
    o.inner.y = 20;
    o.z = 30;
    println("{} {} {}", o.inner.x, o.inner.y, o.z);
    return 0;
}
// 期待: 10 20 30
```

### 4.2 Phase 2: 構造体リテラル（優先度: 高）

**目標**: `Point{10, 20}` を動作させる

**変更箇所**:
1. `lexer.cpp` - 構造体リテラル開始の検出
2. `parser.cpp` - 構造体リテラルのパース
3. `hir_lowering.hpp` - HirStructLiteralへの変換
4. `expr_lowering_impl.cpp` - MIRへの変換
5. インタプリタ/LLVM/WASM - 評価

**テスト**:
```cm
struct Point { int x; int y; }

int main() {
    Point p = Point{10, 20};
    println("{} {}", p.x, p.y);
    return 0;
}
// 期待: 10 20
```

### 4.3 Phase 3: 配列リテラルの拡張（優先度: 中）

**目標**: 代入や引数での配列リテラル

**変更箇所**:
1. `parser.cpp` - 配列リテラルのパース拡張
2. `hir_lowering.hpp` - HirArrayLiteralの処理
3. `expr_lowering_impl.cpp` - MIRへの変換

**テスト**:
```cm
int main() {
    int[3] arr;
    arr = {1, 2, 3};
    println("{} {} {}", arr[0], arr[1], arr[2]);
    return 0;
}
// 期待: 1 2 3
```

### 4.4 Phase 4: 複合型のサポート（優先度: 中）

**目標**: 構造体内の配列、配列内の構造体

**テスト**:
```cm
struct Data {
    int[3] values;
}

int main() {
    Data d;
    d.values = {1, 2, 3};
    d.values[1] = 100;
    println("{}", d.values[1]);
    return 0;
}
// 期待: 100
```

### 4.5 Phase 5: 複合メソッド呼び出し（優先度: 高）

**目標**: ネストしたメンバーや配列要素のメソッド呼び出し

**パターン**:
```cm
// パターン1: ネストしたメンバーのメソッド
s.inner.method();

// パターン2: 配列要素のメソッド
arr[i].method();

// パターン3: 複合パターン
container.data.items[i].process();
```

**変更箇所**:
1. `parser.cpp` - メソッド呼び出しのベースオブジェクト解析
2. `hir_lowering.hpp` - 複合オブジェクトの型解決
3. `expr_lowering_impl.cpp` - 複合レシーバーのMIR生成

**テスト**:
```cm
interface Printable {
    void print();
}

struct Inner {
    int value;
}

impl Inner for Printable {
    void print() {
        println("Inner: {self.value}");
    }
}

struct Outer {
    Inner inner;
}

int main() {
    Outer o;
    o.inner.value = 42;
    o.inner.print();  // "Inner: 42"
    return 0;
}
```

### 4.6 Phase 6: メソッドチェーン（優先度: 中）

**目標**: 戻り値に対する連続したメソッド呼び出し

**パターン**:
```cm
// 自身を返すメソッドチェーン
builder.setX(10).setY(20).build();

// 異なる型を返すチェーン
str.trim().toUpper().length();
```

**変更箇所**:
1. `type_checker.cpp` - 戻り値型の追跡
2. `parser.cpp` - 連続したメソッド呼び出しのパース
3. `expr_lowering_impl.cpp` - チェーンのMIR生成

**テスト**:
```cm
interface Builder {
    Builder setX(int x);
    Builder setY(int y);
    Point build();
}

struct PointBuilder {
    int x;
    int y;
}

impl PointBuilder for Builder {
    Builder setX(int x) {
        self.x = x;
        return self;
    }
    
    Builder setY(int y) {
        self.y = y;
        return self;
    }
    
    Point build() {
        Point p;
        p.x = self.x;
        p.y = self.y;
        return p;
    }
}

int main() {
    PointBuilder b;
    Point p = b.setX(10).setY(20).build();
    println("{} {}", p.x, p.y);  // "10 20"
    return 0;
}
```

---

## 5. エラー処理

### 5.1 コンパイル時エラー

| エラー | メッセージ |
|--------|----------|
| フィールド数不一致 | `struct 'Point' has 2 fields, but 3 initializers given` |
| フィールド名不明 | `struct 'Point' has no field named 'z'` |
| 型不一致 | `cannot initialize field 'x' of type 'int' with value of type 'string'` |
| 配列サイズ不一致 | `array of size 5 cannot be initialized with 3 elements` |

### 5.2 実行時エラー

| エラー | メッセージ |
|--------|----------|
| 配列範囲外 | `array index 10 out of bounds for array of size 5` |
| 未初期化アクセス | `accessing uninitialized field 'x'` |

---

## 6. 互換性

### 6.1 既存コードとの互換性

- 既存の構造体定義は変更なし
- 既存の配列初期化は変更なし
- 段階的代入（`p.x = 10; p.y = 20;`）は引き続きサポート

### 6.2 将来の拡張

- デフォルト値付きフィールド: `struct Point { int x = 0; int y = 0; }`
- スプレッド演算子: `Point{...p1, x: 100}`
- 匿名構造体: `struct { int x; int y; }`

---

## 7. 参考情報

### 7.1 他言語との比較

| 言語 | 構造体リテラル | 配列リテラル |
|------|--------------|------------|
| C | `(Point){10, 20}` | `(int[]){1, 2, 3}` |
| C++ | `Point{10, 20}` | `{1, 2, 3}` |
| Rust | `Point { x: 10, y: 20 }` | `[1, 2, 3]` |
| Go | `Point{10, 20}` | `[3]int{1, 2, 3}` |
| **Cm** | `Point{10, 20}` | `{1, 2, 3}` |

### 7.2 関連ドキュメント

- [CANONICAL_SPEC.md](CANONICAL_SPEC.md) - 言語仕様
- [構造体チュートリアル](../tutorials/types/structs.md)
- [配列チュートリアル](../tutorials/basics/arrays.md)

---

## 8. 変更履歴

| 日付 | 変更内容 |
|------|---------|
| 2024-12-20 | 初版作成 |
| 2024-12-20 | Phase 1（ネストした代入）実装完了 |
| 2024-12-20 | Phase 5, 6（複合メソッド、メソッドチェーン）追加 |
| 2024-12-20 | Phase 2（構造体リテラル）実装完了 |
| 2024-12-20 | impl内暗黙的thisアクセス実装 |
| 2024-12-20 | thisキーワードパース実装 |
| 2024-12-20 | 複合メソッド呼び出し動作確認 |
| 2024-12-21 | 構造体リテラル名前付き初期化のみに変更 |
| 2024-12-21 | 配列リテラル `[]` 構文実装 |
| 2024-12-21 | 構造体の配列、構造体内配列の完全サポート |
