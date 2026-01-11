# Cm言語 C++風STLコンテナ実装 - 現状分析と改善提案

作成日: 2026-01-10
バージョン: v0.11.0対応

## エグゼクティブサマリー

本文書は、Cm言語におけるC++風STLコンテナ実装のための現状分析と改善提案をまとめたものです。調査の結果、Cm言語は既にジェネリクス、RAII、インターフェースシステムなど、STL実装に必要な基盤機能の多くを備えていることが判明しました。しかし、完全なC++風STL実装にはいくつかの重要な課題があり、特に「selfメソッド変更バグ」の修正が最優先事項として識別されました。

## 1. 調査結果サマリー

### 1.1 現在の実装状況

| 機能カテゴリ | 実装度 | 評価 | 説明 |
|------------|-------|------|-----|
| ジェネリクス | 95% | ✅ | C++風構文 `<T>`、型制約、モノモーフィゼーション実装済み |
| メモリ管理 | 70% | ⚠️ | RAII（コンストラクタ/デストラクタ）実装済み、スマートポインタ未実装 |
| インターフェース | 90% | ✅ | interface定義、impl for実装済み |
| コンテナ | 20% | ❌ | PriorityQueue、LinkedListのサンプルのみ |
| イテレータ | 0% | ❌ | 未実装 |

### 1.2 重要な発見事項

#### 強み
1. **C++風構文が完全実装**: `map<int, string>` のような構文が既にサポート
2. **インターフェースベース設計**: `interface` と `impl for` による多態性
3. **RAIIサポート**: デストラクタによる自動リソース管理
4. **モノモーフィゼーション**: ジェネリック型の実体化メカニズム完備

#### 弱点
1. **致命的バグ**: selfメソッド内での状態変更が反映されない
2. **スマートポインタ欠如**: 手動malloc/free必要
3. **標準コンテナ未実装**: Vector, Map, Set等の基本コンテナなし
4. **イテレータパターン未実装**: 統一的な反復処理機構なし

## 2. 主要問題点と影響分析

### 2.1 P0: 致命的問題（即座修正必須）

#### selfメソッド変更バグ
```cm
impl<T> Vector<T> for Container<T> {
    void push(T item) {
        self.size = self.size + 1;  // この変更が反映されない！
    }
}
```

**影響:**
- 全てのコンテナ実装が不可能
- pushやpopなど状態を変更するメソッドが動作しない
- v0.11.0のSTL実装のブロッカー

**原因:**
- MIR lowering時にselfが値コピーされ、その後参照を取る実装
- `/src/mir/lowering/expr_call.cpp` または `/src/mir/lowering/stmt.cpp`

**修正案:**
```cpp
// src/mir/lowering/expr_call.cpp
// 現在の実装（推測）
Value* self_copy = emit_copy(self);
Value* self_ref = emit_ref(self_copy);

// 修正案
Value* self_ref = emit_mut_ref(self);  // 直接可変参照を取る
```

### 2.2 P1: 重要問題（STL実装に必須）

#### スマートポインタの欠如
**影響:**
- メモリリークのリスク
- 手動メモリ管理の負担
- 例外安全性の欠如

**対応案:**
```cm
// unique_ptr相当の実装
struct UniquePtr<T> {
    T* ptr;
}

impl<T> UniquePtr<T> {
    self(T* p) { self.ptr = p; }
    ~self() {
        if (self.ptr != null as T*) {
            free(self.ptr as void*);
        }
    }
}

impl<T> UniquePtr<T> for Deref<T> {
    T* operator*() { return self.ptr; }
}
```

### 2.3 P2: 機能欠落（段階的実装可）

#### イテレータパターン
**影響:**
- 汎用アルゴリズムの実装困難
- コンテナ横断的な処理の統一性欠如

**対応案:**
```cm
interface Iterator<T> {
    bool has_next();
    T next();
}

interface Iterable<T> {
    Iterator<T> iter();
}
```

## 3. 改善提案とロードマップ

### 3.1 フェーズ1: 基盤修正（1-2週間）

#### 1.1 selfメソッド変更バグの修正
**実装箇所:**
- `/src/mir/lowering/expr_call.cpp`
- `/src/mir/lowering/stmt.cpp`

**テストケース:**
```cm
// tests/regression/self_mutation_test.cm
struct Counter {
    int count;
}

impl Counter {
    void increment() {
        self.count = self.count + 1;
    }
}

int main() {
    Counter c;
    c.count = 0;
    c.increment();
    assert(c.count == 1);  // 現在は失敗する
}
```

#### 1.2 基本的なメモリユーティリティ
```cm
// src/std/memory.cm
<T> T* allocate(int count) {
    void* mem = malloc(sizeof(T) * count);
    return mem as T*;
}

<T> void deallocate(T* ptr) {
    free(ptr as void*);
}

<T> void copy(T* dst, T* src, int count) {
    // memcpy相当の実装
}
```

