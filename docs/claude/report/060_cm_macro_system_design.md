# Cm言語マクロシステム設計書

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計提案

## エグゼクティブサマリー

Cm言語に安全で衛生的なマクロシステムを導入します。Rustの`macro_rules!`を参考にしながら、Pin実装のような高度なライブラリ開発を可能にする設計を提案します。

## 1. 設計理念

### 1.1 基本方針

1. **衛生的（Hygienic）**: 変数名の衝突を防ぐ
2. **型安全**: マクロ展開後も型チェックを保証
3. **段階的展開**: デバッグ可能な展開プロセス
4. **エラーの明確化**: マクロエラーの詳細な報告

### 1.2 マクロの種類

```cm
// 1. 宣言的マクロ（macro_rules スタイル）
macro_rules! vec {
    () => { Vector::new() };
    ($($x:expr),*) => {{
        let mut v = Vector::new();
        $(v.push($x);)*
        v
    }};
}

// 2. 手続き的マクロ（将来実装）
#[proc_macro]
fn derive_debug(input: TokenStream) -> TokenStream {
    // コード生成
}

// 3. 属性マクロ
#[derive(Debug, Clone)]
struct Point { x: int, y: int }
```

## 2. 宣言的マクロ設計

### 2.1 構文定義

```ebnf
macro_declaration ::= 'macro_rules!' identifier '{' macro_rules '}'

macro_rules ::= macro_rule (';' macro_rule)* ';'?

macro_rule ::= macro_matcher '=>' macro_transcriber

macro_matcher ::= '(' token_tree* ')'
                | '[' token_tree* ']'
                | '{' token_tree* '}'

token_tree ::= token
             | macro_matcher
             | '$' identifier ':' fragment_specifier
             | '$' '(' token_tree* ')' separator? repetition_op

fragment_specifier ::= 'expr' | 'stmt' | 'pat' | 'ty' | 'ident'
                     | 'path' | 'literal' | 'block' | 'item'
                     | 'meta' | 'tt'

repetition_op ::= '*' | '+' | '?'

separator ::= token
```

### 2.2 フラグメント指定子

| 指定子 | マッチ対象 | 例 |
|--------|-----------|-----|
| `expr` | 式 | `x + 1`, `func()` |
| `stmt` | 文 | `let x = 5;` |
| `ty` | 型 | `int`, `Vector<T>` |
| `ident` | 識別子 | `foo`, `x` |
| `path` | パス | `std::vec::Vector` |
| `literal` | リテラル | `42`, `"hello"` |
| `block` | ブロック | `{ ... }` |
| `pat` | パターン | `Some(x)`, `_` |
| `item` | アイテム | 関数、構造体定義 |
| `tt` | トークンツリー | 任意のトークン |

## 3. Pin実装用マクロ

### 3.1 pin!マクロ

```cm
// Pin作成マクロ
macro_rules! pin {
    ($val:expr) => {{
        // スタック上にピン留めされた値を作成
        let mut pinned = $val;
        // SAFETY: ローカル変数は移動しない
        unsafe { Pin::new_unchecked(&mut pinned) }
    }};
}

// 使用例
let future = pin!(async_operation());
```

### 3.2 pin_project!マクロ

```cm
// 自己参照構造体のための投影マクロ
macro_rules! pin_project {
    (
        $(#[$meta:meta])*
        struct $name:ident {
            $(
                $(#[pin])?
                $field_vis:vis $field:ident : $field_ty:ty
            ),* $(,)?
        }
    ) => {
        $(#[$meta])*
        struct $name {
            $(
                $field_vis $field: $field_ty,
            )*
        }

        // 投影メソッドの自動生成
        impl $name {
            fn project(self: Pin<&mut Self>) -> __Projection {
                unsafe {
                    let this = self.get_unchecked_mut();
                    __Projection {
                        $(
                            $field: project_field!(
                                this.$field,
                                $(#[pin])?
                            ),
                        )*
                    }
                }
            }
        }

        // 内部投影構造体
        struct __Projection<'a> {
            $(
                $field: project_type!($field_ty, $(#[pin])?),
            )*
        }
    };
}
```

### 3.3 assert_pinned!マクロ

