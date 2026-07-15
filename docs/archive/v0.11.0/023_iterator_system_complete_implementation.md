# Cm言語 イテレータシステム完全実装案

作成日: 2026-01-11対象バージョン: v0.11.0実装ステータス: 📝 設計段階

## エグゼクティブサマリー

Cm言語に統一的なイテレータシステムを実装し、配列、スライス、カスタムデータ構造で共通のイテレーションパターンを提供します。本提案では、ポインタベースの効率的な実装とインターフェースによる抽象化を組み合わせた設計を採用します。

## 1. 現状分析

### 1.1 既存の実装

#### for-in文の内部実装（現在）
```cm
// ユーザーコード
for (int val in arr) {
    println("{val}");
}

// 内部的に以下に変換される
int __for_in_idx_val = 0;
while (__for_in_idx_val < arr.length) {
    int val = arr[__for_in_idx_val];
    println("{val}");
    __for_in_idx_val++;
}
```

#### 実験的なIterator実装（tests/test_iter_closure_pattern.cm）
```cm
struct Iterator<T> {
    T* data;
    int size;
    int index;
}

impl<T> Iterator<T> {
    bool has_next() {
        return self.index < self.size;
    }

    T next() {
        T value = self.data[self.index];
        self.index++;
        return value;
    }
}
```

### 1.2 既存機能の活用可能性

| 機能 | 状態 | 活用方法 |
|------|------|----------|
| **ジェネリクス** | ✅ 実装済み | `Iterator<T>` の型安全性 |
| **インターフェース** | ✅ 実装済み | 共通のイテレーションプロトコル |
| **impl文** | ✅ 実装済み | 型ごとのイテレータ実装 |
| **for-in文** | ✅ 実装済み | イテレータとの統合 |
| **ポインタ演算** | ✅ 実装済み | 効率的な要素アクセス |

## 2. 提案するイテレータシステム

### 2.1 コアインターフェース

```cm
// std/iter/iterator.cm
module std.iter;

/// イテレータの基本インターフェース
export interface Iterator<T> {
    /// 次の要素があるか確認
    bool has_next();

    /// 次の要素を取得（ポインタを進める）
    T next();

    /// 現在の要素を覗く（ポインタは進めない）
    T peek();

    /// 残りの要素数を取得（可能な場合）
    int remaining();
}

/// 両方向イテレータ
export interface BidirectionalIterator<T> for Iterator<T> {
    /// 前の要素があるか確認
    bool has_prev();

    /// 前の要素を取得（ポインタを戻す）
    T prev();
}

/// ランダムアクセスイテレータ
export interface RandomAccessIterator<T> for BidirectionalIterator<T> {
    /// n番目の要素にジャンプ
    void seek(int n);

    /// 現在位置を取得
    int position();
}

/// イテレータを生成可能な型
export interface Iterable<T> {
    /// イテレータを取得
    Iterator<T> iter();
}
```

### 2.2 基本実装

```cm
// std/iter/array_iterator.cm

/// 配列用イテレータ（ポインタベース）
export struct ArrayIterator<T> {
    private T* current;    // 現在の要素へのポインタ
    private T* end;        // 終端へのポインタ
    private T* begin;      // 開始位置（逆方向用）
}

impl<T> ArrayIterator<T> {
    /// コンストラクタ
    self(T* data, int size) {
        self.begin = data;
        self.current = data;
        self.end = data + size;
    }
}

impl<T> ArrayIterator<T> for Iterator<T> {
    bool has_next() {
        return self.current < self.end;
    }

    T next() {
        if (!has_next()) {
            // パニックまたはエラー処理
            __panic__("Iterator exhausted");
        }
        T value = *self.current;
        self.current++;
        return value;
    }

    T peek() {
        if (!has_next()) {
            __panic__("Iterator exhausted");
        }
        return *self.current;
    }

    int remaining() {
        return (self.end - self.current);
    }
}

impl<T> ArrayIterator<T> for BidirectionalIterator<T> {
    bool has_prev() {
        return self.current > self.begin;
    }

    T prev() {
        if (!has_prev()) {
            __panic__("Iterator at beginning");
        }
        self.current--;
        return *self.current;
    }
}

impl<T> ArrayIterator<T> for RandomAccessIterator<T> {
    void seek(int n) {
        T* target = self.begin + n;
        if (target < self.begin || target >= self.end) {
            __panic__("Iterator seek out of bounds");
        }
        self.current = target;
    }

    int position() {
        return (self.current - self.begin);
    }
}
```

