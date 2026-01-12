# Cm言語 Pin（メモリ固定）ライブラリ設計（Cm構文完全準拠版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 最終改訂
関連文書: 060_cm_macro_system_design.md

## エグゼクティブサマリー

Cm言語でメモリ上の値の移動を防ぐPin機能を提供します。**Unpinを廃止し、Cm構文に100%準拠した直感的な設計**を実現します。

## 1. 設計理念

### 1.1 基本方針

1. **Cm構文厳守**: autoを使わず、<T>ジェネリクス構文を使用
2. **シンプルさ優先**: Unpinのような複雑な概念を排除
3. **明示的な固定**: Pinで囲んだものだけが固定される
4. **ゼロコスト抽象化**: 実行時オーバーヘッドなし

### 1.2 構文規則

| 要素 | 誤った例（auto/template） | 正しい例（Cm構文） |
|------|------------------------|-----------------|
| 変数宣言 | `auto v = value;` | `int v = value;` |
| ジェネリクス | `template<typename T>` | `<T>` |
| Pin宣言 | `auto p = Pin(v);` | `Pin<int> p(v);` |

## 2. 基本設計

### 2.1 Pin型の定義（Cm構文）

```cm
// Cmジェネリクス構文（struct Name<T>）
struct Pin<T> {
private:
    T* pointer;  // 固定された値へのポインタ

public:
    // コンストラクタ（移動を防ぐ）
    explicit Pin(T& value) : pointer(&value) {
        // アドレスを記録して移動を検出
        #ifdef DEBUG
        register_pinned_address(pointer);
        #endif
    }

    // デストラクタ
    ~Pin() {
        #ifdef DEBUG
        unregister_pinned_address(pointer);
        #endif
    }

    // 値への読み取り専用アクセス
    const T& operator*() const {
        return *pointer;
    }

    const T* operator->() const {
        return pointer;
    }

    // 可変アクセス（安全性を保証）
    T& get_mut() {
        // Pinされた値は移動できないので安全
        return *pointer;
    }

    // コピー・ムーブ禁止
    Pin(const Pin&) = delete;
    Pin& operator=(const Pin&) = delete;
    Pin(Pin&&) = delete;
    Pin& operator=(Pin&&) = delete;
};
```

### 2.2 使用例（Cm構文厳守）

```cm
struct SelfReferential {
    int value;
    int* self_ptr;  // 自己参照ポインタ

    void init() {
        self_ptr = &value;  // 自己参照を設定
    }
};

int main() {
    // 明示的な型宣言（autoは使わない）
    SelfReferential obj;
    obj.init();

    // Pinで固定（型を明示）
    Pin<SelfReferential> pinned(obj);

    // アクセス
    printf("Value: %d\n", pinned->value);

    // これはコンパイルエラー
    // SelfReferential moved = obj;  // Error: obj is pinned

    return 0;
}
```

## 3. マクロによる簡易化（Cm構文版）

### 3.1 pinマクロ（060の新設計に準拠）

```cm
// 値を固定するマクロ
macro pin<T>(T& value) {
    Pin<T> pinned(value);
    return pinned;
}

// ヒープ上に作成して固定
macro pin_box<T>(T value) {
    Box<T> boxed = Box<T>::new(value);
    Pin<Box<T>> pinned(boxed);
    return pinned;
}

// 使用例（型を明示）
int main() {
    int value = 42;
    Pin<int> p1 = pin(value);

    SelfReferential obj;
    Pin<SelfReferential> p2 = pin(obj);

    // ヒープ上で固定
    Pin<Box<int>> p3 = pin_box(100);

    return 0;
}
```

## 4. スマートポインタとの統合

### 4.1 Box<T>との統合（Cm構文）

```cm
// Boxの拡張（ジェネリクス使用）
struct Box<T> {
    T* ptr;

    // Pin作成用の静的メソッド
    static Pin<Box<T>> pin(T value) {
        Box<T> boxed;
        boxed.ptr = new T(value);
        Pin<Box<T>> pinned(boxed);
        return pinned;
    }

    T& operator*() { return *ptr; }
    T* operator->() { return ptr; }
};

// 使用例
int main() {
    // 型を明示的に指定
    Pin<Box<SelfReferential>> pinned = Box<SelfReferential>::pin(
        SelfReferential{42, nullptr}
    );

    pinned->init();  // 安全に自己参照を設定

    return 0;
}
```

### 4.2 Rc/Arcとの統合

```cm
// 参照カウント型でのPin
struct Rc<T> {
    T* ptr;
    int* count;

    static Pin<Rc<T>> pin(T value) {
        Rc<T> rc;
        rc.ptr = new T(value);
        rc.count = new int(1);
        Pin<Rc<T>> pinned(rc);
        return pinned;
    }
};
```

## 5. 非同期処理との統合

### 5.1 Future/Promiseでの使用

