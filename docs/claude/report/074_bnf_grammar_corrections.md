# BNF文法修正レポート

作成日: 2026-01-11
ステータス: 修正完了

## 修正内容

### 1. 削除したキーワード

#### 未実装のキーワード（削除）
以下のキーワードは実装されていないため、BNF定義から削除しました：

| キーワード | 理由 |
|-----------|------|
| `do` | do-while構文は未実装 |
| `public` | アクセス修飾子は`private`のみ実装 |
| `protected` | アクセス修飾子は`private`のみ実装 |
| `volatile` | メモリ修飾子は未実装 |
| `mutable` | メモリ修飾子は未実装 |
| `new` | 動的メモリ確保は標準ライブラリで実装 |
| `delete` | 動的メモリ解放は標準ライブラリで実装 |
| `let` | 型推論は`auto`のみ実装 |
| `var` | 型推論は`auto`のみ実装 |

### 2. 修正した文法規則

#### declaration_statement
```bnf
# 修正前
declaration_statement ::= type identifier ('=' expression)? ';'
                        | 'let' identifier '=' expression ';'  # 型推論

# 修正後
declaration_statement ::= type identifier ('=' expression)? ';'
```

#### unary_expr
```bnf
# 修正前
unary_expr ::= postfix_expr
             | '++' unary_expr
             | '--' unary_expr
             | unary_operator cast_expr
             | 'sizeof' '(' type ')'
             | 'sizeof' unary_expr
             | 'typeof' '(' expression ')'
             | 'new' type '(' argument_list? ')'
             | 'delete' expression

# 修正後
unary_expr ::= postfix_expr
             | '++' unary_expr
             | '--' unary_expr
             | unary_operator cast_expr
             | 'sizeof' '(' type ')'
             | 'sizeof' unary_expr
             | 'typeof' '(' expression ')'
```

### 3. 追加した文法規則

#### operator_decl（演算子オーバーロード）
```bnf
operator_decl ::= 'operator' operator_symbol '(' param_list ')' type block
                | 'operator' operator_symbol '(' param_list ')' type ';'

operator_symbol ::= '+' | '-' | '*' | '/' | '%'
                  | '==' | '!=' | '<' | '>' | '<=' | '>='
                  | '&' | '|' | '^' | '~' | '!'
                  | '<<' | '>>'
                  | '[]' | '()'
```

### 4. 保持したキーワード

以下のキーワードは実装済みのため保持しました：

- **制御構造**: `if`, `else`, `while`, `for`, `switch`, `case`, `default`
- **ジャンプ**: `break`, `continue`, `return`, `defer`
- **型定義**: `struct`, `enum`, `trait`, `impl`, `typedef`, `macro`
- **モジュール**: `import`, `export`, `module`
- **演算子**: `operator`, `sizeof`, `typeof`  # operatorを追加
- **修飾子**: `const`, `static`, `private`
- **リテラル**: `true`, `false`, `null`, `nullptr`
- **型**: `void`, `int`, `uint`, `tiny`, `utiny`, `short`, `ushort`, `long`, `ulong`, `float`, `double`, `bool`, `char`, `string`, `size_t`
- **その他**: `auto`, `with`, `where`, `as`

## 更新した統計

| 項目 | 修正前 | 修正後（最終） | 差分 |
|------|--------|--------------|------|
| プロダクション数 | 99 | 100 | +1 |
| 終端記号数 | 140 | 133 | -7 |
| 非終端記号数 | 99 | 100 | +1 |

## 検証ツールの更新

`scripts/validate_bnf.py`の組み込みキーワードリストを更新し、実際に実装されているキーワードのみを含むようにしました（ツールは`scripts/`ディレクトリに移動）。

## 再生成したファイル

以下のファイルを修正後のBNF定義で再生成しました：

1. `docs/claude/report/cm_grammar_output.txt` - テキスト形式の文法
2. `docs/claude/report/cm_grammar.html` - HTML形式の文法ドキュメント
3. `docs/claude/report/cm_grammar.dot` - Graphviz形式の文法図

## 影響

この修正により：

1. **正確性向上**: BNF定義が実際の言語実装と一致
2. **混乱防止**: 未実装機能への誤った期待を防ぐ
3. **パーサー生成**: 正しいBNFからのコード生成が可能に

## 今後の注意点

新機能を追加する際は：

1. 実装とBNF定義を同期させる
2. キーワードの追加は慎重に検討する
3. 予約語との衝突を避ける

## まとめ

BNF文法定義から未実装および存在しないキーワードを削除し、実際のCm言語仕様と一致させました。これにより、文法定義の正確性が向上し、今後のパーサー生成やドキュメント作成の基盤が整いました。

---

**作成者:** Claude Code
**承認:** 修正完了
**次ステップ:** BNFからのパーサー生成実装