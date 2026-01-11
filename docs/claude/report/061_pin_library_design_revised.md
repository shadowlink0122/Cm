# Cm言語 Pin（メモリ固定）ライブラリ設計（改訂版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計改訂
関連文書: 060_cm_macro_system_design_revised.md

## エグゼクティブサマリー

Cm言語でメモリ上の値の移動を防ぐPin機能を提供します。**Rustの複雑なUnpin設計を排除し**、より直感的で理解しやすいAPIを提供します。

## 1. 設計理念

### 1.1 基本方針

1. **シンプルさ優先**: Unpinのような複雑な概念を排除
2. **明示的な固定**: Pinで囲んだものだけが固定される
3. **C++風の構文**: Cm言語の構文スタイルに準拠
4. **ゼロコスト抽象化**: 実行時オーバーヘッドなし

### 1.2 Rustの問題点と改善

| Rustの問題 | Cmの解決策 |
|-----------|-----------|
| Unpinトレイトの複雑性 | 廃止：デフォルトで移動可能 |
| !Unpinの理解困難性 | 不要：Pinで明示的に固定 |
| PhantomPinnedの必要性 | 不要：シンプルな設計 |
| 暗黙的な制約 | 明示的な固定のみ |

## 2. 基本設計

### 2.1 Pin型の定義

```cm
// Cm構文：テンプレート宣言
template<typename T>
struct Pin {
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

### 2.2 使用例

```cm
// Cm構文での宣言と使用
struct SelfReferential {
    int value;
    int* self_ptr;  // 自己参照ポインタ

    void init() {
        self_ptr = &value;  // 自己参照を設定
    }
};

int main() {
    // Pin型の宣言（Cm風）
    SelfReferential obj;
    obj.init();

    // Pinで固定
    Pin<SelfReferential> pinned(obj);

    // アクセス
    printf("Value: %d\n", pinned->value);

    // これはコンパイルエラー
    // SelfReferential moved = obj;  // Error: obj is pinned

    return 0;
}
```

## 3. マクロによる簡易化

### 3.1 pin!マクロ（Cm構文版）

```cm
// macro_rules!をCm風に調整
macro pin {
    // パターン1: 値を固定
    ($value:expr) => {{
        Pin<typeof($value)> __pin($value);
        __pin
    }};

    // パターン2: 新規変数として固定
    ($type:ty, $name:ident = $init:expr) => {
        $type $name = $init;
        Pin<$type> __pin_##$name($name);
    };
}
```

### 3.2 使用例

```cm
int main() {
    // マクロで簡単にPin作成
    int value = 42;
    auto pinned = pin!(value);

    // 構造体の場合
    SelfReferential obj;
    auto pinned_obj = pin!(obj);

    return 0;
}
```

## 4. スマートポインタとの統合

### 4.1 Box<T>との統合

```cm
// Boxに入れた値をPin
template<typename T>
Pin<Box<T>> Box<T>::pin(T value) {
    Box<T> boxed = Box<T>::new(value);
    return Pin<Box<T>>(boxed);
}

// 使用例
int main() {
    // ヒープ上でPin
    auto pinned_box = Box<SelfReferential>::pin(
        SelfReferential{42, nullptr}
    );

    pinned_box->init();  // 安全に自己参照を設定

    return 0;
}
```

### 4.2 Rc/Arcとの統合

```cm
// 参照カウント型でのPin
template<typename T>
Pin<Rc<T>> Rc<T>::pin(T value) {
    Rc<T> rc = Rc<T>::new(value);
    return Pin<Rc<T>>(rc);
}
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
// async関数の内部状態はPinされる
async int fetch_data() {
    // コンパイラが自動的にPinを管理
    int local_state = 0;  // awaitを跨いで保持される

    await delay(1000);  // ここで中断・再開される

    return local_state + 42;
}

int main() {
    // async関数の返り値は自動的にPinされる
    auto future = fetch_data();

    // 手動でpollする場合
    Pin<decltype(future)> pinned_future(future);
    while (pinned_future.poll() == AsyncTask::PENDING) {
        // 待機
    }

    return 0;
}
```

## 6. 安全性保証

### 6.1 コンパイル時チェック

```cm
// コンパイラが以下を保証
template<typename T>
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
static std::set<void*> pinned_addresses;

void register_pinned_address(void* addr) {
    if (pinned_addresses.count(addr) > 0) {
        panic("Address already pinned!");
    }
    pinned_addresses.insert(addr);
}

void unregister_pinned_address(void* addr) {
    pinned_addresses.erase(addr);
}

// ムーブ検出
void check_move(void* old_addr, void* new_addr) {
    if (pinned_addresses.count(old_addr) > 0) {
        panic("Attempted to move pinned value!");
    }
}
#endif
```

## 7. 標準ライブラリでの活用

### 7.1 コンテナでのPin

```cm
// Vectorに格納されたPinされた要素
template<typename T>
struct PinnedVector {
private:
    Vector<Box<T>> storage;  // ヒープ確保で移動を防ぐ

public:
    Pin<T>& push(T value) {
        Box<T> boxed = Box<T>::new(value);
        storage.push(boxed);
        return Pin<T>(storage.back());
    }

    Pin<T>& operator[](size_t index) {
        return Pin<T>(storage[index]);
    }
};
```

### 7.2 イテレータとPin

```cm
// Pinされた要素のイテレータ
template<typename T>
struct PinnedIterator {
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
template<typename T>
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

## 9. 他言語との比較

| 機能 | Cm | Rust | C++ |
|------|-----|------|-----|
| メモリ固定 | Pin<T> | Pin<P> | なし |
| 移動防止 | ✅ | ✅ | std::pin（C++20） |
| Unpinの複雑性 | なし | あり | N/A |
| 自己参照構造体 | ✅簡単 | ✅複雑 | ⚠️危険 |
| ゼロコスト | ✅ | ✅ | ✅ |
| 直感的API | ✅ | ❌ | ⚠️ |

## 10. まとめ

CmのPin設計は：

1. **シンプル**: Unpinを廃止し、直感的な固定機能のみ提供
2. **安全**: コンパイル時と実行時の両方でチェック
3. **効率的**: ゼロコスト抽象化
4. **Cm風**: 言語の構文スタイルに完全準拠
5. **実用的**: 非同期処理や自己参照構造体で活用

Rustの複雑な歴史を繰り返さず、より良い設計を実現します。

---

**作成者:** Claude Code
**ステータス:** 設計改訂
**次文書:** 060_cm_macro_system_design_revised.md