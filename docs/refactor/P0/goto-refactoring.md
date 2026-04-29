# goto文のリファクタリング

**優先度**: 高  
**影響範囲**: コード保守性  
**対象ファイル**: 複数

---

## 問題

構造化プログラミングの原則に反する `goto` 文が使用されている。

### 1. parser_expr.cpp (parse_concat)

```cpp
// Line ~1094, 1096
goto parse_concat;
```

SV連接式のパースでgotoを使用してラベルにジャンプ。

### 2. lexer.cpp (normal_number)

```cpp
// Line ~340, 343
goto normal_number;
```

SV幅付きリテラル解析のフォールバック処理。

### 3. import.cpp (finalize)

```cpp
// Line ~多数
goto finalize;
```

インポート処理の終了処理。

---

## 修正案

### parser_expr.cpp

ヘルパー関数 `parse_sv_concat()` に抽出:

```cpp
ast::ExprPtr Parser::parse_sv_concat(uint32_t start_pos) {
    std::vector<ast::ExprPtr> elements;
    elements.push_back(parse_expr());
    while (consume_if(TokenKind::Comma)) {
        elements.push_back(parse_expr());
    }
    expect(TokenKind::RBrace);
    auto callee = ast::make_ident("__builtin_concat", Span{start_pos, start_pos});
    return ast::make_call(std::move(callee), std::move(elements),
                          Span{start_pos, previous().end});
}
```

### lexer.cpp

早期リターンパターンに変更:

```cpp
// SVリテラル解析を試みる
if (auto sv_token = try_parse_sv_literal(start)) {
    return *sv_token;
}
// フォールバック: 通常の数値
return parse_normal_number(start);
```

### import.cpp

RAII パターンまたは do-while(false) イディオム:

```cpp
auto cleanup = [&]() { /* 終了処理 */ };

do {
    if (error1) break;
    if (error2) break;
    // 成功処理
} while (false);

cleanup();
```

---

## 影響

- コードの可読性向上
- デバッグの容易化
- 制御フローの明確化

