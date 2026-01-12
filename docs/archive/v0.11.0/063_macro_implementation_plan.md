# Cm言語 マクロシステム実装計画

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 実装計画
関連文書: 060_cm_macro_system_design.md, 061_pin_library_design.md

## エグゼクティブサマリー

設計文書(060-062)に基づき、マクロシステムとPinライブラリの実装を段階的に進めます。Phase 1では基本的なmacro_rules!を実装し、Phase 2でPinライブラリを構築します。

## Phase 1: 基本マクロシステム (2週間)

### Week 1: パーサーとマッチャー

#### 1.1 マクロパーサー実装

**新規ファイル: `src/parser/macro_parser.hpp`**
```cpp
namespace cm::parser {

class MacroParser {
public:
    // macro_rules! の解析
    std::unique_ptr<MacroDefinition> parse_macro_rules(TokenStream& tokens);

private:
    // パターン解析
    MacroPattern parse_pattern(TokenStream& tokens);

    // トランスクライバー解析
    MacroTranscriber parse_transcriber(TokenStream& tokens);

    // フラグメント指定子の解析
    FragmentSpecifier parse_fragment_spec(const Token& token);

    // 繰り返しパターンの解析
    RepetitionPattern parse_repetition(TokenStream& tokens);
};

}  // namespace cm::parser
```

#### 1.2 トークンツリー構造

**新規ファイル: `src/macro/token_tree.hpp`**
```cpp
namespace cm::macro {

// トークンツリー（マクロの基本単位）
struct TokenTree {
    enum class Kind {
        TOKEN,           // 単一トークン
        DELIMITED,       // 括弧で囲まれたトークン群
        METAVAR,         // メタ変数 ($x:expr)
        REPETITION,      // 繰り返し $(...)*
    };

    Kind kind;
    std::variant<
        Token,
        DelimitedTokens,
        MetaVariable,
        RepetitionNode
    > data;
};

// メタ変数
struct MetaVariable {
    std::string name;
    FragmentSpecifier spec;
};

}  // namespace cm::macro
```

#### 1.3 マッチングエンジン

**新規ファイル: `src/macro/matcher.cpp`**
```cpp
namespace cm::macro {

class MacroMatcher {
public:
    MatchResult match(const TokenStream& input,
                     const MacroPattern& pattern) {
        MatchState state;

        if (match_recursive(input, pattern, 0, 0, state)) {
            return {true, state.bindings, ""};
        }

        return {false, {}, generate_error(state)};
    }

private:
    bool match_fragment(const TokenStream& input, size_t& pos,
                       FragmentSpecifier spec,
                       std::vector<Token>& matched);

    bool match_repetition(const TokenStream& input, size_t& pos,
                         const RepetitionPattern& pattern,
                         MatchState& state);
};

}  // namespace cm::macro
```

### Week 2: 展開と衛生性

#### 2.1 衛生的コンテキスト

**新規ファイル: `src/macro/hygiene.cpp`**
```cpp
namespace cm::macro {

class HygieneContext {
    uint32_t next_context_id = 1;
    uint32_t gensym_counter = 0;

public:
    // 新しい構文コンテキストを作成
    SyntaxContext create_context(const ExpansionInfo& info) {
        return {next_context_id++, info};
    }

    // ユニークなシンボル生成 (gensym)
    std::string gensym(const std::string& base) {
        return base + "__gensym_" + std::to_string(gensym_counter++);
    }

    // 識別子の衛生的な処理
    HygienicIdent make_hygienic(const std::string& name,
                                const SyntaxContext& ctx) {
        return {name, ctx};
    }
};

}  // namespace cm::macro
```

#### 2.2 マクロ展開器

**新規ファイル: `src/macro/expander.cpp`**
```cpp
namespace cm::macro {

class MacroExpander {
    HygieneContext hygiene;
    std::map<std::string, MacroDefinition> macros;

public:
    // マクロ呼び出しの展開
    TokenStream expand(const MacroCall& call) {
        auto def = find_macro(call.name);
        if (!def) {
            throw MacroError("Undefined macro: " + call.name);
        }

        // パターンマッチング
        for (const auto& rule : def->rules) {
            auto result = matcher.match(call.args, rule.pattern);
            if (result.success) {
                return transcribe(rule.transcriber, result.bindings);
            }
        }

        throw MacroError("No matching pattern for macro: " + call.name);
    }

private:
    TokenStream transcribe(const MacroTranscriber& transcriber,
                          const MatchBindings& bindings);
};

}  // namespace cm::macro
```

#### 2.3 Lexerとの統合

**修正ファイル: `src/lexer/lexer.cpp`**
```cpp
// マクロ定義の認識を追加
Token Lexer::next_token() {
    // 既存のトークン処理...

    // macro_rules! の認識
    if (current_text == "macro_rules") {
        if (peek() == '!') {
            advance(); // '!' をスキップ
            return Token{TokenType::MacroRules, "macro_rules!", location};
        }
    }

    // マクロ呼び出しの認識 (identifier!)
    if (is_identifier(current)) {
        auto ident = read_identifier();
        if (peek() == '!') {
            advance(); // '!' をスキップ
            return Token{TokenType::MacroCall, ident + "!", location};
        }
    }
}
```

## Phase 2: Pin実装用マクロ (1週間)

