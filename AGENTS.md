# CLAUDE.md - Cm言語プロジェクトAIガイド

## 重要：開発前に必ず確認
1. `ROADMAP.md` - ロードマップ(最優先)
2. `docs/design/*.md` - **正式言語仕様**

## 言語仕様（必須）

### 構文スタイル：C++風（Rust風禁止）
```cm
// ✓ 正しい
int add(int a, int b) { return a + b; }
<T: Ord> T max(T a, T b) { return a > b ? a : b; }

// ✗ 間違い（Rust風）
fn add(a: int, b: int) -> int { }
```

### キーワード優先順位
- `typedef` - 型エイリアス（`type`は使わない）
- `overload` - 関数オーバーロード（明示的に必要）
- `with` - 自動トレイト実装（`[[derive()]]`は使わない）
- `T 宣言` - 変数・関数宣言(`fn 宣言() -> T`は使わない)

## プロジェクトルール（厳守）

### ファイル配置
```
✗ プロジェクトルート禁止：*.cm, 実行ファイル, 個別ドキュメント
✓ tests/        - 全テストファイル
✓ docs/         - 全ドキュメント
✓ src/          - ソースコード
✓ examples/     - サンプルコード
```

### デバッグ出力
```cpp
// 正しい形式：[STAGE] のみ（1つのブラケット）
[LEXER] Starting tokenization
[PARSER] ERROR: Expected ';'
[MIR] Lowering function: main

// ✗ 間違い：[STAGE] [LEVEL] （2つのブラケット）
```

## コンパイラパイプライン

```
Lexer → Parser → AST → HIR → MIR → LLVM IR → Native/WASM
                                 → インタプリタ
```

### 実装済み
- ✓ Lexer/Parser（基本構文）
- ✓ HIR（高レベル中間表現）
- ✓ MIR（SSA形式、CFGベース）
- ✓ 基本的な型システム
- ✓ LLVMバックエンド（基本機能）

## ビルド・テスト

```bash
# ビルド（LLVM有効）
cmake -B build -DCM_USE_LLVM=ON && cmake --build build

# テスト
ctest --test-dir build               # C++テスト
./build/bin/cm tests/regression/*.cm # Cmテスト
make tip                             # インタプリタテスト
make tlp                             # LLVMテスト
make tlwp                            # WASMテスト
```

## 開発時の注意

1. **矛盾時は`CANONICAL_SPEC.md`が最優先**
2. **C++20必須**（Clang 17+推奨） 
3. **テストは必ず`tests/`に配置**
4. **新機能は設計文書を先に作成**

## AI向けヒント
- 思考は英語、コメントは日本語
- エラーメッセージは具体的に
- デバッグモード(--debug, -d)を必ず使うこと
- デバッグメッセージはdebug_msgで表示するためのIDとメッセージを正しく設定すること
- 実装前に既存コードを確認
- テスト駆動開発を推奨
- 機能実装時に他の機能が壊れていないことを確認
- ファイル構成はシンプルにする
- 1ファイルは1000行が目安。それ以上になる場合は分割を検討