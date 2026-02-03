---
title: マクロ
parent: Advanced
nav_order: 2
---

# マクロ

Cmのマクロシステムは、コンパイル時にコードを展開する機能を提供します。C言語の`#define`に似ていますが、型安全です。

## 定数マクロ

### 基本的な使い方

```cm
// 定数マクロの定義
macro int VERSION = 13;
macro string APP_NAME = "MyApp";
macro bool DEBUG = true;

int main() {
    println("Version: {VERSION}");    // Version: 13
    println("App: {APP_NAME}");       // App: MyApp
    
    if (DEBUG) {
        println("Debug mode");
    }
    
    return 0;
}
```

### サポートされる型

| 型 | 例 |
|---|-----|
| `int` | `macro int SIZE = 100;` |
| `string` | `macro string NAME = "Hello";` |
| `bool` | `macro bool ENABLED = true;` |

## 関数マクロ

### 引数なし関数マクロ

```cm
macro int VERSION = 13;

// 引数なし関数マクロ
macro int*() get_version = () => VERSION;

int main() {
    int v = get_version();  // 展開: VERSION -> 13
    println(v);             // 13
    return 0;
}
```

### 引数あり関数マクロ

```cm
// 引数あり関数マクロ
macro int*(int, int) add = (int a, int b) => a + b;
macro int*(int, int) mul = (int a, int b) => a * b;

int main() {
    int sum = add(3, 5);       // 展開: 3 + 5 -> 8
    int product = mul(4, 6);   // 展開: 4 * 6 -> 24
    
    println("Sum: {sum}");         // Sum: 8
    println("Product: {product}"); // Product: 24
    
    // マクロの組み合わせ
    int combined = add(mul(2, 3), 4);  // (2*3) + 4 = 10
    println("Combined: {combined}");    // Combined: 10
    
    return 0;
}
```

### 関数マクロの構文

```cm
macro 戻り値型*(引数型1, 引数型2, ...) マクロ名 = (引数1, 引数2, ...) => 式;
```

## マクロ vs const

| 特徴 | macro | const |
|------|-------|-------|
| 展開タイミング | コンパイル前 | コンパイル時 |
| 関数形式 | ✅ 対応 | ❌ 非対応 |
| 型チェック | ✅ 型付き | ✅ 型付き |
| 用途 | 条件付きコンパイル、DSL | 定数値 |

## 使用例: デバッグマクロ

```cm
macro bool DEBUG = true;

macro void*(string) debug_log = (string msg) => {
    if (DEBUG) {
        println("[DEBUG] {msg}");
    }
};

int main() {
    debug_log("Starting...");  // DEBUGがtrueの場合のみ出力
    
    // メイン処理
    int result = 42;
    
    debug_log("Result: {result}");
    
    return 0;
}
```

## 注意事項

- マクロはグローバルスコープでのみ定義可能
- マクロは定義より前では使用不可
- 循環参照は許可されない

## 関連項目

- [定数と定数式](const.md) - constキーワード