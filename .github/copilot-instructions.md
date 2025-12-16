# CLAUDE.md - Cm言語プロジェクトAIガイド

## 重要：開発前に必ず確認
1. `docs/design/CANONICAL_SPEC.md` - **正式言語仕様**（最優先）
2. `docs/design/architecture.md` - システム設計
3. `docs/PROJECT_STRUCTURE.md` - プロジェクト構造

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
- `#define` - 型付きコンパイル時定数
- `#macro` - コードマクロ（`#define`マクロは使わない）
- `overload` - 関数オーバーロード（明示的に必要）
- `constexpr` - コンパイル時計算
- `with` - 自動トレイト実装（`[[derive()]]`は使わない）

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
                            ↑
                        現在ここまで実装
```

### 実装済み
- ✓ Lexer/Parser（基本構文）
- ✓ HIR（高レベル中間表現）
- ✓ MIR（SSA形式、CFGベース）
- ✓ 基本的な型システム
- ✓ LLVMバックエンド（基本機能）

### 未実装
- ✗ 構造体/配列の完全サポート
- ✗ 完全なオーバーロード解決
- ✗ ジェネリック特殊化

### 廃止されたバックエンド
- ~~Rust/TS/C++トランスパイラ~~ → LLVMバックエンドに置き換え

## ビルド・テスト

```bash
# ビルド（LLVM有効）
cmake -B build -DCM_USE_LLVM=ON && cmake --build build

# テスト
ctest --test-dir build               # C++テスト
./build/bin/cm tests/regression/*.cm # Cmテスト
make test-llvm-all                   # LLVMテスト
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