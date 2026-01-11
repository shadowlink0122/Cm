# イテレータシステム実装メモ

作成日: 2026-01-11
関連文書: 023_iterator_system_complete_implementation.md

## 実装に必要な言語機能の確認

### 現在実装済みで利用可能な機能

1. **ジェネリクス** ✅
   - `struct Iterator<T>` - 実装済み
   - `impl<T> Iterator<T>` - 実装済み

2. **インターフェース** ✅
   - `interface Printable` - 実装済み
   - `impl Point for Printable` - 実装済み

3. **ポインタ演算** ✅
   - `T* current` - 実装済み
   - `current + 1` - 実装済み

4. **for-in文** ✅
   - 配列に対する基本的なfor-in - 実装済み

### 実装に必要だが未実装の機能

1. **ジェネリックインターフェース** ❌
   ```cm
   // 現在サポートされていない
   interface Iterator<T> {
       T next();
   }
   ```
   **回避策:** 型ごとに個別のインターフェースを定義

2. **関数ポインタの型パラメータ** ⚠️
   ```cm
   // パースエラーが発生
   void for_each(Iterator<T>* iter, void (*action)(T))
   ```
   **回避策:** 具体的な型で実装

3. **impl内の非コンストラクタメソッド** ⚠️
   ```cm
   impl<T> LinkedList<T> {
       void push_front(T value) { // エラー
   ```
   **回避策:** implをインターフェース実装のみに使用

## 実装可能な範囲

### Phase 1: 基本イテレータ（現在可能）
```cm
// 具体的な型でのイテレータ
struct IntIterator {
    int* current;
    int* end;
}

impl IntIterator {
    self(int* data, int size) {
        self.current = data;
        self.end = data + size;
    }

    bool has_next() {
        return self.current < self.end;
    }

    int next() {
        int value = *self.current;
        self.current = self.current + 1;
        return value;
    }
}
```

### Phase 2: インターフェース統合（部分的に可能）
```cm
// 非ジェネリックインターフェース
interface IntIterable {
    bool has_next();
    int next();
}

impl IntIterator for IntIterable {
    bool has_next() { return self.current < self.end; }
    int next() { /* ... */ }
}
```

### Phase 3: for-in統合（コンパイラ修正必要）
- HIR/MIR変換部の修正が必要
- Iterableインターフェースのチェック追加
- イテレータメソッド呼び出しへの展開

## 推奨実装順序

### Step 1: プロトタイプ実装（1週間）
- [ ] 具体的な型（int）でのイテレータ実装
- [ ] 基本的な動作確認
- [ ] ベンチマーク作成

### Step 2: ジェネリックインターフェース対応（2週間）
- [ ] パーサー修正（ジェネリックインターフェース）
- [ ] 型チェッカー修正
- [ ] HIR/MIR生成対応

### Step 3: 言語統合（2週間）
- [ ] for-in文のイテレータ対応
- [ ] 標準ライブラリ更新
- [ ] ドキュメント作成

## コンパイラ修正が必要な箇所

### 1. パーサー
```cpp
// src/frontend/parser/parser_decl.cpp
// ジェネリックインターフェースのパース追加
if (peek().kind == TokenKind::Lt) {
    // interface Iterator<T> のパース
}
```

### 2. 型チェッカー
```cpp
// src/frontend/types/checking/interface.cpp
// ジェネリックインターフェースの型チェック
```

### 3. HIR Lowering
```cpp
// src/hir/lowering/stmt.cpp
// for-in文でIterableチェック追加
if (implements_interface(type, "Iterable")) {
    return lower_for_in_with_iterator(for_in);
}
```

## パフォーマンス考慮事項

### メモリレイアウト
```
ArrayIterator<T>:
  +0: T* current  (8 bytes)
  +8: T* end      (8 bytes)
  Total: 16 bytes
```

### 最適化機会
1. **インライン化** - has_next(), next()を積極的にインライン化
2. **ループ展開** - イテレータループの展開
3. **SIMD** - 配列イテレータでのSIMD活用

## まとめ

現在のCm言語でイテレータシステムの基本実装は可能ですが、完全な実装には以下の言語機能追加が必要です：

1. **必須**: ジェネリックインターフェース
2. **推奨**: impl内での通常メソッド定義
3. **推奨**: 関数ポインタの型パラメータサポート

これらの機能追加により、提案した設計（023_iterator_system_complete_implementation.md）が完全に実装可能になります。

---

**作成者:** Claude Code
**ステータス:** 実装準備段階