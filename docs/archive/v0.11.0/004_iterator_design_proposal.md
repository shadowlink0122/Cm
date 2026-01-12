# Cm言語 イテレータ設計提案書

作成日: 2026-01-10
対象バージョン: v0.11.0

## 概要

本文書では、Cm言語におけるイテレータシステムの設計提案を行います。C++のSTLイテレータの表現力と、既存のビルトインmap/filter機能を統合し、型安全で効率的なイテレータシステムを構築します。

## 1. 背景と要件

### 1.1 現状分析

#### 既存のビルトイン機能
Cm言語には既に配列/スライスに対する高階関数がビルトインとして実装されています：

```cm
// 現在のビルトインmap/filter
int[] numbers = [1, 2, 3, 4, 5];
int[] doubled = numbers.map((int x) => { return x * 2; });
int[] evens = numbers.filter((int x) => { return x % 2 == 0; });
int sum = numbers.reduce((int acc, int x) => { return acc + x; }, 0);
```

**特徴:**
- コンパイラレベルで実装（`__builtin_array_map`等）
- JavaScript/LLVMへの直接変換で高速
- クロージャ完全サポート
- 型推論による戻り値型の自動決定

#### 新規コンテナの要件
STLコンテナ（Vector, HashMap, Set等）には：
- 統一的なイテレータインターフェース
- ポインタベースの効率的な反復処理
- C++風の演算子オーバーロード（++, *, ->）
- アルゴリズムライブラリとの統合

### 1.2 設計方針

**二つのアプローチの共存:**
1. **ビルトイン関数型アプローチ**: 配列/スライス用（既存）
2. **イテレータベースアプローチ**: STLコンテナ用（新規）

両アプローチを統合し、ユーザーが自然に使い分けできる設計を目指します。

## 2. イテレータアーキテクチャ

### 2.1 インターフェース階層

```cm
// src/std/iter/traits.cm

// レベル1: 基本イテレータ
interface Iterator<T> {
    T* current();        // 現在の要素への参照
    void advance();      // 次の要素へ
    bool is_valid();     // 有効な状態か
}

// レベル2: 入力イテレータ（読み取り専用）
interface InputIterator<T> for Iterator<T> {
    T read();           // 値を読む
}

// レベル3: 前進イテレータ（複数回読み取り可能）
interface ForwardIterator<T> for InputIterator<T> {
    ForwardIterator<T> clone();  // イテレータのコピー
}

// レベル4: 双方向イテレータ
interface BidirectionalIterator<T> for ForwardIterator<T> {
    void retreat();     // 前の要素へ
}

// レベル5: ランダムアクセスイテレータ
interface RandomAccessIterator<T> for BidirectionalIterator<T> {
    void advance_by(int n);                              // n要素移動
    int distance_to(RandomAccessIterator<T> other);     // 距離計算
    T get_at(int offset);                               // 相対アクセス
}

// 比較可能性
interface EqualityComparable<T> {
    bool equals(T other);
}

interface Ordered<T> for EqualityComparable<T> {
    bool less_than(T other);
}
```

### 2.2 C++風演算子オーバーロード

