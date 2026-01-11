# Cm言語マクロシステム設計書（改訂版）

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計改訂

## エグゼクティブサマリー

Cm言語に安全で衛生的なマクロシステムを導入します。**Rust風の構文を避け、C++風のCm言語スタイルに準拠した設計**を行います。

## 1. 設計理念

### 1.1 基本方針

1. **C++風構文の維持**: Cm言語の構文スタイルを尊重
2. **衛生的（Hygienic）**: 変数名の衝突を防ぐ
3. **型安全**: マクロ展開後も型チェックを保証
4. **直感的**: C++プログラマーにも理解しやすい

### 1.2 マクロの種類

```cm
// 1. 関数風マクロ（C++風）
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 2. パターンマクロ（新機能）
macro vec {
    () => { Vector::new() };
    ($x:expr, ...) => {{
        Vector<typeof($x)> v;
        v.push($x);
        ...
        v
    }};
}

// 3. 属性マクロ（将来実装）
[[derive(Debug, Clone)]]
struct Point { int x; int y; };
```

## 2. パターンマクロ設計

### 2.1 構文定義（Cm風）

```ebnf
macro_definition ::= 'macro' identifier '{' macro_rules '}'

macro_rules ::= macro_rule (';' macro_rule)* ';'?

macro_rule ::= macro_pattern '=>' macro_body

macro_pattern ::= '(' pattern_elements ')'

macro_body ::= expression | '{' statements '}'

pattern_element ::= token
                  | '$' identifier ':' type_spec
                  | '$' identifier '...'
                  | '...'

type_spec ::= 'expr' | 'stmt' | 'type' | 'ident'
            | 'literal' | 'block'
```

### 2.2 フラグメント指定子（簡略化）

| 指定子 | マッチ対象 | 例 |
|--------|-----------|-----|
| `expr` | 式 | `x + 1`, `func()` |
| `stmt` | 文 | `int x = 5;` |
| `type` | 型 | `int`, `Vector<T>` |
| `ident` | 識別子 | `foo`, `x` |
| `literal` | リテラル | `42`, `"hello"` |
| `block` | ブロック | `{ ... }` |

## 3. 基本マクロ例

### 3.1 vec!マクロ（Cm構文版）

```cm
// Vectorを簡単に作成するマクロ
macro vec {
    // 空のVector
    () => {
        Vector<int>()
    };

    // 単一型の要素
    ($first:expr) => {{
        Vector<typeof($first)> v;
        v.push($first);
        v
    }};

    // 複数要素（可変長）
    ($first:expr, $rest:expr...) => {{
        Vector<typeof($first)> v;
        v.push($first);
        $(v.push($rest);)...
        v
    }};
}

// 使用例
int main() {
    auto v1 = vec!();           // 空のVector
    auto v2 = vec!(1);           // Vector<int> with 1
    auto v3 = vec!(1, 2, 3);    // Vector<int> with 1,2,3

    return 0;
}
```

### 3.2 assert!マクロ（Cm構文版）

```cm
// アサーションマクロ
macro assert {
    ($cond:expr) => {
        if (!($cond)) {
            fprintf(stderr, "Assertion failed: %s\n", #$cond);
            abort();
        }
    };

    ($cond:expr, $msg:literal) => {
        if (!($cond)) {
            fprintf(stderr, "Assertion failed: %s\n  Message: %s\n",
                   #$cond, $msg);
            abort();
        }
    };
}

// 使用例
void test() {
    int x = 5;
    assert!(x > 0);
    assert!(x < 10, "x must be less than 10");
}
```

### 3.3 println!マクロ（Cm構文版）

```cm
// 改行付き出力マクロ
macro println {
    () => {
        printf("\n");
    };

    ($fmt:literal) => {
        printf($fmt "\n");
    };

    ($fmt:literal, $args:expr...) => {
        printf($fmt "\n", $args...);
    };
}

// 使用例
int main() {
    println!();                        // 改行のみ
    println!("Hello, World!");        // 文字列出力
    println!("Value: %d", 42);        // フォーマット付き

    return 0;
}
```

## 4. Pin実装用マクロ

### 4.1 pin!マクロ（Cm構文版）

```cm
// Pin作成マクロ
macro pin {
    ($val:ident) => {{
        Pin<typeof($val)> __pinned($val);
        __pinned
    }};

    ($type:type $name:ident = $init:expr) => {{
        $type $name = $init;
        Pin<$type> __pinned_##$name($name);
        __pinned_##$name
    }};
}

// 使用例
int main() {
    int value = 42;
    auto pinned = pin!(value);

    // 新規変数として作成
    auto pinned_new = pin!(int x = 100);

    return 0;
}
```

### 4.2 pin_struct!マクロ

