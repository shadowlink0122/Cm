# Cm言語マクロシステム設計

## 概要

Cm言語のマクロシステムは、C++の伝統的なプリプロセッサマクロと、モダンな言語のマクロシステムの良い部分を組み合わせた設計です。`@`や`$`のような特殊記号を避け、C++プログラマーにとって親しみやすい構文を採用しています。

## マクロの種類

### 1. Cm言語ネイティブマクロ

`#macro`キーワードを使用した、Cm言語の標準マクロ構文：

```cm
// 基本的なマクロ定義
#macro void LOG_ERROR(string message) {
    fprintf(stderr, "[ERROR] %s:%d - %s\n",
            __FILE__, __LINE__, message);
}

// ブロックを受け取るマクロ
#macro void WITH_MUTEX(mutex& m, block code) {
    m.lock();
    code;
    m.unlock();
}

// 型を返すマクロ
#macro auto MAX_VALUE(auto a, auto b) {
    return (a > b) ? a : b;
}

// 値の出力マクロ
#macro void DBG(auto value) {
    printf("[%s:%d] %s = ", __FILE__, __LINE__, #value);
    println(value);
}
```

### 2. プリプロセッサディレクティブ

条件付きコンパイルとシンプルな定数定義：

```cm
// 定数定義
#define PI 3.14159265359
#define MAX_SIZE 1024

// 条件付きコンパイル
#ifdef DEBUG
    #define LOG(msg) println(msg)
#else
    #define LOG(msg) // 何もしない
#endif
```

### 3. コンパイル時計算マクロ

```cm
// constexpr相当の機能
constexpr int ARRAY_SIZE = 256;

// コンパイル時関数
constexpr int factorial(int n) {
    return (n <= 1) ? 1 : n * factorial(n - 1);
}

// static_assert
static_assert(sizeof(int) == 4, "int must be 32 bits");
```

## 自動トレイト実装とディレクティブ

### 構造体の自動トレイト実装（withキーワード）

```cm
// withキーワードでトレイトを自動実装
struct Point with Debug, Clone, PartialEq {
    int x;
    int y;
};

struct Person with Debug, Display, Serialize {
    string name;
    int age;
    double height;
};
```

### 関数ディレクティブ

```cm
// テスト関数
#test void test_addition() {
    assert(1 + 1 == 2);
    assert(2 + 2 == 4, "Basic math should work");
}

// ベンチマーク関数
#bench void benchmark_sorting() {
    Vec<int> v = generate_random_vector(10000);
    sort(v);
}

// 廃止予定の関数
#deprecated("Use new_api() instead")
void old_api() {
    // 旧実装
}

// インライン化指示
#inline(always)
int hot_path() { return 42; }

// 最適化レベル指定
#optimize(none)
void debug_only_function() {
    // デバッグ用コード
}
```

## 条件付きコンパイル

### 伝統的なプリプロセッサ

```cm
#ifdef DEBUG
    #define LOG(msg) printf("[DEBUG] %s\n", msg)
#else
    #define LOG(msg) // 何もしない
#endif

#if defined(__linux__)
    // Linux固有のコード
#elif defined(_WIN32)
    // Windows固有のコード
#elif defined(__APPLE__)
    // macOS固有のコード
#else
    #error "Unsupported platform"
#endif
```

### feature検出

```cm
#if __has_feature(concepts)
    // C++20 conceptsを使用
#endif

#if __has_include(<optional>)
    #include <optional>
#else
    #include "my_optional.h"
#endif
```

## 特殊マクロ・定数

### 標準定義済みマクロ

```cm
__FILE__        // 現在のファイル名
__LINE__        // 現在の行番号
__func__        // 現在の関数名
__DATE__        // コンパイル日付
__TIME__        // コンパイル時刻
__COUNTER__     // 一意なカウンター

// Cm言語固有
__MODULE__      // 現在のモジュール名
__PACKAGE__     // パッケージ名
```

### アサーションマクロ

```cm
// 基本的なアサート
assert(condition);
assert_with_message(condition, "Error message");

// デバッグ専用アサート
debug_assert(condition);

// 静的アサート（コンパイル時）
static_assert(sizeof(void*) == 8, "64-bit system required");
```

## マクロ内での特殊機能

### 文字列化と連結

```cm
#define STRINGIFY(x) #x
#define CONCAT(a, b) a##b

// 使用例
STRINGIFY(hello)    // → "hello"
CONCAT(foo, bar)    // → foobar
```

### 可変長引数マクロ

```cm
// Cスタイル
#define DEBUG_PRINT(fmt, ...) \
    printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

// Cm言語スタイル
macro void LOG(string format, auto... args) {
    printf(("[" + __func__ + "] " + format).c_str(), args...);
}
```

## テンプレートとの統合

```cm
// テンプレート関数（マクロの代替）
template<typename T>
inline T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

// 可変長テンプレート
template<typename T, typename... Args>
T sum(T first, Args... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        return first + sum(rest...);
    }
}
```

## マクロ使用のベストプラクティス

### 推奨事項

1. **大文字を使用**: マクロ名は`UPPER_CASE`で記述
2. **括弧を使用**: マクロ引数は必ず括弧で囲む
3. **do-while(0)パターン**: 複数文のマクロで使用
4. **型安全性**: 可能な限りテンプレートまたはinline関数を使用

### 避けるべきこと

1. 副作用のある式をマクロ引数に渡さない
2. グローバルな名前空間を汚染しない
3. 複雑なロジックはマクロではなく関数で実装

## 実装段階

### Phase 1: 基本マクロサポート
- [x] `#define` プリプロセッサマクロ
- [x] `#ifdef`, `#ifndef`, `#if`, `#else`, `#endif`
- [x] `__FILE__`, `__LINE__`, `__func__`

### Phase 2: Cm言語マクロ
- [ ] `macro` キーワードサポート
- [ ] ブロックパラメータ
- [ ] 型推論（auto）

### Phase 3: 高度な機能
- [ ] `[[attribute]]` 構文
- [ ] `constexpr` サポート
- [ ] 可変長テンプレート
- [ ] コンセプト（concepts）

## 他言語との比較

| 機能 | C/C++ | Rust | Cm |
|------|-------|------|-----|
| プリプロセッサマクロ | ✅ | ❌ | ✅ |
| 衛生的マクロ | ❌ | ✅ | ✅ (`macro`) |
| 属性/アノテーション | ✅ (C++11) | ✅ | ✅ |
| コンパイル時実行 | ✅ (constexpr) | ✅ (const fn) | ✅ |
| パターンマッチング | ❌ | ✅ | 🔄 (計画中) |

## まとめ

Cm言語のマクロシステムは、C++プログラマーの既存知識を活かしながら、モダンな言語機能を段階的に導入できるよう設計されています。特殊記号（`@`、`$`）を避けることで、可読性と学習曲線の改善を図っています。