```cm
// src/std/iter/operators.cm

// ポインタベースの基本イテレータ
struct PointerIterator<T> {
    T* ptr;
    T* end_ptr;  // 範囲チェック用（デバッグビルドのみ）
}

impl<T> PointerIterator<T> {
    self(T* start, T* end) {
        self.ptr = start;
        self.end_ptr = end;
    }

    // デリファレンス演算子
    T operator*() {
        #ifdef DEBUG
        if (self.ptr >= self.end_ptr || self.ptr == null as T*) {
            panic("Iterator dereferenced out of bounds");
        }
        #endif
        return *self.ptr;
    }

    // アロー演算子（構造体メンバアクセス）
    T* operator->() {
        #ifdef DEBUG
        if (self.ptr >= self.end_ptr || self.ptr == null as T*) {
            panic("Iterator dereferenced out of bounds");
        }
        #endif
        return self.ptr;
    }

    // 前置インクリメント
    PointerIterator<T>* operator++() {
        self.ptr = self.ptr + 1;
        return &self;
    }

    // 後置インクリメント
    PointerIterator<T> operator++(int) {
        PointerIterator<T> copy = *self;
        self.ptr = self.ptr + 1;
        return copy;
    }

    // 前置デクリメント
    PointerIterator<T>* operator--() {
        self.ptr = self.ptr - 1;
        return &self;
    }

    // 後置デクリメント
    PointerIterator<T> operator--(int) {
        PointerIterator<T> copy = *self;
        self.ptr = self.ptr - 1;
        return copy;
    }

    // 比較演算子
    bool operator==(PointerIterator<T> other) { return self.ptr == other.ptr; }
    bool operator!=(PointerIterator<T> other) { return self.ptr != other.ptr; }
    bool operator<(PointerIterator<T> other)  { return self.ptr < other.ptr; }
    bool operator<=(PointerIterator<T> other) { return self.ptr <= other.ptr; }
    bool operator>(PointerIterator<T> other)  { return self.ptr > other.ptr; }
    bool operator>=(PointerIterator<T> other) { return self.ptr >= other.ptr; }

    // ランダムアクセス演算子
    PointerIterator<T> operator+(int n) {
        return PointerIterator<T>(self.ptr + n, self.end_ptr);
    }

    PointerIterator<T> operator-(int n) {
        return PointerIterator<T>(self.ptr - n, self.end_ptr);
    }

    int operator-(PointerIterator<T> other) {
        return self.ptr - other.ptr;
    }

    T operator[](int n) {
        #ifdef DEBUG
        if (self.ptr + n >= self.end_ptr) {
            panic("Iterator indexed out of bounds");
        }
        #endif
        return self.ptr[n];
    }

    // 複合代入演算子
    void operator+=(int n) { self.ptr = self.ptr + n; }
    void operator-=(int n) { self.ptr = self.ptr - n; }
}

// インターフェース実装
impl<T> PointerIterator<T> for RandomAccessIterator<T> {
    T* current() { return self.ptr; }
    void advance() { self.ptr = self.ptr + 1; }
    bool is_valid() { return self.ptr < self.end_ptr; }
    void retreat() { self.ptr = self.ptr - 1; }
    void advance_by(int n) { self.ptr = self.ptr + n; }
    int distance_to(RandomAccessIterator<T> other) {
        return other.ptr - self.ptr;
    }
    T get_at(int offset) { return self.ptr[offset]; }
    T read() { return *self.ptr; }
    ForwardIterator<T> clone() { return *self; }
}
```

## 3. コンテナとの統合

### 3.1 Vector実装

```cm
// src/std/collections/vector_iter.cm

// Vectorのイテレータサポート
impl<T> Vec<T> {
    typedef PointerIterator<T> iterator;
    typedef PointerIterator<const T> const_iterator;

    // イテレータ取得メソッド
    iterator begin() {
        return iterator(self.data, self.data + self.len);
    }

    iterator end() {
        return iterator(self.data + self.len, self.data + self.len);
    }

    const_iterator cbegin() const {
        return const_iterator(self.data, self.data + self.len);
    }

    const_iterator cend() const {
        return const_iterator(self.data + self.len, self.data + self.len);
    }

    // 逆イテレータ
    ReverseIterator<T> rbegin() {
        return ReverseIterator<T>(self.end());
    }

    ReverseIterator<T> rend() {
        return ReverseIterator<T>(self.begin());
    }

    // イテレータベースの操作
    void insert(iterator pos, T value) {
        int index = pos - self.begin();
        self.insert_at(index, value);
    }

    iterator erase(iterator pos) {
        int index = pos - self.begin();
        self.remove_at(index);
        return self.begin() + index;
    }

    iterator erase(iterator first, iterator last) {
        int start = first - self.begin();
        int count = last - first;
        for (int i = 0; i < count; i++) {
            self.remove_at(start);
        }
        return self.begin() + start;
    }
}
```

### 3.2 Range-based構文サポート

```cm
// src/std/iter/range.cm

// Rangeコンセプト
interface Iterable<T> {
    Iterator<T> iter_begin();
    Iterator<T> iter_end();
}

// Vectorに実装
impl<T> Vec<T> for Iterable<T> {
    PointerIterator<T> iter_begin() { return self.begin(); }
    PointerIterator<T> iter_end() { return self.end(); }
}

// マクロによるfor-each実装
#define foreach(item, container) \
    for (auto _iter = (container).begin(); \
         _iter != (container).end(); \
         ++_iter) \
        for (auto item = *_iter; true; break)

// 使用例
Vec<int> numbers;
numbers.push(1);
numbers.push(2);
numbers.push(3);

foreach(n, numbers) {
    println("{}", n);
}

// C++11風の範囲for（言語機能として実装する場合）
for (int n : numbers) {
    println("{}", n);
}
```