### 3.2 フェーズ2: 基本コンテナ実装（2-3週間）

#### 2.1 Vector<T>の実装
```cm
// src/std/collections/vector.cm
struct Vector<T> {
    T* data;
    int size;
    int capacity;
}

interface Container<T> {
    void push(T item);
    T pop();
    T get(int index);
    int len();
    bool is_empty();
}

impl<T> Vector<T> {
    self() {
        self.data = null as T*;
        self.size = 0;
        self.capacity = 0;
    }

    self(int initial_capacity) {
        self.data = allocate<T>(initial_capacity);
        self.size = 0;
        self.capacity = initial_capacity;
    }

    ~self() {
        if (self.data != null as T*) {
            deallocate(self.data);
        }
    }
}

impl<T> Vector<T> for Container<T> {
    void push(T item) {
        self.ensure_capacity(self.size + 1);
        self.data[self.size] = item;
        self.size = self.size + 1;
    }

    T pop() {
        assert(self.size > 0);
        self.size = self.size - 1;
        return self.data[self.size];
    }

    T get(int index) {
        assert(index < self.size);
        return self.data[index];
    }

    int len() { return self.size; }
    bool is_empty() { return self.size == 0; }
}

// プライベートメソッド
impl<T> Vector<T> {
    private void ensure_capacity(int required) {
        if (required > self.capacity) {
            int new_cap = self.capacity * 2;
            if (new_cap < required) { new_cap = required; }
            if (new_cap < 8) { new_cap = 8; }

            T* new_data = allocate<T>(new_cap);
            if (self.data != null as T*) {
                copy(new_data, self.data, self.size);
                deallocate(self.data);
            }

            self.data = new_data;
            self.capacity = new_cap;
        }
    }
}
```

#### 2.2 HashMap<K, V>の実装
```cm
// src/std/collections/hashmap.cm
struct Entry<K, V> {
    K key;
    V value;
    Entry<K, V>* next;
}

struct HashMap<K, V> {
    Entry<K, V>** buckets;
    int bucket_count;
    int size;
}

interface Map<K, V> {
    void put(K key, V value);
    V get(K key);
    bool contains(K key);
    void remove(K key);
}

impl<K: Hash + Eq, V> HashMap<K, V> for Map<K, V> {
    // 実装詳細...
}
```

### 3.3 フェーズ3: 高度な機能（3-4週間）

#### 3.1 イテレータ実装
```cm
interface Iterator<T> {
    bool has_next();
    T next();
}

impl<T> Vector<T> for Iterable<T> {
    VectorIterator<T> iter() {
        return VectorIterator<T>(self);
    }
}

struct VectorIterator<T> {
    Vector<T>* vec;
    int index;
}

impl<T> VectorIterator<T> for Iterator<T> {
    bool has_next() { return self.index < self.vec->size; }
    T next() {
        T val = self.vec->data[self.index];
        self.index = self.index + 1;
        return val;
    }
}
```

#### 3.2 アルゴリズム実装
```cm
// src/std/algorithm.cm
<T: Ord> void sort(T[] arr, int len) {
    // クイックソート実装
}

<T, I: Iterator<T>> bool any(I iter, bool (fn)(T)) {
    while (iter.has_next()) {
        if (fn(iter.next())) { return true; }
    }
    return false;
}
```

## 4. リファクタリング提案

### 4.1 ディレクトリ構造の整理
```
src/
├── std/                    # 標準ライブラリ（新規）
│   ├── collections/
│   │   ├── vector.cm
│   │   ├── hashmap.cm
│   │   ├── set.cm
│   │   └── deque.cm
│   ├── memory/
│   │   ├── allocator.cm
│   │   ├── unique_ptr.cm
│   │   └── shared_ptr.cm
│   ├── iterator/
│   │   └── traits.cm
│   └── algorithm/
│       ├── sort.cm
│       └── search.cm
```

### 4.2 テスト構造の改善
```
tests/
├── unit/                   # 単体テスト
│   ├── collections/
│   │   ├── vector_test.cm
│   │   └── hashmap_test.cm
│   └── memory/
│       └── smart_ptr_test.cm
├── integration/           # 統合テスト
│   └── stl_usage_test.cm
└── benchmark/            # パフォーマンステスト
    └── container_perf.cm
```

### 4.3 コーディング規約
```cm
// 命名規則
struct PascalCase<T> { }      // 型名
interface PascalCase { }       // インターフェース名
void snake_case() { }         // 関数名
int snake_case;               // 変数名
const int UPPER_CASE = 42;   // 定数

// インターフェース設計原則
interface Container<T> {      // 汎用インターフェース
    // 基本操作のみ定義
    void add(T item);
    T remove();
    int size();
}

interface IndexedContainer<T> for Container<T> {  // 拡張インターフェース
    T get(int index);
    void set(int index, T value);
}
```