```cm
// 非同期タスクの状態をPin
struct AsyncTask {
    enum State {
        PENDING,
        READY,
        COMPLETED
    };

    State state;
    void* context;  // 実行コンテキスト（移動不可）

    // Pollメソッド（Pinが必要）
    State poll(Pin<AsyncTask>& self) {
        // selfは移動しないことが保証される
        switch (self->state) {
            case PENDING:
                // 非同期処理を進める
                break;
            case READY:
                // 結果を処理
                break;
            default:
                break;
        }
        return self->state;
    }
};
```

### 5.2 async/awaitとの統合

```cm
// async関数の戻り値の型
struct Future<T> {
    T result;
    bool ready;
};

// async関数（コンパイラが内部でPinを管理）
async Future<int> fetch_data() {
    int local_state = 0;  // awaitを跨いで保持される

    await delay(1000);  // ここで中断・再開される

    return local_state + 42;
}

int main() {
    // async関数の返り値を取得（型を明示）
    Future<int> future = fetch_data();

    // 手動でpollする場合
    Pin<Future<int>> pinned_future = pin(future);

    // ポーリング
    while (!pinned_future->ready) {
        // 待機
    }

    return pinned_future->result;
}
```

## 6. 安全性保証

### 6.1 コンパイル時チェック

```cm
// コンパイラが以下を保証
<T>
void compile_time_checks() {
    // 1. Pinされた値は移動できない
    T value;
    Pin<T> pinned(value);
    // T moved = value;  // コンパイルエラー

    // 2. Pinはコピーできない
    // Pin<T> copy = pinned;  // コンパイルエラー

    // 3. Pinはムーブできない
    // Pin<T> moved = std::move(pinned);  // コンパイルエラー
}
```

### 6.2 実行時チェック（デバッグモード）

```cm
#ifdef DEBUG
// デバッグモードでアドレス追跡
static Set<void*> pinned_addresses;

void register_pinned_address(void* addr) {
    if (pinned_addresses.contains(addr)) {
        panic("Address already pinned!");
    }
    pinned_addresses.insert(addr);
}

void unregister_pinned_address(void* addr) {
    pinned_addresses.erase(addr);
}
#endif
```

## 7. 標準ライブラリでの活用

### 7.1 コンテナでのPin

```cm
// Vectorに格納されたPinされた要素
struct PinnedVector<T> {
private:
    Vector<Box<T>> storage;  // ヒープ確保で移動を防ぐ

public:
    Pin<T>& push(T value) {
        Box<T> boxed = Box<T>::new(value);
        storage.push(boxed);
        Pin<T> pinned(storage.back());
        return pinned;
    }

    Pin<T>& operator[](size_t index) {
        Pin<T> pinned(storage[index]);
        return pinned;
    }
};
```

### 7.2 イテレータとPin

```cm
// Pinされた要素のイテレータ
struct PinnedIterator<T> {
    Pin<T>* current;

    Pin<T>& operator*() {
        return *current;
    }

    PinnedIterator& operator++() {
        ++current;
        return *this;
    }
};
```

## 8. パフォーマンス最適化

### 8.1 ゼロコスト抽象化

```cm
// リリースビルドでは追加コストなし
// Pin<T>のメソッド実装（Tは既に定義済み）
inline T& Pin<T>::get_mut() {
    // インライン化により関数呼び出しオーバーヘッドなし
    return *pointer;
}

// コンパイラ最適化ヒント
[[always_inline]]
const T& Pin<T>::operator*() const {
    return *pointer;
}
```

### 8.2 メモリレイアウト

```cm
// Pinは単一ポインタのみ（サイズ最小）
static_assert(sizeof(Pin<int>) == sizeof(int*));

// アライメントも同じ
static_assert(alignof(Pin<int>) == alignof(int*));
```

## 9. 使用ガイドライン

### 9.1 いつPinを使うべきか

```cm
// 必要な場合：
// 1. 自己参照構造体
struct SelfRef {
    int data;
    int* ptr_to_data;  // &data を保持
};

// 2. 非同期タスクの状態
struct AsyncState {
    void* stack_frame;  // 中断時のスタック
};

// 3. コールバックの登録
struct Callback {
    void (*func)(void*);
    void* context;  // thisポインタなど
};
```

### 9.2 Pinの正しい使い方

```cm
// ✅ 良い例：型を明示
int main() {
    SelfRef obj;
    Pin<SelfRef> pinned = pin(obj);
    pinned->ptr_to_data = &pinned->data;
}

// ❌ 悪い例：autoの使用
int main() {
    auto obj = SelfRef();  // autoは非推奨
    auto pinned = pin(obj);  // 型が不明確
}
```

## 10. まとめ

CmのPin設計は：

1. **Cm構文100%準拠**: autoを使わず、<T>ジェネリクス使用
2. **シンプル**: Unpinを廃止し、直感的な固定機能のみ
3. **安全**: コンパイル時と実行時の両方でチェック
4. **効率的**: ゼロコスト抽象化
5. **実用的**: 非同期処理や自己参照構造体で活用

Rustの複雑さを避け、Cmらしいシンプルで型安全な設計を実現しました。

---

**作成者:** Claude Code
**ステータス:** 最終設計
**実装準備:** 完了