## 4. 既存機能との統合戦略

### 4.1 二重インターフェース設計

```cm
// src/std/collections/vec_functional.cm

// Vectorに関数型インターフェースを追加
impl<T> Vec<T> {
    // ビルトイン風のmap実装（イテレータベース）
    <U> Vec<U> map(U (*f)(T)) {
        Vec<U> result;
        result.reserve(self.len);

        for (auto it = self.begin(); it != self.end(); ++it) {
            result.push(f(*it));
        }

        return result;
    }

    // ビルトイン風のfilter実装
    Vec<T> filter(bool (*pred)(T)) {
        Vec<T> result;

        for (auto it = self.begin(); it != self.end(); ++it) {
            if (pred(*it)) {
                result.push(*it);
            }
        }

        return result;
    }

    // ビルトイン風のreduce実装
    <U> U reduce(U (*f)(U, T), U initial) {
        U acc = initial;

        for (auto it = self.begin(); it != self.end(); ++it) {
            acc = f(acc, *it);
        }

        return acc;
    }

    // foreach実装
    void foreach(void (*f)(T)) {
        for (auto it = self.begin(); it != self.end(); ++it) {
            f(*it);
        }
    }

    // find実装
    T* find(bool (*pred)(T)) {
        for (auto it = self.begin(); it != self.end(); ++it) {
            if (pred(*it)) {
                return &(*it);
            }
        }
        return null as T*;
    }

    // any/all実装
    bool any(bool (*pred)(T)) {
        for (auto it = self.begin(); it != self.end(); ++it) {
            if (pred(*it)) {
                return true;
            }
        }
        return false;
    }

    bool all(bool (*pred)(T)) {
        for (auto it = self.begin(); it != self.end(); ++it) {
            if (!pred(*it)) {
                return false;
            }
        }
        return true;
    }
}
```

### 4.2 配列とVectorの統一インターフェース

```cm
// src/std/core/array_adapter.cm

// 固定長配列用のイテレータアダプタ
struct ArrayIterator<T, const int N> {
    T* ptr;
    T* end_ptr;
}

// 配列からイテレータを取得する関数
<T, const int N> ArrayIterator<T, N> array_begin(T[N] arr) {
    return ArrayIterator<T, N>(&arr[0], &arr[N]);
}

<T, const int N> ArrayIterator<T, N> array_end(T[N] arr) {
    return ArrayIterator<T, N>(&arr[N], &arr[N]);
}

// 配列をVectorに変換
<T, const int N> Vec<T> to_vec(T[N] arr) {
    Vec<T> result;
    result.reserve(N);

    for (int i = 0; i < N; i++) {
        result.push(arr[i]);
    }

    return result;
}

// スライスをVectorに変換
<T> Vec<T> slice_to_vec(T[] slice) {
    Vec<T> result;
    result.reserve(slice.len());

    for (int i = 0; i < slice.len(); i++) {
        result.push(slice[i]);
    }

    return result;
}
```

### 4.3 ブリッジ実装

```cm
// src/std/bridge/functional_bridge.cm

// ビルトイン関数を使用してVectorのmap/filterを高速化
impl<T> Vec<T> {
    // ビルトイン関数を内部で使用
    <U> Vec<U> map_fast(U (*f)(T)) {
        // 内部配列を取得
        T* arr = self.data;
        int len = self.len;

        // ビルトイン関数を呼び出し
        U* result_arr = __builtin_array_map(arr, len, f);

        // Vectorに変換
        Vec<U> result;
        result.data = result_arr;
        result.len = len;
        result.capacity = len;

        return result;
    }

    Vec<T> filter_fast(bool (*pred)(T)) {
        // ビルトイン関数を使用
        T* result_arr = __builtin_array_filter(self.data, self.len, pred);

        // サイズは動的に決まる
        Vec<T> result;
        // ... 結果をVectorに変換

        return result;
    }
}

// マクロでビルトイン版を選択可能に
#ifdef USE_BUILTIN_OPTIMIZATION
    #define VEC_MAP(vec, f) (vec).map_fast(f)
    #define VEC_FILTER(vec, p) (vec).filter_fast(p)
#else
    #define VEC_MAP(vec, f) (vec).map(f)
    #define VEC_FILTER(vec, p) (vec).filter(p)
#endif
```