### 3.1 pin!マクロ実装

**新規ファイル: `src/stdlib/macros/pin.cm`**
```cm
// pin!マクロの定義
export macro_rules! pin {
    // スタック上の値をピン留め
    ($val:expr) => {{
        // ユニークな変数名を生成（衛生性保証）
        let mut __pin_value = $val;
        // SAFETY: スタック上の値は移動しない
        unsafe { Pin::new_unchecked(&mut __pin_value) }
    }};
}

// テスト用マクロ
export macro_rules! assert_pinned {
    ($ty:ty) => {
        const _: () = {
            fn _assert_not_unpin<T: ?Sized + Unpin>() {}
            // コンパイル時にUnpinでないことを確認
        };
    };
}
```

### 3.2 pin_project!マクロ

**新規ファイル: `src/stdlib/macros/pin_project.cm`**
```cm
export macro_rules! pin_project {
    (
        struct $name:ident {
            $(
                $(#[pin])?
                $field:ident : $field_ty:ty
            ),* $(,)?
        }
    ) => {
        struct $name {
            $($field: $field_ty,)*
        }

        // 投影構造体の自動生成
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
    };
}
```

## Phase 3: 統合とテスト (1週間)

### 4.1 パーサー統合

**修正ファイル: `src/parser/parser.cpp`**
```cpp
std::unique_ptr<AST> Parser::parse_top_level() {
    // 既存の処理...

    // macro_rules! の処理を追加
    if (current_token.type == TokenType::MacroRules) {
        return parse_macro_definition();
    }

    // マクロ呼び出しの処理を追加
    if (current_token.type == TokenType::MacroCall) {
        return expand_and_parse_macro();
    }
}
```

### 4.2 テストケース

**新規ファイル: `tests/macros/test_basic.cm`**
```cm
// 基本的なマクロテスト
macro_rules! vec {
    () => { Vector::new() };
    ($($x:expr),*) => {{
        let mut v = Vector::new();
        $(v.push($x);)*
        v
    }};
}

void test_vec_macro() {
    let v1 = vec![];           // 空のベクター
    let v2 = vec![1, 2, 3];    // 要素付きベクター

    assert(v1.len() == 0);
    assert(v2.len() == 3);
}
```

**新規ファイル: `tests/macros/test_pin.cm`**
```cm
struct SelfRef {
    value: int,
    ptr: *const int,
}

void test_pin_macro() {
    let mut data = SelfRef {
        value: 42,
        ptr: null
    };

    // pin!マクロでピン留め
    let pinned = pin!(data);

    // ピン留め後は移動不可
    // let moved = data;  // コンパイルエラー
}
```

### 4.3 エラー処理

**新規ファイル: `src/macro/errors.hpp`**
```cpp
namespace cm::macro {

enum class MacroErrorKind {
    UNDEFINED_MACRO,
    NO_MATCHING_PATTERN,
    INVALID_FRAGMENT,
    RECURSION_LIMIT,
    EXPANSION_OVERFLOW,
};

class MacroError : public std::runtime_error {
public:
    MacroError(MacroErrorKind kind, const std::string& message,
              const SourceLocation& loc)
        : std::runtime_error(format_error(kind, message, loc))
        , kind_(kind)
        , location_(loc) {}

private:
    static std::string format_error(MacroErrorKind kind,
                                   const std::string& msg,
                                   const SourceLocation& loc);
};

}  // namespace cm::macro
```

## 実装優先順位

1. **必須（Week 1）**
   - [ ] TokenTreeの定義
   - [ ] MacroParserの基本実装
   - [ ] MacroMatcherのパターンマッチング

2. **重要（Week 2）**
   - [ ] HygieneContextの実装
   - [ ] MacroExpanderの展開処理
   - [ ] Lexer/Parserとの統合

3. **Pin対応（Week 3）**
   - [ ] pin!マクロの実装
   - [ ] pin_project!マクロの実装
   - [ ] Pinライブラリとの統合テスト

4. **品質保証（Week 4）**
   - [ ] 包括的テストスイート
   - [ ] エラーメッセージの改善
   - [ ] デバッグ支援機能

## テスト戦略

### ユニットテスト
```bash
# C++側のテスト
./build/bin/test_macros

# Cm側のテスト
./build/bin/cm tests/macros/*.cm
```

### 統合テスト
```bash
# Pin + マクロの統合テスト
./build/bin/cm tests/integration/pin_macro_test.cm
```

### パフォーマンステスト
```bash
# マクロ展開のベンチマーク
./build/bin/cm --bench tests/bench/macro_expansion.cm
```

## リスクと対策

| リスク | 影響度 | 対策 |
|-------|-------|------|
| 衛生性バグ | 高 | 徹底的なユニットテスト |
| 展開の無限ループ | 中 | 再帰深度制限（128） |
| パフォーマンス劣化 | 中 | 展開結果のキャッシュ |
| デバッグ困難 | 低 | 展開トレース機能 |

## まとめ

この実装計画により、4週間で：
1. 基本的なmacro_rules!システム
2. Pin実装に必要なマクロ
3. 完全なテストカバレッジ

が実現できます。Phase 1の基本実装から始めましょう。

---

**作成者:** Claude Code
**ステータス:** 実装計画
**次ステップ:** TokenTreeとMacroParserの実装開始