## 5. 実装優先順位と見積もり

### 優先度マトリクス

| 優先度 | タスク | 工数見積 | 依存関係 | 理由 |
|-------|--------|----------|----------|------|
| P0 | selfメソッドバグ修正 | 2-3日 | なし | 全てのブロッカー |
| P0 | Vector<T>実装 | 3-4日 | selfバグ修正 | 最も基本的なコンテナ |
| P1 | HashMap<K,V>実装 | 4-5日 | Vector | 頻繁に使用される |
| P1 | メモリユーティリティ | 2日 | なし | コンテナ実装に必要 |
| P2 | イテレータ基盤 | 3日 | コンテナ | アルゴリズムの前提 |
| P2 | Set<T>実装 | 2日 | HashMap | HashMap のラッパー |
| P3 | スマートポインタ | 5日 | メモリ基盤 | 安全性向上 |
| P3 | アルゴリズムライブラリ | 5日 | イテレータ | 利便性向上 |

### リリース計画
- **v0.11.0-alpha**: selfバグ修正 + Vector + メモリ基盤（1週間）
- **v0.11.0-beta**: HashMap + Set + 基本イテレータ（2週間）
- **v0.11.0-rc**: 全コンテナ + テスト完了（3週間）
- **v0.11.0**: ドキュメント + サンプル完備（4週間）

## 6. リスクと緩和策

### 技術的リスク
| リスク | 影響度 | 発生確率 | 緩和策 |
|--------|--------|----------|--------|
| selfバグ修正の複雑性 | 高 | 中 | 段階的修正、広範なテスト |
| パフォーマンス劣化 | 中 | 低 | ベンチマーク継続実施 |
| 既存コードへの影響 | 高 | 中 | 回帰テストの充実 |

### プロジェクトリスク
- **スコープクリープ**: 最小限の実装から開始し、段階的に機能追加
- **後方互換性**: 既存APIは維持し、新機能として追加
- **ドキュメント不足**: 実装と並行してドキュメント作成

## 7. 成功指標

### 定量的指標
- selfバグ修正: 全テスト100%パス
- コンテナ実装: 5つの基本コンテナ（Vector, HashMap, Set, Deque, PriorityQueue）
- パフォーマンス: C++STLの80%以上の性能
- テストカバレッジ: 90%以上

### 定性的指標
- C++開発者にとって直感的なAPI
- 既存Cmコードとの良好な統合
- 明確なエラーメッセージ
- 充実したドキュメントとサンプル

## 8. 結論と次のステップ

### 結論
Cm言語は既にC++風STL実装のための強固な基盤を持っています。主要な障害は「selfメソッド変更バグ」であり、これを修正すれば、短期間で実用的なSTLコンテナライブラリを構築可能です。

### 推奨される次のステップ
1. **即座**: selfメソッドバグの修正に着手
2. **1週間以内**: Vector<T>のプロトタイプ実装
3. **2週間以内**: HashMap<K,V>実装とテスト
4. **1ヶ月以内**: v0.11.0-alphaリリース

### アクションアイテム
- [ ] selfバグ修正のPR作成
- [ ] std/ディレクトリ構造の作成
- [ ] Vector<T>の詳細設計文書作成
- [ ] テストフレームワークの整備
- [ ] CI/CDパイプラインへのSTLテスト追加

---

## 付録A: 参考実装例

### A.1 完全なVector<T>実装例
[コードは本文参照]

### A.2 簡単な使用例
```cm
int main() {
    Vector<int> vec;
    vec.push(10);
    vec.push(20);
    vec.push(30);

    println("Size: {}", vec.len());  // Size: 3

    while (!vec.is_empty()) {
        println("Popped: {}", vec.pop());
    }

    // HashMap使用例
    HashMap<string, int> map;
    map.put("apple", 100);
    map.put("banana", 200);

    if (map.contains("apple")) {
        println("Apple price: {}", map.get("apple"));
    }

    return 0;
}
```

## 付録B: 技術的詳細

### B.1 モノモーフィゼーションの仕組み
- 呼び出し時点で型引数を収集
- 特殊化された関数名を生成（例: `Vector__int__push`）
- MIRレベルで特殊化コードを生成
- LLVMに渡す前に完全に具体化

### B.2 メモリレイアウト
```
Vector<T>のメモリレイアウト:
+----------+----------+----------+
| data*    | size     | capacity |
| (8 bytes)| (4 bytes)| (4 bytes)|
+----------+----------+----------+

実データ配列（ヒープ）:
+-----+-----+-----+-----+
| T0  | T1  | T2  | ... |
+-----+-----+-----+-----+
```

---

**文書終了**
最終更新: 2026-01-10