## 5. 高度なイテレータパターン

### 5.1 イテレータアダプタ

```cm
// src/std/iter/adapters.cm

// Mapアダプタ
struct MapIterator<I: Iterator<T>, T, U> {
    I inner;
    U (*transform)(T);
    U cached_value;  // 変換結果をキャッシュ
    bool has_cached;
}

impl<I: Iterator<T>, T, U> MapIterator<I, T, U> for Iterator<U> {
    U* current() {
        if (!self.has_cached) {
            T* value = self.inner.current();
            if (value == null as T*) {
                return null as U*;
            }
            self.cached_value = self.transform(*value);
            self.has_cached = true;
        }
        return &self.cached_value;
    }

    void advance() {
        self.inner.advance();
        self.has_cached = false;
    }

    bool is_valid() {
        return self.inner.is_valid();
    }
}

// Filterアダプタ
struct FilterIterator<I: Iterator<T>, T> {
    I inner;
    bool (*predicate)(T);
}

impl<I: Iterator<T>, T> FilterIterator<I, T> for Iterator<T> {
    T* current() {
        // 条件を満たす要素まで進む
        while (self.inner.is_valid()) {
            T* value = self.inner.current();
            if (value != null as T* && self.predicate(*value)) {
                return value;
            }
            self.inner.advance();
        }
        return null as T*;
    }

    void advance() {
        self.inner.advance();
        // 次の有効な要素を探す
        self.current();
    }

    bool is_valid() {
        return self.inner.is_valid();
    }
}

// Takeアダプタ（最初のn個を取る）
struct TakeIterator<I: Iterator<T>, T> {
    I inner;
    int remaining;
}

impl<I: Iterator<T>, T> TakeIterator<I, T> for Iterator<T> {
    T* current() {
        if (self.remaining <= 0) {
            return null as T*;
        }
        return self.inner.current();
    }

    void advance() {
        if (self.remaining > 0) {
            self.inner.advance();
            self.remaining = self.remaining - 1;
        }
    }

    bool is_valid() {
        return self.remaining > 0 && self.inner.is_valid();
    }
}

// Chainアダプタ（2つのイテレータを連結）
struct ChainIterator<I1: Iterator<T>, I2: Iterator<T>, T> {
    I1 first;
    I2 second;
    bool on_second;
}

impl<I1: Iterator<T>, I2: Iterator<T>, T> ChainIterator<I1, I2, T> for Iterator<T> {
    T* current() {
        if (!self.on_second) {
            T* value = self.first.current();
            if (value != null as T*) {
                return value;
            }
            self.on_second = true;
        }
        return self.second.current();
    }

    void advance() {
        if (!self.on_second) {
            self.first.advance();
            if (!self.first.is_valid()) {
                self.on_second = true;
            }
        } else {
            self.second.advance();
        }
    }

    bool is_valid() {
        return (!self.on_second && self.first.is_valid()) ||
               (self.on_second && self.second.is_valid());
    }
}

// Zipアダプタ（2つのイテレータをペアにする）
struct ZipIterator<I1: Iterator<T>, I2: Iterator<U>, T, U> {
    I1 first;
    I2 second;
    struct Pair<T, U> current_pair;
}

impl<I1: Iterator<T>, I2: Iterator<U>, T, U> ZipIterator<I1, I2, T, U>
    for Iterator<Pair<T, U>> {

    Pair<T, U>* current() {
        T* first_val = self.first.current();
        U* second_val = self.second.current();

        if (first_val == null as T* || second_val == null as U*) {
            return null as Pair<T, U>*;
        }

        self.current_pair.first = *first_val;
        self.current_pair.second = *second_val;
        return &self.current_pair;
    }

    void advance() {
        self.first.advance();
        self.second.advance();
    }

    bool is_valid() {
        return self.first.is_valid() && self.second.is_valid();
    }
}
```

