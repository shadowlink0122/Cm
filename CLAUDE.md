# CLAUDE.md - Cm言語プロジェクトAIガイド

> **Note:** 詳細なルールは `.agent/rules/` を参照

## プロジェクト概要

**Cm (Sea Minor)** - OS/ベアメタル開発からWebアプリケーションまで、単一言語で実現するプログラミング言語

### 設計思想
- C++にモダン言語の良いとこ取り
- シンプルで直感的な構文
- Rust風構文は使用しない（`fn`, `->`等禁止）

## 言語仕様

### 構文スタイル：C++風
```cm
// ✓ 正しい
int add(int a, int b) { return a + b; }
<T: Ord> T max(T a, T b) { return a > b ? a : b; }

// ✗ 間違い（Rust風）
fn add(a: int, b: int) -> int { }
```

### キーワード
| 使用 | 不使用 |
|------|--------|
| `typedef` | `type` |
| `with` | `[[derive()]]` |
| `T 宣言` | `fn 宣言() -> T` |

## コンパイラパイプライン

```
Source → Lexer → Parser → AST → HIR → MIR → LLVM IR → Native/WASM/JS
                                          ↘ インタプリタ
```

## ビルド・テスト

```bash
# ビルド
make build

# テスト
make tip    # インタプリタ
make tlp    # LLVM Native
make tjp    # JavaScript
```

## 開発ルール

### 一貫性の原則
機能開発時は以下を**すべて**更新：
- コンパイラ実装
- テスト
- lint/format
- tutorials/
- README.md
- VERSION

### デバッグ
- `-d`オプションを常に使用
- 不要なデバッグ出力は削除ではなくデバッグモードで活かす

### ドキュメント
- 生成ドキュメントに`00N_`プレフィックス
- 実装完了後`docs/archive/`へ移動

## ファイル配置

```
✓ tests/     - テストファイル
✓ docs/      - ドキュメント
✓ src/       - ソースコード
✓ examples/  - サンプル
✓ tutorials/ - チュートリアル
✗ ルート禁止  - *.cm, 個別md
```

## 参照

- `.agent/rules/` - 詳細な開発ルール
- `.agent/workflows/` - 開発ワークフロー
- `.agent/skills/` - チェックスキル
- `ROADMAP.md` - ロードマップ