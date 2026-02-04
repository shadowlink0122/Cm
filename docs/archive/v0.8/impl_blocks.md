[English](impl_blocks.en.html)

# Cm言語 impl ブロック設計

## ジェネリック関数の構文（オプション3）

```cm
// 簡潔で明確な構文
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

// 複数の型パラメータ
<T, U> U convert(T from, U default_val) {
    // 実装
}

// 複雑な制約
<T: Clone + Debug, U: Default> U process(T input) {
    // 実装
}
```

## impl ブロックの2つの形式

### 1. 基本 impl（コンストラクタ・デストラクタのみ）

```cm
// コンストラクタとデストラクタのみ定義可能
impl<T> Vec<T> {
    // デフォルトコンストラクタ（overload不要）
    self() {
        this.data = null;
        this.size = 0;
        this.capacity = 0;
    }

    // 引数付きコンストラクタ（overload必須）
    overload self(size_t initial_capacity) {
        this.data = new T[initial_capacity];
        this.size = 0;
        this.capacity = initial_capacity;
    }

    // デフォルト引数付きコンストラクタ
    overload self(T initial_value, size_t count = 10) {
        this.data = new T[count];
        this.size = count;
        this.capacity = count;
        for (size_t i = 0; i < count; i++) {
            this.data[i] = initial_value;
        }
    }

    // コピーコンストラクタ
    overload self(const Vec<T>& other) {
        this.size = other.size;
        this.capacity = other.capacity;
        this.data = new T[this.capacity];
        for (size_t i = 0; i < this.size; i++) {
            this.data[i] = other.data[i];
        }
    }

    // ムーブコンストラクタ
    overload self(Vec<T>&& other) {
        this.data = other.data;
        this.size = other.size;
        this.capacity = other.capacity;
        other.data = null;
        other.size = 0;
        other.capacity = 0;
    }

    // デストラクタ（1つのみ）
    ~self() {
        if (this.data != null) {
            delete[] this.data;
        }
    }
}
```

### 2. impl for（メソッド実装）

```cm
// インターフェース/トレイトの実装
impl<T> Vec<T> for Container<T> {
    // 通常の関数として記述（型パラメータは既にimplで宣言済み）
    void push(T item) {
        if (size >= capacity) {
            grow();
        }
        data[size++] = item;
    }

    T pop() {
        if (size == 0) {
            panic("Pop from empty vector");
        }
        return data[--size];
    }

    bool is_empty() {
        return size == 0;
    }

    size_t len() {
        return size;
    }

    // デフォルト引数も使用可能
    T get(size_t index, T default_value = T()) {
        if (index >= size) {
            return default_value;
        }
        return data[index];
    }

    // 可変長引数
    void append(T... items) {
        for (T item : items) {
            push(item);
        }
    }

    private void grow() {
        size_t new_capacity = capacity * 2;
        T* new_data = new T[new_capacity];
        for (size_t i = 0; i < size; i++) {
            new_data[i] = data[i];
        }
        delete[] data;
        data = new_data;
        capacity = new_capacity;
    }
}

// 複数のトレイトを実装
impl<T: Display> Vec<T> for Printable {
    void print() {
        print("[");
        for (size_t i = 0; i < size; i++) {
            if (i > 0) print(", ");
            print(data[i]);
        }
        println("]");
    }
}

impl<T: Ord> Vec<T> for Sortable {
    void sort() {
        // クイックソート実装
        quick_sort(0, size - 1);
    }

    private void quick_sort(size_t left, size_t right) {
        if (left < right) {
            size_t pivot = partition(left, right);
            if (pivot > 0) quick_sort(left, pivot - 1);
            quick_sort(pivot + 1, right);
        }
    }

    private size_t partition(size_t left, size_t right) {
        T pivot = data[right];
        size_t i = left;
        for (size_t j = left; j < right; j++) {
            if (data[j] <= pivot) {
                swap(data[i], data[j]);
                i++;
            }
        }
        swap(data[i], data[right]);
        return i;
    }
}
```

## 構造体定義

```cm
// 構造体自体の定義
struct Vec<T> {
    private T* data;
    private size_t size;
    private size_t capacity;
}

// インターフェース定義
interface Container<T> {
    void push(T item);
    T pop();
    bool is_empty();
    size_t len();
}

interface Printable {
    void print();
}

interface Sortable {
    void sort();
}
```

## 使用例