### 5.2 メソッドチェーン

```cm
// src/std/iter/chain.cm

// イテレータ拡張メソッド
impl<I: Iterator<T>, T> I {
    // map変換
    <U> MapIterator<I, T, U> map(U (*f)(T)) {
        MapIterator<I, T, U> result;
        result.inner = self;
        result.transform = f;
        result.has_cached = false;
        return result;
    }

    // filter変換
    FilterIterator<I, T> filter(bool (*pred)(T)) {
        FilterIterator<I, T> result;
        result.inner = self;
        result.predicate = pred;
        return result;
    }

    // take変換
    TakeIterator<I, T> take(int n) {
        TakeIterator<I, T> result;
        result.inner = self;
        result.remaining = n;
        return result;
    }

    // skip変換
    I skip(int n) {
        for (int i = 0; i < n && self.is_valid(); i++) {
            self.advance();
        }
        return self;
    }

    // collectでVectorに変換
    Vec<T> collect() {
        Vec<T> result;
        while (self.is_valid()) {
            T* value = self.current();
            if (value != null as T*) {
                result.push(*value);
            }
            self.advance();
        }
        return result;
    }

    // 消費メソッド
    int count() {
        int n = 0;
        while (self.is_valid()) {
            n = n + 1;
            self.advance();
        }
        return n;
    }

    T* find(bool (*pred)(T)) {
        while (self.is_valid()) {
            T* value = self.current();
            if (value != null as T* && pred(*value)) {
                return value;
            }
            self.advance();
        }
        return null as T*;
    }

    bool any(bool (*pred)(T)) {
        return self.find(pred) != null as T*;
    }

    bool all(bool (*pred)(T)) {
        while (self.is_valid()) {
            T* value = self.current();
            if (value != null as T* && !pred(*value)) {
                return false;
            }
            self.advance();
        }
        return true;
    }

    // fold/reduce
    <U> U fold(U initial, U (*f)(U, T)) {
        U acc = initial;
        while (self.is_valid()) {
            T* value = self.current();
            if (value != null as T*) {
                acc = f(acc, *value);
            }
            self.advance();
        }
        return acc;
    }
}

// 使用例
Vec<int> numbers;
numbers.push(1);
numbers.push(2);
numbers.push(3);
numbers.push(4);
numbers.push(5);

Vec<int> result = numbers.begin()
    .filter((int x) => { return x % 2 == 0; })
    .map((int x) => { return x * 2; })
    .take(2)
    .collect();
// result = [4, 8]
```

## 6. 安全性とパフォーマンス

### 6.1 イテレータ無効化の検出

```cm
// src/std/iter/safe.cm

struct SafeIterator<T> {
    Vec<T>* container;
    int index;
    ulong version;  // 変更検出用
}

impl<T> Vec<T> {
    ulong mod_count;  // 変更カウンタ

    SafeIterator<T> safe_iter() {
        SafeIterator<T> it;
        it.container = &self;
        it.index = 0;
        it.version = self.mod_count;
        return it;
    }

    // 変更操作でカウンタを更新
    void push(T item) {
        // 既存の実装...
        self.mod_count = self.mod_count + 1;
    }

    void pop() {
        // 既存の実装...
        self.mod_count = self.mod_count + 1;
    }
}

impl<T> SafeIterator<T> for Iterator<T> {
    T* current() {
        if (self.version != self.container->mod_count) {
            panic("Iterator invalidated by container modification");
        }
        if (self.index >= self.container->len) {
            return null as T*;
        }
        return &self.container->data[self.index];
    }

    void advance() {
        if (self.version != self.container->mod_count) {
            panic("Iterator invalidated by container modification");
        }
        self.index = self.index + 1;
    }

    bool is_valid() {
        return self.version == self.container->mod_count &&
               self.index < self.container->len;
    }
}
```

### 6.2 最適化戦略

