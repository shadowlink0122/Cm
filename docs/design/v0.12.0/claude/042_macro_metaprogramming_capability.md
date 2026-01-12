# マクロによるメタプログラミング：新しい構文と言語機能の創造

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 技術解説

## 重要な訂正

**インライン関数では新しい構文や言語機能は作れません。**
**それができるのはマクロです。**

## 機能の明確な区別

| 機能 | インライン関数 | マクロ |
|------|---------------|--------|
| **新しい構文を作れる** | ❌ 不可能 | ✅ 可能 |
| **新しい言語機能を作れる** | ❌ 不可能 | ✅ 可能 |
| **DSL（ドメイン特化言語）を作れる** | ❌ 不可能 | ✅ 可能 |
| **コンパイル時コード生成** | ❌ 不可能 | ✅ 可能 |
| **実行時パフォーマンス改善** | ✅ 可能 | ⚠️ 副次的に可能 |

## 1. インライン関数ができること（制限的）

インライン関数は**既存の言語機能の範囲内**でしか動作しません。

```rust
// Rustのインライン関数の例
#[inline]
fn add(a: i32, b: i32) -> i32 {
    a + b  // ただの関数、新しい構文ではない
}

// 使い方は通常の関数呼び出しと同じ
let result = add(3, 4);  // 新しい構文ではない！
```

### インライン関数の限界

```rust
// ❌ これはできない
#[inline]
fn create_syntax() {
    // 新しい構文 "my_keyword" を作ることは不可能
    // インライン関数は既存の構文内でしか動作しない
}

// ❌ これもできない
#[inline]
fn unless(condition: bool, block: impl Fn()) {
    if !condition {
        block();
    }
}

// unless(x > 5) {  // ❌ この構文は作れない
//     println!("x is not greater than 5");
// }
```

## 2. マクロができること（強力）

マクロは**新しい構文を作り、言語を拡張**できます。

### 2.1 新しい制御構文の作成

```rust
// Rustマクロで新しい制御構文を作る
macro_rules! unless {
    ($condition:expr, $block:block) => {
        if !$condition {
            $block
        }
    };
}

// 新しい構文として使える！
unless!(x > 5, {
    println!("x is not greater than 5");
});

// さらに高度な例：カスタムループ構文
macro_rules! repeat {
    ($n:expr, $body:block) => {
        for _ in 0..$n {
            $body
        }
    };
}

repeat!(5, {
    println!("Hello!");
});
```

### 2.2 DSL（ドメイン特化言語）の作成

```rust
// HTMLを書くような構文を作る
macro_rules! html {
    (< $tag:ident > $($inner:tt)* </ $tag2:ident >) => {
        {
            assert_eq!(stringify!($tag), stringify!($tag2));
            format!("<{}>{}</{}>",
                stringify!($tag),
                html!($($inner)*),
                stringify!($tag))
        }
    };
    ($text:expr) => {
        $text.to_string()
    };
}

// 新しいHTML風構文が使える！
let page = html! {
    <div>
        <h1>"Welcome"</h1>
        <p>"This is a paragraph"</p>
    </div>
};
```

### 2.3 新しい宣言構文

```rust
// SQLテーブル定義のような構文を作る
macro_rules! create_table {
    ($name:ident {
        $($field:ident : $ftype:ty),*
    }) => {
        #[derive(Debug)]
        struct $name {
            $($field: $ftype),*
        }

        impl $name {
            fn table_name() -> &'static str {
                stringify!($name)
            }
        }
    };
}

// SQL風の新しい構文！
create_table!(User {
    id: i32,
    name: String,
    email: String
});

// 上記は以下のコードを生成
// struct User {
//     id: i32,
//     name: String,
//     email: String,
// }
// impl User { ... }
```

### 2.4 パターンマッチング拡張

```rust
// JSONライクな構文を作る
macro_rules! json {
    // オブジェクト
    ({ $($key:literal : $value:tt),* }) => {
        {
            let mut map = std::collections::HashMap::new();
            $(
                map.insert($key, json!($value));
            )*
            JsonValue::Object(map)
        }
    };

    // 配列
    ([ $($element:tt),* ]) => {
        JsonValue::Array(vec![$(json!($element)),*])
    };

    // 値
    ($value:expr) => {
        JsonValue::from($value)
    };
}

// JavaScript風のJSON構文が使える！
let data = json!({
    "name": "Alice",
    "age": 30,
    "tags": ["rust", "programming"]
});
```

## 3. 実例：vec!マクロの内部実装

Rustの標準ライブラリの`vec!`マクロは新しい構文を提供します。

```rust
// vec!マクロの簡略版実装
macro_rules! vec {
    // 空のベクタ
    () => {
        Vec::new()
    };

    // 要素列挙
    ( $( $x:expr ),* ) => {
        {
            let mut temp_vec = Vec::new();
            $(
                temp_vec.push($x);
            )*
            temp_vec
        }
    };

    // 繰り返し構文
    ( $elem:expr ; $n:expr ) => {
        {
            let mut temp_vec = Vec::with_capacity($n);
            for _ in 0..$n {
                temp_vec.push($elem.clone());
            }
            temp_vec
        }
    };
}

// これらの新しい構文が使える
let v1 = vec![];                  // 空
let v2 = vec![1, 2, 3, 4, 5];    // 要素列挙
let v3 = vec![0; 100];            // 100個の0
```

