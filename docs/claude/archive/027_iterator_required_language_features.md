# イテレータシステム実装に必要な言語機能追加

作成日: 2026-01-11
関連文書: 025_iterator_explicit_interface_design.md, 026_iterator_stdlib_implementation.md

## 現在のCm言語の実装状況

### ✅ 実装済み機能

1. **基本的なジェネリクス**
   ```cm
   struct Container<T> { T value; }
   <T> T identity(T x) { return x; }
   ```

2. **複数型パラメータ**
   ```cm
   struct Pair<T, U> { T first; U second; }
   ```

3. **ジェネリックインターフェース**
   ```cm
   interface ValueHolder<T> { T get(); }
   ```

4. **ジェネリックimpl**
   ```cm
   impl<T> Container<T> for ValueHolder<T> { ... }
   ```

5. **配列構文**
   ```cm
   int[5] arr;        // 固定長配列
   int[] slice;       // スライス（動的配列）
   ```

6. **for-in文**
   ```cm
   for (int x in arr) { ... }
   ```

### ❌ 未実装機能（追加が必要）

## 1. 定数ジェネリックパラメータ 🔴 **必須**

### 現状
```cm
// ❌ 現在サポートされていない
struct Array<T, const N: int> {
    T[N] data;
}
```

### 必要な理由
- 固定長配列にIterableを実装するため
- コンパイル時定数をジェネリックパラメータとして扱う

### 実装案
```cpp
// src/frontend/ast/decl.hpp
struct GenericParam {
    enum Kind {
        Type,      // 型パラメータ（T, U）
        Const      // 定数パラメータ（const N: int）
    };

    Kind kind = Type;
    std::string name;
    TypePtr const_type;  // Constの場合の型（int等）
    std::vector<std::string> constraints;
};
```

### 使用例
```cm
// 固定長配列への自動実装
impl<T, const N: int> T[N] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>(&self[0], N);
    }
}
```

## 2. ビルトイン型へのimpl 🔴 **必須**

### 現状
```cm
// ❌ 配列型への直接impl不可
impl<T> T[] for Iterable<T> { ... }

// ❌ プリミティブ型へのimpl不可
impl int for Display { ... }
```

### 必要な理由
- 配列とスライスに自動でIterableを実装
- プリミティブ型にインターフェースを実装

### 実装方法

#### 方法1: コンパイラ内部での特別扱い
```cpp
// src/frontend/parser/parser_decl.cpp
if (type_name == "T[]" || is_array_type_syntax(type_name)) {
    // 特別な処理で配列型として認識
}
```

#### 方法2: 構文拡張
```cm
// 新しい構文を追加
impl<T> [T] for Iterable<T> { ... }      // スライス
impl<T, const N: int> [T; N] for Iterable<T> { ... }  // 固定長配列
```

## 3. impl内の通常メソッド 🟡 **推奨**

### 現状
```cm
// ❌ エラー: Expected 'self' or '~self' in constructor impl block
impl<T> LinkedList<T> {
    void push_front(T value) { ... }
}
```

### 必要な理由
- データ構造の操作メソッドを提供
- コンストラクタ以外のメソッドを定義

### 実装案
```cm
// implブロックを2種類に分ける
impl<T> LinkedList<T> {
    // コンストラクタとデストラクタのみ
    self() { ... }
    ~self() { ... }
}

// メソッド用の別構文（Rustスタイル）
impl<T> LinkedList<T> {
    fn push_front(&mut self, T value) { ... }
    fn pop_front(&mut self) -> T { ... }
}
```

または、既存の構文を拡張：
```cm
impl<T> LinkedList<T> {
    self() { ... }

    // メソッドも許可
    void push_front(T value) { ... }
}
```

## 4. 関数ポインタの型パラメータ 🟡 **推奨**

### 現状
```cm
// ❌ パースエラー
<T> void for_each(Iterator<T>* iter, void (*action)(T)) { ... }
```

### 必要な理由
- 汎用的なイテレータ操作関数
- 高階関数のサポート

### 実装案
```cpp
// パーサーの修正で対応可能
// 関数ポインタ型内の型パラメータを正しく認識
```

## 5. トレイト境界（Where節） 🔵 **将来的に必要**

### 現状
```cm
// ❌ サポートされていない
<T> where T: Ord
T max(T a, T b) { ... }
```

### 必要な理由
- より複雑な型制約の表現
- ジェネリックコードの型安全性向上

### 実装案
```cm
// 簡略構文
<T: Ord> T max(T a, T b) { ... }

// Where節構文
<T> T max(T a, T b) where T: Ord { ... }
```

## 実装優先順位とロードマップ

### Phase 1: 最小限の実装（2週間）
1. **定数ジェネリックパラメータ** 🔴
   - パーサー修正
   - AST拡張
   - 型チェッカー対応

2. **ビルトイン型へのimpl** 🔴
   - 配列型の特別処理
   - スライス型の認識

### Phase 2: 利便性向上（3週間）
3. **impl内の通常メソッド** 🟡
   - パーサー修正
   - セマンティック解析

4. **関数ポインタの型パラメータ** 🟡
   - パーサー修正のみ

### Phase 3: 高度な機能（将来）
5. **トレイト境界** 🔵
   - 新しい構文追加
   - 型システムの拡張

## コンパイラ修正が必要な箇所

### 1. パーサー
```cpp
// src/frontend/parser/parser_decl.cpp
- parse_generic_params(): 定数パラメータのサポート
- parse_impl(): ビルトイン型の認識
- parse_type(): 配列型構文の拡張
```

### 2. AST
```cpp
// src/frontend/ast/decl.hpp
- GenericParam: Kind列挙型追加
- ImplDecl: ビルトイン型のサポート
```

### 3. 型チェッカー
```cpp
// src/frontend/types/checking/interface.cpp
- ビルトイン型へのインターフェース実装チェック
- 定数ジェネリックパラメータの評価
```

### 4. HIR/MIR生成
```cpp
// src/hir/lowering/impl.cpp
- ビルトイン型のimplのlowering
- 定数パラメータの処理
```

## 代替実装案

### 最小限の変更で実装する場合

1. **イテレータは具体的な型で実装**
   ```cm
   // ジェネリックインターフェースの代わりに
   struct IntIterator { ... }
   struct DoubleIterator { ... }
   ```

2. **配列は特別扱い**
   ```cm
   // for-in文で配列を特別に認識
   // Iterableインターフェースなしで動作
   ```

3. **マクロで疑似的に実装**
   ```cm
   // マクロシステムが実装されれば
   #define IMPL_ITERABLE(T) \
       impl T[] for Iterable { ... }
   ```

## まとめ

イテレータシステムの完全な実装には、以下の言語機能追加が必要：

1. **必須機能**（Phase 1）
   - 定数ジェネリックパラメータ
   - ビルトイン型へのimpl

2. **推奨機能**（Phase 2）
   - impl内の通常メソッド
   - 関数ポインタの型パラメータ

3. **将来的な機能**（Phase 3）
   - トレイト境界（Where節）

これらの機能追加により、提案したイテレータシステム（023-026）が完全に実装可能になります。

---

**作成者:** Claude Code
**ステータス:** 要件定義完了