```cm
// src/std/iter/optimized.cm

// インライン化ヒント
#[inline]
impl<T> PointerIterator<T> {
    T operator*() {
        // デバッグビルドでのみ境界チェック
        #ifdef DEBUG
            assert(self.ptr < self.end_ptr);
        #endif
        return *self.ptr;
    }
}

// SIMD最適化（将来的な拡張）
#ifdef __AVX2__
struct SimdIterator<T> {
    T* ptr;
    int count;

    // 8要素を同時に処理
    void advance_simd() {
        self.ptr = self.ptr + 8;
    }
}
#endif

// Small Size Optimization
struct SmallVecIterator<T, const int N> {
    union {
        T* heap_ptr;
        int stack_index;
    }
    bool on_heap;
    SmallVec<T, N>* container;
}
```

## 7. STLアルゴリズムとの統合

### 7.1 イテレータベースアルゴリズム

```cm
// src/std/algorithm/iter_algo.cm

// 基本アルゴリズム
<I: Iterator<T>, T> void for_each(I begin, I end, void (*f)(T)) {
    while (begin != end) {
        f(*begin);
        ++begin;
    }
}

<I: Iterator<T>, T: Eq> I find(I begin, I end, T value) {
    while (begin != end) {
        if (*begin == value) {
            return begin;
        }
        ++begin;
    }
    return end;
}

<I: Iterator<T>, T> I find_if(I begin, I end, bool (*pred)(T)) {
    while (begin != end) {
        if (pred(*begin)) {
            return begin;
        }
        ++begin;
    }
    return end;
}

<I: Iterator<T>, O: Iterator<T>, T> O copy(I begin, I end, O dest) {
    while (begin != end) {
        *dest = *begin;
        ++begin;
        ++dest;
    }
    return dest;
}

<I: Iterator<T>, T> I remove(I begin, I end, T value) {
    I write = begin;
    I read = begin;

    while (read != end) {
        if (*read != value) {
            if (write != read) {
                *write = *read;
            }
            ++write;
        }
        ++read;
    }

    return write;
}

// ソート（ランダムアクセスイテレータ必須）
<I: RandomAccessIterator<T>, T: Ord> void sort(I begin, I end) {
    if (begin >= end) {
        return;
    }

    // クイックソート実装
    I pivot = partition(begin, end);
    sort(begin, pivot);
    sort(pivot + 1, end);
}

<I: RandomAccessIterator<T>, T: Ord> I partition(I begin, I end) {
    T pivot = *(end - 1);
    I i = begin - 1;

    for (I j = begin; j < end - 1; ++j) {
        if (*j <= pivot) {
            ++i;
            T temp = *i;
            *i = *j;
            *j = temp;
        }
    }

    T temp = *(i + 1);
    *(i + 1) = *(end - 1);
    *(end - 1) = temp;

    return i + 1;
}

// 数値アルゴリズム
<I: Iterator<T>, T: Numeric> T accumulate(I begin, I end, T init) {
    T sum = init;
    while (begin != end) {
        sum = sum + *begin;
        ++begin;
    }
    return sum;
}

<I: Iterator<T>, T: Numeric> T inner_product(
    I begin1, I end1, I begin2, T init
) {
    T sum = init;
    while (begin1 != end1) {
        sum = sum + (*begin1 * *begin2);
        ++begin1;
        ++begin2;
    }
    return sum;
}
```

### 7.2 並列アルゴリズム（将来的拡張）

```cm
// src/std/algorithm/parallel.cm

enum ExecutionPolicy {
    Sequential,
    Parallel,
    ParallelUnsequenced
}

// 並列for_each
<I: RandomAccessIterator<T>, T>
void for_each(ExecutionPolicy policy, I begin, I end, void (*f)(T)) {
    match policy {
        Sequential => {
            // 通常の逐次処理
            for_each(begin, end, f);
        },
        Parallel => {
            // OpenMPやスレッドプールを使用
            int n = end - begin;
            #pragma omp parallel for
            for (int i = 0; i < n; i++) {
                f(begin[i]);
            }
        },
        ParallelUnsequenced => {
            // SIMD最適化を許可
            // ...
        }
    }
}
```

## 8. 使用ガイドライン

### 8.1 使い分けの指針

| シナリオ | 推奨アプローチ | 理由 |
|---------|--------------|------|
| 固定長配列の単純な変換 | ビルトイン map/filter | コンパイラ最適化が効く |
| Vectorの要素処理 | イテレータメソッド | 統一的なインターフェース |
| 複雑なイテレータ操作 | イテレータアダプタ | 柔軟な組み合わせ可能 |
| アルゴリズムライブラリ | イテレータベース | STL互換性 |
| パフォーマンス重視 | ポインタイテレータ | オーバーヘッド最小 |
| 安全性重視 | SafeIterator | 無効化検出 |

