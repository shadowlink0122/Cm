# v0.14.2 Release - Cm言語コンパイラ

## 概要

v0.14.2は**Cosmo Linux（Cm言語製カーネル）開発で発見されたコンパイラバグの修正**と**言語機能拡張**を含むパッチリリースです。ASM含有関数の過剰volatile生成、型キーワードのnamespace名衝突を修正し、Dead Function Elimination (DFE)、enum値の文字リテラルサポート、Cosmo Linux向けDockerクロスビルド対応を追加しています。

---

## 🔥 v0.14.2 変更点

### バグ修正

#### BUG-1: ASM含有関数の過剰volatile生成

`__asm__`を含む関数で全ローカル変数に`volatile`属性が付与され、不要な`alloca → volatile store → volatile load`チェーンが生成される問題を修正。ASM operandで直接参照される変数のみvolatileにし、キャスト中間変数のvolatile属性を除去。

```
修正前: outb(ushort, utiny) → 13回のメモリ操作、0x43バイト
修正後: outb(ushort, utiny) → 0回のメモリ操作、0x4バイト
```

#### BUG-2: 型キーワードのnamespace名衝突

`import ../lib/string;` で展開される `namespace string { ... }` がCm組み込み型`string`と衝突する問題を修正。型キーワードをnamespace名・名前空間修飾子として受け入れるようパーサーを拡張。

#### その他のバグ修正

| 問題 | 修正内容 |
|------|----------|
| varargs関数のパラメータ数検証 | 可変長引数関数の引数数チェックを最小引数数でのガードに修正 |
| callee関数のシンボル検索失敗 | impl内関数のルックアップロジックを修正 |
| 文字列スライスの範囲外アクセス | スライス境界チェックを追加 |
| プリプロセッサのデバッグログ | debug_modeガードで保護し、通常ビルドでの出力を抑制 |
| パーサ無限再帰 | 安全ガードを追加し、進行しない再帰を防止 |
| DFEラムダ関数誤削除 | ラムダ関数をDFEスキップ対象に追加 |
| interface impl関数のDCE除去 | interface実装関数を未使用と誤判定しない修正 |
| フォーマッター1行バッククォート | インデントが崩壊するバグを修正 |

### 新機能

#### Dead Function Elimination (DFE)

MIR→LLVM変換時に未使用関数を除去する最適化パスを追加。ベアメタル環境でのバイナリサイズ削減に有効。

#### enum値の文字リテラルサポート

`parse_enum_decl`で`CharLiteral`を受け付けるよう拡張。文字リテラルを直接enum値として使用可能に。オートインクリメントも文字リテラル後に正常動作。

```cm
export enum Ascii {
    LowerA = 'a',
    LowerB,        // 98 (オートインクリメント)
    LowerC,        // 99
    UpperA = 'A',
    Digit0 = '0',
    BracketL = '[',
    Backslash,     // 92 (オートインクリメント)
    BracketR       // 93
}
```

#### Cosmo Linux向けDockerクロスビルド対応

x86_64ターゲット向けDockerビルド環境を追加。LLVM共有ライブラリ + C++静的リンクの最適な構成。

---

## 📁 変更ファイル一覧

### コンパイラコア

| ファイル | 変更内容 |
|---------|----------|
| `src/codegen/llvm/core/mir_to_llvm.cpp` | ASM operand変数のみvolatile化、DFE連携 (+245行) |
| `src/codegen/llvm/core/mir_to_llvm.hpp` | volatile追跡メソッド追加 |
| `src/codegen/llvm/core/terminator.cpp` | switch文コード生成改善 (+127行) |
| `src/codegen/llvm/core/utils.cpp` | ユーティリティ拡張 |
| `src/codegen/llvm/native/runtime_format.c` | ランタイムフォーマット関数リファクタリング |
| `src/codegen/llvm/native/target.cpp` | ターゲット設定改善 |
| `src/mir/lowering/lowering.cpp` | DFE統合 (+66行) |
| `src/mir/passes/cleanup/program_dce.cpp` | Dead Function Elimination実装 (+107行) |

### パーサー / フロントエンド

| ファイル | 変更内容 |
|---------|----------|
| `src/frontend/parser/parser.hpp` | パーサ宣言拡張 |
| `src/frontend/parser/parser_expr.cpp` | 型キーワード名前空間修飾子対応 (+67行) |
| `src/frontend/parser/parser_module.cpp` | enum値CharLiteralサポート、無限再帰ガード (+41行) |
| `src/frontend/parser/parser_stmt.cpp` | 文パーサ改善 (+14行) |
| `src/preprocessor/import.cpp` | 型キーワードnamespace対応 (+25行) |
| `src/preprocessor/import.hpp` | プリプロセッサ宣言拡張 |

### その他

| ファイル | 変更内容 |
|---------|----------|
| `src/fmt/formatter.cpp` | バッククォートインデント修正 |
| `src/hir/lowering/expr.cpp` | HIR式lowering修正 |
| `src/mir/lowering/impl.cpp` | impl lowering改善 |
| `src/main.cpp` | メインエントリ修正 |
| `Dockerfile` | x86_64クロスビルド環境 (新規) |

### テスト

| ファイル | 変更内容 |
|---------|----------|
| `tests/common/types/enum_char_value.cm` | enum値文字リテラルテスト (新規) |

---

## 🧪 テスト状況

全バックエンドで0 FAILを維持。

---

## ✅ チェックリスト

- [x] ASM過剰volatile修正
- [x] 型キーワードnamespace名衝突修正
- [x] Dead Function Elimination実装
- [x] enum値文字リテラルサポート
- [x] Cosmo Linux向けDockerビルド対応
- [x] varargs/callee検索/文字列スライスバグ修正
- [x] パーサ無限再帰防止
- [x] フォーマッター修正
- [x] interface impl関数DCE除去防止
- [x] ローカルパス情報なし

---

**バージョン**: v0.14.2