```cm
// 自己参照構造体用マクロ
macro pin_struct {
    (
        struct $name:ident {
            $($field:ident : $type:type;)...
        }
    ) => {
        struct $name {
            $($field : $type;)...

            // Pinされた状態でのみ初期化可能
            void init_pinned(Pin<$name>& self) {
                // 自己参照の設定など
            }
        };

        // Pin用のヘルパー関数
        Pin<$name> make_pinned_##$name() {
            $name instance;
            Pin<$name> pinned(instance);
            pinned.init_pinned(pinned);
            return pinned;
        }
    };
}
```

## 5. マクロ展開メカニズム

### 5.1 展開フロー

```
ソースコード（Cm構文）
    ↓
[Lexer: トークン化]
    ↓
[マクロ検出]
    ↓
[パターンマッチング]
    ↓
[マクロ展開] ← 再帰的展開
    ↓
[衛生性適用]
    ↓
[構文解析]
    ↓
AST
```

### 5.2 衛生性の実装（簡略化）

```cm
// マクロ内で生成される変数は自動的にユニーク化
macro swap {
    ($a:ident, $b:ident) => {{
        auto __temp = $a;  // __tempは自動的にユニーク名になる
        $a = $b;
        $b = __temp;
    }};
}

// 使用例（名前衝突なし）
int main() {
    int x = 1, y = 2;
    int __temp = 100;  // ユーザーの__temp

    swap!(x, y);  // マクロの__tempと衝突しない

    printf("%d\n", __temp);  // 100が出力される

    return 0;
}
```

## 6. エラー処理

### 6.1 エラーメッセージ例

```
error: no matching pattern for macro 'vec'
  --> main.cm:10:15
   |
10 | auto v = vec!{1, 2, 3};
   |               ^ expected '(' but found '{'
   |
   = help: vec! expects one of:
           - vec!() for empty vector
           - vec!(elem, ...) for list
```

### 6.2 デバッグ支援

```cm
// マクロ展開の確認
#pragma macro_trace(vec)  // vecマクロの展開をトレース

int main() {
    auto v = vec!(1, 2, 3);
    // [MACRO] Expanding vec!(1, 2, 3)
    // [MACRO]   Matched pattern: ($first:expr, $rest:expr...)
    // [MACRO]   Result: Vector<int> v; v.push(1); v.push(2); v.push(3); v

    return 0;
}
```

## 7. 標準マクロライブラリ

### 7.1 基本マクロ

```cm
// std/macros.cm

// 最小/最大値
macro min {
    ($a:expr, $b:expr) => {
        (($a) < ($b) ? ($a) : ($b))
    };
}

macro max {
    ($a:expr, $b:expr) => {
        (($a) > ($b) ? ($a) : ($b))
    };
}

// デバッグ出力
macro dbg {
    ($val:expr) => {{
        fprintf(stderr, "[%s:%d] %s = ",
                __FILE__, __LINE__, #$val);
        auto __result = $val;
        // 型に応じた出力（将来実装）
        print_debug(__result);
        __result
    }};
}

// TODO マクロ
macro todo {
    () => {
        panic("not yet implemented");
    };
    ($msg:literal) => {
        panic("not yet implemented: " $msg);
    };
}
```

### 7.2 コレクションマクロ

```cm
// ハッシュマップ
macro hashmap {
    () => {
        HashMap<int, int>()
    };

    ($($key:expr => $val:expr),+) => {{
        auto map = HashMap<typeof($key), typeof($val)>();
        $(map.insert($key, $val);)+
        map
    }};
}

// 使用例
int main() {
    auto map = hashmap!(
        "one" => 1,
        "two" => 2,
        "three" => 3
    );

    return 0;
}
```

## 8. C++マクロとの違い

| 機能 | Cm マクロ | C++ マクロ |
|------|----------|-----------|
| パターンマッチ | ✅ | ❌ |
| 型安全 | ✅ | ❌ |
| 衛生性 | ✅ | ❌ |
| 再帰展開 | ✅ 制限付き | ⚠️ 危険 |
| デバッグ | ✅ 容易 | ❌ 困難 |
| 可変長引数 | ✅ ... 構文 | ⚠️ __VA_ARGS__ |

## 9. 実装優先度

1. **Phase 1（必須）**
   - 基本パターンマッチング
   - 単純な展開
   - vec!, assert!マクロ

2. **Phase 2（重要）**
   - 衛生性の実装
   - pin!マクロ
   - エラー処理改善

3. **Phase 3（将来）**
   - 属性マクロ
   - 手続きマクロ
   - IDEサポート

## 10. まとめ

CmのマクロシステムはCmのマクロシステムは：

1. **Cm構文準拠**: C++風の構文スタイルを維持
2. **シンプル**: Rustの複雑さを避けた設計
3. **安全**: 衛生性と型安全性を保証
4. **実用的**: Pin実装などで即座に活用可能
5. **段階的**: 基本機能から順次実装

C++の#defineマクロの問題を解決しつつ、Rustの複雑性も回避する、バランスの取れた設計です。

---

**作成者:** Claude Code
**ステータス:** 設計改訂
**実装開始:** 基本マクロから段階的に