**これはインライン関数では絶対に実現できません！**

## 4. なぜインライン関数では新構文を作れないのか

### 理由1: 処理タイミング

```
ソースコード → [構文解析] → AST → [型チェック] → [最適化（インライン化）] → 実行コード
                    ↑                                        ↑
                マクロはここ                        インライン関数はここ
```

- **マクロ**: 構文解析の段階で展開され、ASTを生成
- **インライン関数**: すでに構文解析が終わった後の最適化段階

### 理由2: 言語の制約

```rust
// インライン関数は言語の構文規則に従う必要がある
#[inline]
fn my_func() {
    // Rustの構文規則内でしか書けない
    let x = 5;      // OK
    // let x := 5;  // エラー！新しい構文 := は作れない
}

// マクロは構文を生成できる
macro_rules! my_let {
    ($var:ident := $val:expr) => {
        let $var = $val;
    };
}

my_let!(x := 5);  // OK！新しい構文が使える
```

## 5. C言語/C++での例

### C言語のマクロ（危険だが強力）

```c
// 新しい制御構文を作る
#define forever for(;;)
#define unless(cond) if(!(cond))

// 使用例
forever {
    // 無限ループの新構文
}

unless(x > 5) {
    printf("x is not greater than 5\n");
}

// Pythonのような範囲ループ
#define for_range(i, start, end) \
    for(int i = start; i < end; i++)

for_range(i, 0, 10) {
    printf("%d\n", i);
}
```

### C++のテンプレート（インライン化されるが新構文ではない）

```cpp
// テンプレート関数（インライン化可能）
template<typename T>
inline T max(T a, T b) {
    return a > b ? a : b;
}

// でも構文は変わらない
int result = max(3, 4);  // 通常の関数呼び出し構文

// ❌ 新しい構文は作れない
// max 3 and 4;  // こんな構文は作れない
```

## 6. 実用例：テストフレームワーク

### マクロを使った場合（新構文可能）

```rust
macro_rules! test_case {
    ($name:ident : $test:expr => $expected:expr) => {
        #[test]
        fn $name() {
            assert_eq!($test, $expected);
        }
    };
}

// 新しいテスト構文！
test_case!(addition: 2 + 2 => 4);
test_case!(subtraction: 5 - 3 => 2);
test_case!(multiplication: 3 * 4 => 12);
```

### インライン関数の場合（新構文不可）

```rust
#[inline]
fn test_equal<T: PartialEq>(actual: T, expected: T) {
    assert_eq!(actual, expected);
}

// 通常の関数呼び出しのみ
test_equal(2 + 2, 4);  // 新しい構文ではない
```

## 7. Cm言語への提案

### 現在：インライン関数のみ（提案中）

```cm
#[inline]
int add(int a, int b) {
    return a + b;
}

// 通常の関数呼び出しのみ可能
int result = add(3, 4);
```

### 将来：マクロシステムの追加で可能になること

```cm
// 新しい制御構文
macro_rules! unless {
    ($cond:expr) $body:block => {
        if (!$cond) $body
    }
}

unless (x > 5) {
    println("x is not greater than 5");
}

// アサーションマクロ
macro_rules! assert_eq {
    ($left:expr, $right:expr, $msg:expr) => {
        if ($left != $right) {
            panic("Assertion failed: {msg}\n  left: {left}\n  right: {right}");
        }
    }
}

assert_eq!(result, 42, "Result should be 42");

// DSL
macro_rules! sql {
    (SELECT * FROM $table:ident WHERE $field:ident = $value:expr) => {
        db.query("SELECT * FROM " + stringify!($table) +
                " WHERE " + stringify!($field) + " = ?", $value)
    }
}

let users = sql!(SELECT * FROM users WHERE age = 25);
```

## まとめ

### できること・できないこと

| | インライン関数 | マクロ |
|---|--------------|--------|
| **関数呼び出しの最適化** | ✅ 主目的 | ❌ 主目的ではない |
| **新しい構文の作成** | ❌ 不可能 | ✅ 可能 |
| **DSLの作成** | ❌ 不可能 | ✅ 可能 |
| **コンパイル時コード生成** | ❌ 不可能 | ✅ 可能 |
| **言語機能の拡張** | ❌ 不可能 | ✅ 可能 |

### 結論

- **インライン関数**: パフォーマンス最適化のための機能
- **マクロ**: 新しい構文や言語機能を作るためのメタプログラミング機能

**「構文レベルのコード生成」ができるのはマクロだけです。インライン関数は既存の構文の範囲内でしか動作しません。**

---

**作成者:** Claude Code
**ステータス:** 技術解説
**関連文書:** 040_inline_expansion_design.md, 041_inline_vs_macro_comparison.md