```cm
// コンパイル時ピン留め検証
macro_rules! assert_pinned {
    ($ty:ty) => {
        const _: () = {
            // PhantomPinnedを含む型かチェック
            fn __assert_not_unpin<T: ?Sized + Unpin>() {}
            fn __assert_pinned<T: ?Sized>(_: &T) {
                // コンパイルエラーを生成
                __assert_not_unpin::<T>();
            }
        };
    };
}
```

## 4. マクロ展開メカニズム

### 4.1 展開フロー

```
ソースコード
    ↓
[Lexer: トークン化]
    ↓
[マクロ収集フェーズ]
    ↓
[マクロ展開フェーズ] ← 再帰的展開
    ↓
[構文解析]
    ↓
AST
```

### 4.2 衛生性の実装

```cpp
// src/macro/hygiene.hpp
namespace cm::macro {

class HygieneContext {
    struct SyntaxContext {
        uint32_t id;
        ExpansionInfo expansion;
        std::set<Symbol> introduced_names;
    };

    // 各識別子に構文コンテキストを付与
    struct HygienicIdent {
        std::string name;
        SyntaxContext context;

        bool operator==(const HygienicIdent& other) const {
            // 同じコンテキストの同じ名前のみ等しい
            return name == other.name &&
                   context.id == other.context.id;
        }
    };

public:
    HygienicIdent create_ident(const std::string& name,
                               const ExpansionInfo& expansion) {
        SyntaxContext ctx{
            next_context_id++,
            expansion,
            {}
        };
        return {name, ctx};
    }

    // gensym: ユニークなシンボル生成
    std::string gensym(const std::string& base) {
        return base + "__" + std::to_string(gensym_counter++);
    }

private:
    uint32_t next_context_id = 1;
    uint32_t gensym_counter = 0;
};

}  // namespace cm::macro
```

### 4.3 マクロマッチングエンジン

```cpp
// src/macro/matcher.hpp
class MacroMatcher {
public:
    struct MatchResult {
        bool success;
        std::map<std::string, MatchedFragment> bindings;
        std::string error;
    };

    MatchResult match(const TokenStream& input,
                     const MacroPattern& pattern) {
        MatchState state;

        if (match_recursive(input, pattern, 0, 0, state)) {
            return {true, state.bindings, ""};
        }

        return {false, {}, generate_error(state)};
    }

private:
    bool match_recursive(const TokenStream& input,
                        const MacroPattern& pattern,
                        size_t input_pos,
                        size_t pattern_pos,
                        MatchState& state) {
        // パターンマッチングの実装
        if (pattern_pos >= pattern.size()) {
            return input_pos == input.size();
        }

        const auto& pat_elem = pattern[pattern_pos];

        // メタ変数のマッチ
        if (pat_elem.is_metavar()) {
            return match_metavar(input, input_pos,
                                pat_elem, state);
        }

        // 繰り返しのマッチ
        if (pat_elem.is_repetition()) {
            return match_repetition(input, input_pos,
                                   pat_elem, state);
        }

        // リテラルトークンのマッチ
        if (input_pos < input.size() &&
            input[input_pos] == pat_elem.token) {
            return match_recursive(input, pattern,
                                 input_pos + 1,
                                 pattern_pos + 1,
                                 state);
        }

        return false;
    }
};
```

## 5. エラー処理

### 5.1 マクロエラーの種類

```cm
enum class MacroError {
    // パターンマッチエラー
    E0601_NO_MATCHING_PATTERN,
    E0602_AMBIGUOUS_MATCH,
    E0603_INVALID_FRAGMENT,

    // 展開エラー
    E0611_RECURSION_LIMIT,
    E0612_EXPANSION_OVERFLOW,
    E0613_UNBOUND_METAVAR,

    // 衛生性エラー
    E0621_NAME_COLLISION,
    E0622_CONTEXT_MISMATCH,
};
```

### 5.2 エラーメッセージ例

```
error[E0601]: no rules expected the token `]`
  --> src/main.cm:10:15
   |
10 | let v = vec![1, 2, 3];
   |               ^ no rules expected this token
   |
   = note: expected one of: `,`, `)`
   = help: the macro `vec` expects either:
           - vec!() for empty vector
           - vec![elem; count] for repetition
           - vec![elem1, elem2, ...] for list
```