### 2.3 配列型への統合

```cm
// 配列にIterableインターフェースを実装
impl<T, const N: int> T[N] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>(&self[0], N);
    }
}

// スライスにIterableインターフェースを実装
impl<T> T[] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>(self.data, self.len);
    }
}
```

## 3. 高度な機能

### 3.1 イテレータアダプタ

```cm
// std/iter/adapters.cm

/// Map アダプタ - 要素を変換
export struct MapIterator<T, U> {
    private Iterator<T> source;
    private U (*transform)(T);
}

impl<T, U> MapIterator<T, U> for Iterator<U> {
    bool has_next() {
        return self.source.has_next();
    }

    U next() {
        T value = self.source.next();
        return self.transform(value);
    }

    U peek() {
        T value = self.source.peek();
        return self.transform(value);
    }

    int remaining() {
        return self.source.remaining();
    }
}

/// Filter アダプタ - 要素をフィルタリング
export struct FilterIterator<T> {
    private Iterator<T> source;
    private bool (*predicate)(T);
    private bool peeked;
    private T peeked_value;
}

impl<T> FilterIterator<T> for Iterator<T> {
    bool has_next() {
        if (self.peeked) {
            return true;
        }

        while (self.source.has_next()) {
            T value = self.source.peek();
            if (self.predicate(value)) {
                self.peeked = true;
                self.peeked_value = value;
                self.source.next();  // 消費
                return true;
            }
            self.source.next();  // スキップ
        }
        return false;
    }

    T next() {
        if (!has_next()) {
            __panic__("Iterator exhausted");
        }

        if (self.peeked) {
            self.peeked = false;
            return self.peeked_value;
        }

        // has_next()で見つかったはず
        __unreachable__();
    }
}
```

### 3.2 イテレータメソッドチェーン

```cm
// イテレータに便利メソッドを追加
impl<T> Iterator<T> {
    /// map適用
    <U> MapIterator<T, U> map(U (*f)(T)) {
        return MapIterator<T, U>{source: self, transform: f};
    }

    /// filter適用
    FilterIterator<T> filter(bool (*p)(T)) {
        return FilterIterator<T>{source: self, predicate: p};
    }

    /// 配列に収集
    T[] collect() {
        // 動的配列を作成して収集
        T[] result = [];
        while (self.has_next()) {
            result.push(self.next());
        }
        return result;
    }

    /// 畳み込み
    <U> U fold(U init, U (*f)(U, T)) {
        U acc = init;
        while (self.has_next()) {
            acc = f(acc, self.next());
        }
        return acc;
    }

    /// foreach実行
    void for_each(void (*f)(T)) {
        while (self.has_next()) {
            f(self.next());
        }
    }
}
```

## 4. for-in文との統合

### 4.1 コンパイラの変更

```cpp
// src/hir/lowering/stmt.cpp の修正

HirStmtPtr HirLowering::lower_for_in(ast::ForInStmt& for_in) {
    auto iterable_type = for_in.iterable->type;

    // Iterableインターフェースを実装しているかチェック
    if (implements_interface(iterable_type, "Iterable")) {
        // イテレータベースの展開
        return lower_for_in_with_iterator(for_in);
    } else if (is_array_type(iterable_type)) {
        // 既存の配列展開（後方互換性）
        return lower_for_in_array(for_in);
    }

    error("Type does not support iteration");
}

HirStmtPtr HirLowering::lower_for_in_with_iterator(ast::ForInStmt& for_in) {
    // for (T val in iterable) { body }
    // を以下に変換:
    // {
    //     auto __iter = iterable.iter();
    //     while (__iter.has_next()) {
    //         T val = __iter.next();
    //         body
    //     }
    // }

    // ... 実装 ...
}
```

