# SV initial構文の実装

**優先度**: 低（将来対応）  
**影響範囲**: SV機能  
**対象ファイル**: パーサー、コード生成

---

## 現状

`KwInitial` トークンはレキサーで定義されているが、パーサーで受理されない。

```cpp
// src/frontend/lexer/token.hpp:113
KwInitial,  // initial シミュレーション初期化
```

ドキュメントでは「未実装（将来対応予定）」と明記済み。

---

## 提案構文

```cm
//! platform: sv

initial {
    clk = false;
    rst = true;
    #10 rst = false;  // 遅延構文も検討
}
```

### SV出力

```systemverilog
initial begin
    clk = 1'b0;
    rst = 1'b1;
    #10 rst = 1'b0;
end
```

---

## 実装計画

### Phase 1: パーサー

```cpp
// parser_module.cpp または parser_stmt.cpp

// トップレベル initial ブロック
if (consume_if(TokenKind::KwInitial)) {
    return parse_initial_block();
}

ast::StmtPtr Parser::parse_initial_block() {
    expect(TokenKind::LBrace);
    auto stmts = parse_block();
    // InitialBlockノードを作成
    return ast::make_initial(std::move(stmts), span);
}
```

### Phase 2: AST/HIR

```cpp
// ast/stmt.hpp
struct InitialBlock {
    std::vector<StmtPtr> statements;
};
```

### Phase 3: SVコード生成

```cpp
// sv/codegen.cpp
void SVCodeGen::emitInitialBlock(const mir::InitialBlock& block) {
    ss << "initial begin\n";
    increaseIndent();
    for (const auto& stmt : block.statements) {
        emitStatement(stmt);
    }
    decreaseIndent();
    ss << "end\n";
}
```

---

## 課題

### 1. 遅延構文 `#N`

Cmには遅延演算子がない。検討オプション:
- `delay(10)` ビルトイン関数
- `#[sv::delay(10)]` 属性
- `#10` リテラル構文の追加

### 2. シミュレーション専用の明示

`initial` は合成不可。合成ターゲットでは警告/エラーを出すべき。

---

## 影響

- テストベンチ記述の強化
- シミュレーションワークフローの改善

