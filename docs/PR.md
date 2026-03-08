# v0.14.2 Release - Cm言語コンパイラ

## 概要

v0.14.2は**コンパイラバグの修正**と**言語機能拡張**、**パフォーマンス改善**を含むパッチリリースです。特に**Tagged Union（Result/Option/enum）のペイロード抽出に関する重大なバグ2件**を修正し、ベアメタル環境での型安全なエラーハンドリングを実現しました。

---

## 🔥 v0.14.2 変更点

### 重大バグ修正: Tagged Union ペイロード

#### BUG-CRITICAL-1: Tagged Unionペイロード型の64bitハードコード問題

`mir_to_llvm.cpp`の`convertPlaceToAddress`でTagged Union（enum/Result/Option）のペイロード型が`hir::make_int()`（i32）にハードコードされていた。64bit型（long/ulong/pointer等）のペイロードが4バイトで切り詰められ、残り4バイトがゴミデータとなる。

```
症状: Result::Ok(0)のペイロード抽出で188050848088064等の不正値
原因: ペイロード型がi32固定 → 64bit値の上位4バイトが未読
修正: type_argsから動的に型推論し、64bit型存在時はi64を使用
```

#### BUG-CRITICAL-2: Tagged Unionペイロード部分書き込みによるundef bytes

`Result::Ok(0)`等のenum construct時、ペイロード値（i32）が`i8[N]`に部分書き込みされ、LLVM最適化が`memset+store`を`constant phi`に畳み込む際にundef bytesが生成される。

```
症状: ベアメタル環境でResult<ulong, long>::Ok(0)の値抽出にゴミデータ混入
原因: ペイロードストアが32bitのみ→残りバイトがundefined
修正:
  1. payload storeにZExt（Zero-Extension）を追加し正確なビット幅に拡張
  2. retval allocaにstruct型ゼロ初期化を追加
  3. resolve_typedefにモノモーフ化enum名フォールバック追加
```

> **影響**: この修正により`Result<T, E>`でT==E（例: `Result<long, long>`）が正しく動作するようになり、ベアメタル環境を含む全レイヤーでResult型エラーハンドリングが使用可能に。

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
| フォーマッター括弧内継続行 | 括弧内の継続行インデントを正しく処理 |

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

#### `__asm__`内の`${CONST_NAME}`定数展開

`lower_asm`でasm文字列中の`${CONST_NAME}`パターンを検出し、`global_const_values`テーブルから定数値を取得して16進数リテラルに直接置換する機能を追加。colonを含む`${+r:var}`等のオペランド記法はスキップ。

#### x86_64 Dockerクロスビルド対応

x86_64ターゲット向けDockerビルド環境を追加。LLVM共有ライブラリ + C++静的リンクの最適な構成。

### パフォーマンス改善

#### モノモーフィゼーション反復ループ最適化

反復ループを最大10パス→2パスに削減。2パス目は新規生成関数のみスキャン。`monomorphize_structs()`の2回実行を1回に統合。ネストジェネリクス（Queue等）にも対応。

#### テストランナーの並列化改善

PIDポーリングループ（`kill -0` + `sleep 0.05`）をFIFOセマフォ方式に置換。max_jobsをCPU数→CPU数×4に変更（I/Oバウンド考慮）。結果: **1:57 → 51.5s（55%短縮）**、CPU使用率 100% → 232%。

---

## 📁 変更ファイル一覧

### コンパイラコア

| ファイル | 変更内容 |
|---------|----------|
| `src/codegen/llvm/core/mir_to_llvm.cpp` | ASM volatile修正、DFE連携、**64bit payload型推論**、**ZExt付きペイロードストア**、**retvalゼロ初期化** |
| `src/codegen/llvm/core/mir_to_llvm.hpp` | volatile追跡メソッド追加 |
| `src/codegen/llvm/core/terminator.cpp` | switch文コード生成改善 |
| `src/codegen/llvm/core/utils.cpp` | ユーティリティ拡張 |
| `src/codegen/llvm/native/runtime_format.c` | ランタイムフォーマット関数リファクタリング |
| `src/codegen/llvm/native/target.cpp` | ターゲット設定改善 |

### MIR / 型解決

| ファイル | 変更内容 |
|---------|----------|
| `src/mir/lowering/lowering.cpp` | DFE統合 |
| `src/mir/lowering/base.cpp` | **resolve_typedefモノモーフ化enum名フォールバック** |
| `src/mir/lowering/context.cpp` | **resolve_typedef enum_defs検索フォールバック** |
| `src/mir/lowering/stmt.cpp` | **`__asm__`内`${CONST_NAME}`定数展開** |
| `src/mir/passes/cleanup/program_dce.cpp` | Dead Function Elimination実装 |
| `src/mir/passes/monomorphization_impl.cpp` | **モノモーフ反復ループ2パス最適化** |

### パーサー / フロントエンド

| ファイル | 変更内容 |
|---------|----------|
| `src/frontend/parser/parser.hpp` | パーサ宣言拡張 |
| `src/frontend/parser/parser_expr.cpp` | 型キーワード名前空間修飾子対応 |
| `src/frontend/parser/parser_module.cpp` | enum値CharLiteralサポート、無限再帰ガード |
| `src/frontend/parser/parser_stmt.cpp` | 文パーサ改善 |
| `src/frontend/types/checking/stmt.cpp` | **Tagged Union 64bitペイロード型チェック** |
| `src/preprocessor/import.cpp` | 型キーワードnamespace対応 |
| `src/preprocessor/import.hpp` | プリプロセッサ宣言拡張 |

### その他

| ファイル | 変更内容 |
|---------|----------|
| `src/fmt/formatter.cpp` | バッククォートインデント修正、括弧継続行修正 |
| `src/hir/lowering/expr.cpp` | HIR式lowering修正 |
| `src/mir/lowering/impl.cpp` | impl lowering改善 |
| `src/main.cpp` | メインエントリ修正 |
| `Dockerfile` | x86_64クロスビルド環境 (新規) |
| `tests/unified_test_runner.sh` | **FIFOセマフォ並列化 (55%短縮)** |

### テスト

| ファイル | 変更内容 |
|---------|----------|
| `tests/common/types/enum_char_value.cm` | enum値文字リテラルテスト (新規) |
| `tests/baremetal-x86/asm/asm_const_expand.cm` | asm定数展開テスト (新規) |

---

## 🧪 テスト状況

全バックエンドで0 FAILを維持。

| バックエンド | 通過 | 失敗 |
|------------|------|------|
| JIT (O0) | 365 | 0 |
| LLVM Native | 399 | 0 |

---

## ✅ チェックリスト

- [x] **Tagged Union 64bitペイロード型推論修正**
- [x] **Tagged Union ZExt+ゼロ初期化修正**
- [x] **resolve_typedefモノモーフ化フォールバック**
- [x] ASM過剰volatile修正
- [x] 型キーワードnamespace名衝突修正
- [x] Dead Function Elimination実装
- [x] enum値文字リテラルサポート
- [x] `__asm__`内`${CONST_NAME}`定数展開
- [x] モノモーフィゼーション反復ループ2パス最適化
- [x] テストランナーFIFOセマフォ並列化 (55%短縮)
- [x] x86_64 Dockerビルド対応
- [x] varargs/callee検索/文字列スライスバグ修正
- [x] パーサ無限再帰防止
- [x] フォーマッター修正 (バッククォート+括弧継続行)
- [x] interface impl関数DCE除去防止
- [x] ローカルパス情報なし

---

**バージョン**: v0.14.2