### 8.2 ベストプラクティス

```cm
// 良い例：適切な使い分け

// 1. 単純な配列変換はビルトイン使用
int[] arr = [1, 2, 3, 4, 5];
int[] doubled = arr.map((int x) => { return x * 2; });

// 2. VectorはイテレータメソッドかビルトインAPI
Vec<int> vec = to_vec(arr);
Vec<int> filtered = vec.filter((int x) => { return x > 2; });

// 3. 複雑な処理はイテレータチェーン
Vec<int> result = vec.begin()
    .filter(is_even)
    .map(square)
    .take(10)
    .collect();

// 4. アルゴリズムはイテレータベース
sort(vec.begin(), vec.end());
int sum = accumulate(vec.begin(), vec.end(), 0);

// 5. カスタムコンテナは標準インターフェース実装
struct MyContainer<T> {
    // ...
}

impl<T> MyContainer<T> {
    MyIterator<T> begin() { /* ... */ }
    MyIterator<T> end() { /* ... */ }
}
```

### 8.3 パフォーマンス考慮事項

```cm
// パフォーマンスのヒント

// 1. 小さな配列はビルトイン関数が高速
int[10] small_arr;
int[10] result = small_arr.map(transform);  // ビルトイン使用

// 2. 大きなコンテナはイテレータが効率的（メモリ効率）
Vec<LargeStruct> big_vec;
auto it = big_vec.begin()
    .filter(complex_predicate)
    .take(100);  // 遅延評価

// 3. チェーンは遅延評価
// 悪い例：中間Vectorを生成
Vec<int> temp1 = vec.map(f);
Vec<int> temp2 = temp1.filter(g);
Vec<int> result = temp2.map(h);

// 良い例：遅延評価で中間生成なし
Vec<int> result = vec.begin()
    .map(f)
    .filter(g)
    .map(h)
    .collect();
```

## 9. 実装優先順位

### Phase 1: 基本実装（Week 1）
1. PointerIterator実装
2. Vector::begin()/end()
3. 基本演算子（++, *, ==, !=）
4. for_each, find基本アルゴリズム

### Phase 2: 拡張機能（Week 2）
1. イテレータアダプタ（map, filter, take）
2. collect()メソッド
3. 双方向/ランダムアクセスイテレータ

### Phase 3: 統合（Week 3）
1. ビルトイン関数との統合
2. SafeIterator実装
3. STLアルゴリズム拡充

### Phase 4: 最適化（Week 4）
1. インライン化
2. SIMD最適化検討
3. 並列アルゴリズム基盤

## 10. テスト計画

```cm
// tests/std/iterator_test.cm

test("basic_iterator", || {
    Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    int sum = 0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum = sum + *it;
    }
    assert_eq(sum, 6, "iterator sum");
});

test("iterator_adapters", || {
    Vec<int> v;
    for (int i = 0; i < 10; i++) {
        v.push(i);
    }

    Vec<int> result = v.begin()
        .filter((int x) => { return x % 2 == 0; })
        .map((int x) => { return x * 2; })
        .take(3)
        .collect();

    assert_eq(result.len(), 3, "adapter chain length");
    assert_eq(result.get(0), 0, "first element");
    assert_eq(result.get(1), 4, "second element");
    assert_eq(result.get(2), 8, "third element");
});

test("iterator_invalidation", || {
    Vec<int> v;
    v.push(1);

    auto it = v.safe_iter();
    v.push(2);  // イテレータ無効化

    assert_panic(|| {
        it.current();  // パニックが期待される
    });
});
```

## 11. まとめ

このイテレータ設計により：

1. **C++風の表現力**: 演算子オーバーロードによる直感的な操作
2. **既存機能との共存**: ビルトインmap/filterと統合
3. **型安全性**: Cm言語の型システムを活用
4. **パフォーマンス**: ゼロコスト抽象化を目指す
5. **拡張性**: アダプタパターンによる柔軟な拡張

ビルトイン関数とイテレータベースの両アプローチを適切に使い分けることで、最高のパフォーマンスと使いやすさを実現します。