```cm
// コンストラクタの使用
int main() {
    // デフォルトコンストラクタ
    Vec<int> v1;

    // 引数付きコンストラクタ
    Vec<int> v2(100);  // 初期容量100

    // デフォルト引数付きコンストラクタ
    Vec<int> v3(0, 10);     // 0を10個
    Vec<int> v4(42);        // 42を10個（デフォルト）

    // コピーコンストラクタ
    Vec<int> v5(v3);

    // ムーブコンストラクタ
    Vec<int> v6(move(v4));

    // メソッドの使用
    v1.push(1);
    v1.push(2);
    v1.push(3);

    int val = v1.pop();  // 3

    // デフォルト引数付きメソッド
    int x = v1.get(10);      // デフォルト値（0）が返る
    int y = v1.get(10, -1);  // -1が返る

    // 可変長引数メソッド
    v1.append(4, 5, 6, 7, 8);

    // トレイトメソッド
    v1.print();  // [1, 2, 4, 5, 6, 7, 8]
    v1.sort();   // ソート

    return 0;
}  // ここで自動的に~self()が呼ばれる
```

## より複雑な例

```cm
// スマートポインタ
struct SharedPtr<T> {
    private T* ptr;
    private int* ref_count;
}

impl<T> SharedPtr<T> {
    // デフォルトコンストラクタ（overload不要）
    self() {
        this.ptr = null;
        this.ref_count = null;
    }

    // 値からの構築（overload必須）
    overload self(T value) {
        this.ptr = new T(move(value));
        this.ref_count = new int(1);
    }

    // コピーコンストラクタ
    overload self(const SharedPtr<T>& other) {
        this.ptr = other.ptr;
        this.ref_count = other.ref_count;
        if (ref_count != null) {
            (*ref_count)++;
        }
    }

    // デストラクタ
    ~self() {
        if (ref_count != null) {
            (*ref_count)--;
            if (*ref_count == 0) {
                delete ptr;
                delete ref_count;
            }
        }
    }
}

impl<T> SharedPtr<T> for Deref<T> {
    T& operator*() {
        return *ptr;
    }

    T* operator->() {
        return ptr;
    }

    bool is_null() {
        return ptr == null;
    }

    int use_count() {
        return ref_count ? *ref_count : 0;
    }
}

// Result型
struct Result<T, E = Error> {
    private enum { Ok, Err } tag;
    private union {
        T ok_value;
        E err_value;
    };
}

impl<T, E> Result<T, E> {
    // Ok値からの構築（overload不要）
    self(T value) {
        this.tag = Ok;
        this.ok_value = move(value);
    }

    // エラーからの構築（タグ付き、overload必須）
    overload self(E error, bool is_error) {
        this.tag = Err;
        this.err_value = move(error);
    }

    ~self() {
        // unionのクリーンアップ
        if (tag == Ok) {
            ok_value.~T();
        } else {
            err_value.~E();
        }
    }
}

impl<T, E> Result<T, E> for Monad<T> {
    bool is_ok() {
        return tag == Ok;
    }

    bool is_err() {
        return tag == Err;
    }

    T unwrap() {
        if (tag != Ok) {
            panic("Called unwrap on Err");
        }
        return ok_value;
    }

    T unwrap_or(T default_value) {
        if (tag == Ok) {
            return ok_value;
        }
        return default_value;
    }

    <U> Result<U, E> map(U mapper(T)) {
        if (tag == Ok) {
            return Result<U, E>(mapper(ok_value));
        }
        return Result<U, E>(err_value, true);
    }

    <F> Result<T, F> map_err(F mapper(E)) {
        if (tag == Err) {
            return Result<T, F>(mapper(err_value), true);
        }
        return Result<T, F>(ok_value);
    }
}
```

## 設計原則

1. **明確な役割分担**
   - `impl<T> Type<T>`: コンストラクタ/デストラクタのみ
   - `impl<T> Type<T> for Trait`: メソッド実装

2. **一貫性**
   - メソッドは通常の関数と同じ構文
   - ジェネリクスは常に明示的に宣言

3. **柔軟性**
   - デフォルト引数のサポート
   - 可変長引数のサポート
   - オーバーロード可能なコンストラクタ

4. **安全性**
   - 明示的なムーブセマンティクス
   - 自動的なデストラクタ呼び出し
   - RAII原則の遵守

## コンパイラ実装の考慮事項

1. **名前マングリング**
   ```
   Vec<int>::self()        -> _ZN3VecIiEC1Ev
   Vec<int>::self(size_t)  -> _ZN3VecIiEC1Em
   Vec<int>::~self()       -> _ZN3VecIiED1Ev
   ```

2. **仮想関数テーブル**
   - インターフェース実装時のみvtable生成
   - 基本implは静的ディスパッチ

3. **テンプレートインスタンス化**
   - 使用時に具体型で展開
   - 重複排除の最適化