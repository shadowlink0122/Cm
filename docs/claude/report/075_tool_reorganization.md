# 開発ツールの再編成レポート

作成日: 2026-01-11
ステータス: 完了

## 実施内容

### 1. ツールの移動

BNF検証ツールを適切なディレクトリに移動しました：

**移動前:** `src/frontend/tools/validate_bnf.py`
**移動後:** `scripts/validate_bnf.py`

### 2. 理由

- **明確な役割分離**: ソースコードと開発ツールの分離
- **プロジェクト構造の整理**: `src/`はソースコード、`scripts/`は開発ツール
- **一貫性**: 他の開発スクリプトも`scripts/`に配置されている

### 3. 更新したドキュメント

以下のドキュメントの参照パスを更新しました：

| ドキュメント | 更新内容 |
|------------|---------|
| `072_bnf_parser_integration.md` | フォルダ構成図を更新 |
| `073_bnf_integration_summary.md` | ツールの場所と使用方法コマンドを更新 |
| `074_bnf_grammar_corrections.md` | ツールパスの記載を更新 |

### 4. 新しい使用方法

```bash
# BNF文法の検証
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf --validate

# 統計情報の表示
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf --stats

# テキスト形式で出力
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f text -o output.txt

# HTML形式で出力
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f html -o output.html

# DOT形式で出力（Graphviz用）
python3 scripts/validate_bnf.py docs/design/cm_grammar.bnf -f dot -o output.dot
```

### 5. プロジェクト構造

```
Cm/
├── src/
│   └── frontend/         # フロントエンドソースコード
│       ├── lexer/
│       ├── parser/
│       ├── ast/
│       └── semantic/
│
├── scripts/              # 開発ツール・スクリプト
│   ├── validate_bnf.py   # BNF文法検証ツール
│   ├── format_tests.py
│   ├── run_tests.sh
│   └── ...
│
└── docs/
    └── design/
        └── cm_grammar.bnf  # BNF文法定義
```

## 利点

1. **明確な構造**: ソースコードと開発ツールが明確に分離
2. **メンテナンス性**: ツールの場所が統一されている
3. **発見しやすさ**: `scripts/`を見れば利用可能なツールがわかる
4. **ビルドシステムとの分離**: ソースコードのビルドに影響しない

## 今後の開発ツール配置方針

新しい開発ツールは以下の基準で配置します：

| 種類 | 配置場所 | 例 |
|------|---------|-----|
| ビルド済みC++ツール | `build/bin/` | `cm`, `cm-test` |
| 開発用スクリプト | `scripts/` | `validate_bnf.py`, `format_tests.py` |
| ソースコード | `src/` | コンパイラ本体 |
| テスト | `tests/` | テストケース |

## まとめ

BNF検証ツールを`scripts/`ディレクトリに移動し、プロジェクト構造をより整理しました。これにより、開発ツールとソースコードの役割が明確に分離され、プロジェクトの保守性が向上しました。

---

**作成者:** Claude Code
**承認:** 完了
**影響:** なし（ツールパスの変更のみ）