### 4.2 使用例

```cm
int main() {
    int[5] arr = {1, 2, 3, 4, 5};

    // 基本的なfor-in（既存構文）
    for (int x in arr) {
        println("{x}");
    }

    // イテレータの明示的使用
    auto iter = arr.iter();
    while (iter.has_next()) {
        println("{}", iter.next());
    }

    // メソッドチェーン
    arr.iter()
       .filter((int x) => x % 2 == 0)
       .map((int x) => x * 2)
       .for_each((int x) => println("{x}"));

    // 収集
    int[] evens = arr.iter()
                     .filter((int x) => x % 2 == 0)
                     .collect();
}
```

## 5. カスタムイテレータの実装例

### 5.1 連結リスト

```cm
struct Node<T> {
    T value;
    Node<T>* next;
}

struct LinkedList<T> {
    Node<T>* head;
}

struct LinkedListIterator<T> {
    private Node<T>* current;
}

impl<T> LinkedListIterator<T> for Iterator<T> {
    bool has_next() {
        return self.current != null;
    }

    T next() {
        if (!has_next()) {
            __panic__("Iterator exhausted");
        }
        T value = self.current->value;
        self.current = self.current->next;
        return value;
    }
}

impl<T> LinkedList<T> for Iterable<T> {
    LinkedListIterator<T> iter() {
        return LinkedListIterator<T>{current: self.head};
    }
}
```

## 6. 実装ロードマップ

### Phase 1: コア実装（2週間）
- [ ] Iteratorインターフェース定義
- [ ] ArrayIterator実装
- [ ] 基本的なテストケース作成

### Phase 2: 言語統合（3週間）
- [ ] Iterableインターフェースの配列型への適用
- [ ] for-in文のイテレータサポート
- [ ] 型チェッカーの更新

### Phase 3: アダプタと便利機能（2週間）
- [ ] MapIterator, FilterIterator実装
- [ ] メソッドチェーンサポート
- [ ] collect, fold等のユーティリティ

### Phase 4: 最適化（2週間）
- [ ] インライン化による性能改善
- [ ] 特殊化による配列イテレータの最適化
- [ ] ベンチマークとチューニング

## 7. 期待される効果

### 利点
1. **統一的なイテレーションAPI** - すべてのコレクションで共通の操作
2. **型安全性** - ジェネリクスによる型チェック
3. **効率性** - ポインタベースで低オーバーヘッド
4. **拡張性** - カスタムデータ構造も対応可能
5. **関数型プログラミング** - map/filter/fold等のサポート

### パフォーマンス影響
- **メモリ**: イテレータオブジェクト1つあたり24バイト（3ポインタ）
- **CPU**: 仮想関数呼び出しのオーバーヘッド（インライン化で軽減）
- **最適化**: O2/O3レベルで従来のforループと同等の性能

## 8. 既存コードとの互換性

### 後方互換性
- 既存のfor-in文は引き続き動作
- 配列の直接インデックスアクセスも可能
- 段階的な移行が可能

### 移行パス
1. **v0.12.0**: イテレータインターフェース追加（実験的）
2. **v0.13.0**: 標準ライブラリでの採用
3. **v0.14.0**: for-in文の完全統合
4. **v1.0.0**: 安定版リリース

## まとめ

提案するイテレータシステムは、既存のCm言語機能（ジェネリクス、インターフェース、ポインタ）を最大限活用し、効率的で型安全なイテレーションを実現します。ポインタベースの実装により、ゼロコスト抽象化を達成しつつ、開発者に使いやすいAPIを提供できます。

---

**作成者:** Claude Code **レビュー状況:** 提案段階