## 6. デバッグ支援

### 6.1 展開トレース

```cm
// マクロ展開のトレース出力
#[macro_trace]
macro_rules! complex_macro {
    ($x:expr) => {
        println!("Value: {}", $x * 2)
    };
}

// 出力:
// [MACRO] Expanding complex_macro!(5)
// [MACRO]   Matched pattern: ($x:expr)
// [MACRO]   Binding: $x = 5
// [MACRO]   Transcribing: println!("Value: {}", $x * 2)
// [MACRO]   Result: println!("Value: {}", 5 * 2)
```

### 6.2 展開の可視化

```bash
# マクロ展開の確認
cm expand src/main.cm --macro vec

# 出力:
# Original:
#   let v = vec![1, 2, 3];
#
# Expanded:
#   let v = {
#       let mut __temp = Vector::new();
#       __temp.push(1);
#       __temp.push(2);
#       __temp.push(3);
#       __temp
#   };
```

## 7. 標準マクロライブラリ

### 7.1 基本マクロ

```cm
// std/macros/core.cm
export macro_rules! assert {
    ($cond:expr) => {
        if (!$cond) {
            panic("Assertion failed: {}", stringify!($cond));
        }
    };
    ($cond:expr, $msg:expr) => {
        if (!$cond) {
            panic("Assertion failed: {}\n  Message: {}",
                  stringify!($cond), $msg);
        }
    };
}

export macro_rules! debug_assert {
    ($($arg:tt)*) => {
        #[cfg(debug)]
        assert!($($arg)*);
    };
}

export macro_rules! unreachable {
    () => {
        panic("Entered unreachable code");
    };
    ($msg:expr) => {
        panic("Entered unreachable code: {}", $msg);
    };
}
```

### 7.2 コレクションマクロ

```cm
// std/macros/collections.cm
export macro_rules! vec {
    () => { Vector::new() };
    ($elem:expr; $n:expr) => {{
        let mut v = Vector::with_capacity($n);
        for _ in 0..$n {
            v.push($elem);
        }
        v
    }};
    ($($x:expr),+ $(,)?) => {{
        let mut v = Vector::new();
        $(v.push($x);)+
        v
    }};
}

export macro_rules! hashmap {
    () => { HashMap::new() };
    ($($key:expr => $value:expr),* $(,)?) => {{
        let mut map = HashMap::new();
        $(map.insert($key, $value);)*
        map
    }};
}
```

## 8. パフォーマンス考慮

### 8.1 展開の最適化

```cpp
class MacroExpander {
    // メモ化による重複展開の回避
    std::map<MacroCallHash, ExpandedResult> cache;

    TokenStream expand_cached(const MacroCall& call) {
        auto hash = compute_hash(call);

        if (auto it = cache.find(hash); it != cache.end()) {
            return it->second.clone_with_new_context();
        }

        auto result = expand_impl(call);
        cache[hash] = result;
        return result;
    }
};
```

### 8.2 制限事項

| 制限 | デフォルト値 | 設定可能範囲 |
|------|------------|-------------|
| 再帰深度 | 128 | 32-1024 |
| 展開サイズ | 65536 tokens | 1KB-1MB |
| ネスト深度 | 64 | 16-256 |

## 9. 他言語との比較

| 機能 | Cm | Rust | C++ | C |
|------|-----|------|-----|---|
| 衛生的マクロ | ✅ | ✅ | ❌ | ❌ |
| パターンマッチ | ✅ | ✅ | ❌ | ❌ |
| 型安全 | ✅ | ✅ | ❌ | ❌ |
| デバッグ支援 | ✅ | ⚠️ | ❌ | ❌ |
| 手続きマクロ | 🔄 | ✅ | ❌ | ❌ |

## 10. まとめ

このマクロシステムにより：

1. **安全なメタプログラミング**: 衛生性と型安全性を保証
2. **Pin実装のサポート**: 複雑なライブラリ開発が可能
3. **優れたデバッグ性**: 展開の可視化とトレース
4. **段階的な学習**: シンプルから高度な使用まで

---

**作成者:** Claude Code
**ステータス:** 設計提案
**次文書:** 061_pin_library_design.md