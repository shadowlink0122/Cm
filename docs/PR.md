[English](PR.en.html)

# v0.14.1 Release - Cm言語コンパイラ

## 概要

v0.14.1は**UEFIコンパイラバグ17件の修正**、**typedef算術演算サポート**、**GCC/Linux CIビルド修正**を含むパッチリリースです。CosmOS UEFI開発で発見されたコンパイラバグを全件修正し、回帰テストを多数追加。整数型出力の完全対応、naked関数コード生成の根本修正、MIR最適化パスのASM対応など広範な安定化を実施しています。

v0.14.0の主な変更: **JavaScriptバックエンドの大規模改善**、**演算子オーバーロードの設計改善**、**ベアメタル/UEFIサポート**、**インラインユニオン型 (`int | null`)**、**プラットフォームディレクティブ**、**VSCode拡張機能の品質改善**。

---

## 🔥 v0.14.1 変更点

### UEFIコンパイラバグ全件修正（Bug#1〜#17）

CosmOS UEFI開発中に発見されたコンパイラバグ17件を全て修正しました。

| Bug# | 問題 | 修正内容 |
|------|------|----------|
| #1 | char switchの符号拡張 | switch_intのchar型比較を修正 |
| #5 | LICM最適化がASM出力変数を移動 | ASM出力変数をループ不変と誤判定しない修正 |
| #6 | constant foldingのASM出力追跡 | ASM→変数代入チェーンの最適化を抑制 |
| #7 | `utiny*`デリファレンスのi32切り詰め | ポインタデリファレンスの型幅修正 |
| #8 | `int→utiny`キャストの符号拡張 | trunc命令の正しい適用 |
| #9 | 構造体フィールドの定数畳み込み | フィールドアクセスの最適化を抑制 |
| #10 | `ptr->method()`のself書き戻し | MIR loweringでポインタ経由の書き戻し実装 |
| #11 | UEFIレジスタマッピング不正 | x86_64 UEFI ABIに合わせたレジスタリマップ |
| #12 | naked関数のプロローグ/エピローグ干渉 | Naked+$N事前置換方式に統一 |
| #13 | LLVM最適化によるcall/ret消滅 | UEFIターゲットの最適化レベル調整 |
| #14 | 構造体配列の全体再代入でゴミ値 | memcpy/配列代入の型サイズ修正 |
| #15 | 非export関数がexport関数から呼出不可 | シンボル解決の修正 |
| #16 | `&local as ulong`キャスト不正 | ポインタ→整数キャストの修正 |
| #17 | UEFIスタックプローブクラッシュ | スタックプローブの無効化 |

### typedef算術演算サポート

typedef型の値に対する算術演算と、typedef引数のstatic→static関数呼び出し時の型不整合を修正しました。

```cm
typedef EFI_STATUS = ulong;
EFI_STATUS status = 0;
if (status != 0) { /* 修正前: コンパイルエラー → 修正後: 正常動作 */ }
```

### 整数型出力の完全対応

MIR loweringのprintln関数選択ロジックにlong/ulong/uint/isize/usize型のケースを追加。

### GCC/Linux CIビルド修正

`src/mir/nodes.hpp`に`<unordered_map>`ヘッダーを追加。AppleClangでは間接インクルードで解決されていたが、GCCでは明示的なインクルードが必要。

### JS/WASMランタイム改善

| ファイル | 変更内容 |
|---------|----------|
| `src/codegen/js/builtins.cpp` | `cm_println_long`/`ulong`/`uint`とformat/to_string版追加 |
| `src/codegen/llvm/wasm/runtime_print.c` | `cm_println_long`/`ulong`出力関数追加 |
| `src/codegen/llvm/core/operators.cpp` | ビット演算（BitAnd/BitOr/BitXor）の型幅統一ロジック追加 |
| `src/mir/lowering/expr_call.cpp` | println型選択にlong/ulong/uint/isize/usizeケース追加 |
| `src/mir/passes/interprocedural/inlining.cpp` | ASM含有関数のインライン展開禁止 |

---

## 🎯 主要な変更

### 1. JSバックエンド大規模リファクタリング

JSコードジェネレータを大幅にリファクタリングし、1,600行以上の不要コードを削除しました。

| 変更 | 詳細 |
|------|------|
| codegen.cpp | -1,618行（大規模整理） |
| emit_expressions.cpp | +124行（式出力改善） |
| emit_statements.cpp | +80行（文出力改善） |
| builtins.hpp | +71行（ビルトイン拡充） |

#### JSテスト通過率

| バージョン | パス | 失敗 | スキップ | 通過率 |
|-----------|------|------|---------|--------|
| v0.13.1 | 206 | 119 | 47 | 55% |
| **v0.14.0** | **298** | **0** | **49** | **87%** |

#### JSコンパイルの使い方

```bash
./cm compile --target=js hello.cm -o output.js
node output.js
```

### 2. 演算子オーバーロード改善

#### `impl T { operator ... }` 構文

演算子を`impl T for InterfaceName`ではなく、直接`impl T { operator ... }`で定義可能になりました。

```cm
struct Vec2 { int x; int y; }

impl Vec2 {
    operator Vec2 +(Vec2 other) {
        return Vec2{x: self.x + other.x, y: self.y + other.y};
    }
}
```

#### 複合代入演算子

二項演算子を定義すると、対応する複合代入演算子が自動的に使えます。

```cm
Vec2 v = Vec2{x: 10, y: 20};
v += Vec2{x: 5, y: 3};   // v = v + Vec2{5, 3} と同等
v -= Vec2{x: 2, y: 1};   // v = v - Vec2{2, 1} と同等
```

**サポートする複合代入演算子**: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

#### ビット演算子オーバーロード

`&`, `|`, `^`, `<<`, `>>` の全ビット演算子をオーバーロード可能になりました。

#### interface存在チェック

`impl T for I` の `I` が宣言済みinterfaceでない場合、コンパイルエラーになります。

### 3. インラインユニオン型とnull型

#### インラインユニオン構文 (`int | null`)

typedefなしで直接ユニオン型を使用可能になりました。

```cm
int | null a = null;           // nullが代入可能
int | null b = 42 as MaybeInt; // int値も代入可能
int | string | null c = null;  // 3型以上のユニオンも可能
```

| 変更 | 詳細 |
|------|------|
| `null`型追加 | `TypeKind::Null`、`make_null()` |
| `parse_type_with_union()` | 変数宣言・関数戻り値・構造体フィールドで使用 |
| 型互換性 | Unionメンバー型とのnull代入・値代入に対応 |

### 4. プラットフォームディレクティブ

ファイル先頭に `//! platform:` で実行可能なプラットフォームを制約可能。

```cm
//! platform: native
// このファイルはLLVM Native/JITでのみコンパイル可能
```

対応プラットフォーム: `native`, `js`, `wasm`, `uefi`, `baremetal`

### 5. ベアメタル / UEFIサポート

`--target=uefi` でUEFIアプリケーションをコンパイル可能。QEMUでHello World出力確認済み。

```cm
// UEFI Hello World
import ./libs/efi_core;
import ./libs/efi_text;

ulong efi_main(void* image_handle, void* system_table) {
    efi_clear_screen(system_table);
    string msg = "Hello World from Cm!";
    efi_println(system_table, msg as void*);
    while (true) { __asm__("hlt"); }
    return 0;
}
```

- インラインASM自動クロバー検出を実装
- UEFIライブラリ (`libs/uefi/`) を新規作成
- ベアメタル向けno_std実行プロファイル対応

### 6. VSCode拡張機能の追加と改善

#### 拡張機能の新規追加

Cm言語用VSCode拡張機能を新規作成しました。

- **構文ハイライト**: TextMate文法定義 (`cm.tmLanguage.json`)
- **ファイルアイコン**: `.cm`ファイルにCmアイコンを表示
- **言語設定**: ブラケットマッチング、折りたたみ、インデント支援
- **VSIXパッケージ**: `pnpm run package` でインストール可能なパッケージを生成

#### TypeScript移行 + ESLint/Prettier導入

スクリプト（`update-version`, `verify-version`）をJavaScript→TypeScriptに移行し、ESLint + Prettierで品質管理を自動化。

| ツール | 設定 | コマンド |
|--------|------|---------|
| TypeScript | `tsconfig.json` (strict, ES2020) | `pnpm run compile` |
| ESLint | `eslint.config.mjs` (Flat Config v9+) | `pnpm run lint` |
| Prettier | `.prettierrc` | `pnpm run format:check` |

#### CI統合

`ci.yml` に `extension-lint` ジョブを追加。push/PRごとにcompile → lint → format:checkを自動チェック。

### 7. サンプルプロジェクト

#### Webアプリサンプル (`examples/web-app/`)

Cm言語でロジックを記述し、JSバックエンドでコンパイルしてブラウザ上で動作するWebアプリのサンプルを追加。HTMLテンプレートをバッククォート複数行文字列で記述する構成。

#### UEFIサンプル (`examples/uefi/`)

UEFI Hello Worldプログラムを`examples/uefi/`に整理。QEMUでの実行手順を含む。

---

## 🐛 バグ修正

### v0.14.1 修正（UEFIコンパイラバグ + 言語機能）

| 問題 | 原因 | 修正ファイル |
|-----|------|------------|
| **Bug#1** char switchの符号拡張 | switch_intがchar型を符号拡張 | `mir_to_llvm.cpp` |
| **Bug#5** LICMがASM出力を移動 | ASM出力変数をループ不変と誤判定 | `licm.cpp` |
| **Bug#6** constant foldingのASM追跡 | ASM→変数代入の最適化抑制漏れ | `folding.cpp` |
| **Bug#7** `utiny*`デリファレンスi32切り詰め | ポインタデリファレンスの型幅不正 | `mir_to_llvm.cpp` |
| **Bug#8** `int→utiny`キャスト不正 | trunc命令の適用ミス | `mir_to_llvm.cpp` |
| **Bug#9** 構造体フィールドの定数畳み込み | フィールドアクセス最適化の抑制漏れ | `folding.cpp` |
| **Bug#10** `ptr->method()`のself書き戻し | ポインタ経由の書き戻し未実装 | `stmt.cpp` (MIR) |
| **Bug#11** UEFIレジスタマッピング不正 | UEFI ABIレジスタリマップ欠落 | `mir_to_llvm.cpp` |
| **Bug#12** naked関数のプロローグ干渉 | module-asm方式の不具合 | `mir_to_llvm.cpp` |
| **Bug#13** LLVM最適化でcall/ret消滅 | 最適化レベル調整 | `codegen.cpp` (native) |
| **Bug#14** 構造体配列の再代入でゴミ値 | 配列代入のサイズ計算不正 | `mir_to_llvm.cpp` |
| **Bug#15** 非export関数がexportから呼出不可 | シンボル解決の不備 | `import.cpp` |
| **Bug#16** `&local as ulong`キャスト不正 | ポインタ→整数キャスト未対応 | `mir_to_llvm.cpp` |
| **Bug#17** UEFIスタックプローブクラッシュ | スタックプローブの無効化 | `codegen.cpp` (native) |
| typedef算術演算エラー | typedef型のis_numeric判定漏れ | `checking/expr.cpp` |
| typedef引数の型不整合 | static→static呼び出し時の型解決 | `monomorphization_impl.cpp` |
| GCC CIビルドエラー | `<unordered_map>`ヘッダー未インクルード | `nodes.hpp` |
| 大きな16進リテラルのprintln | println関数選択にlong/ulong未対応 | `expr_call.cpp` |
| ビット演算の型幅不一致 | BitAnd/BitOr/BitXorに型統一ロジック欠落 | `operators.cpp` |
| ASM関数のインライン展開 | ASM含有関数がインライン展開 | `inlining.cpp` |
| JS/WASMでlong/ulong未出力 | ランタイムにcm_println_long等未定義 | `builtins.cpp`, `runtime_print.c` |

### v0.14.0 修正

| 問題 | 原因 | 修正ファイル |
|-----|------|------------|
| println型判定の誤り | AST型チェッカーがmatch armのpayload変数の型を`int`に設定 | `expr_call.cpp` |
| ペイロードロードエラー | Tagged Unionの非構造体ペイロード型が`i32`にハードコード | `mir_to_llvm.cpp` |
| 構造体ペイロードサイズ計算 | `max_payload_size()`がStruct型をデフォルト8バイトで計算 | `types.cpp` |
| プラットフォーム不一致セグフォ | プラットフォーム制約のないファイルでクラッシュ | プリプロセッサ修正 |
| Boolean定数オペランドの型判定 | JSバックエンドでBoolean定数の型が不正 | JS codegen修正 |
| 並列テストのレースコンディション | テストランナーのファイル名衝突 | テストランナー修正 |

---

## 🔧 ビルド・テスト改善

### JSスキップファイル整理

| カテゴリ | 理由 |
|---------|------|
| `asm/` | インラインアセンブリはJS非対応 |
| `io/` | ファイルI/OはJS非対応 |
| `net/` | TCP/HTTPはJS非対応 |
| `sync/` | Mutex/Channel/AtomicはJS非対応 |
| `thread/` | スレッドはJS非対応 |

### CI改善

- JSバックエンドテストをCIに追加
- VERSIONファイルとブランチ名の整合チェックCI追加
- VSCode拡張機能lint CIジョブ追加
- UEFIコンパイルテストCIジョブ追加
- ベアメタルコンパイルテストCIジョブ追加
- GPUテスト全バックエンドスキップ
- タイムアウトテスト根本修正
- 不安定なoperator_comprehensiveテストを5つの個別テストに分割

### テスト構成再編成

- `tests/test_programs` → `tests/programs` にリネーム
- ライブラリを `libs/` 配下にプラットフォーム別で再構成

---

## 📁 変更ファイル一覧

### JSバックエンド

| ファイル | 変更内容 |
|---------|---------| 
| `src/codegen/js/codegen.cpp` | 大規模リファクタリング（-1,618行） |
| `src/codegen/js/emit_expressions.cpp` | 式出力改善 |
| `src/codegen/js/emit_statements.cpp` | 文出力改善 |
| `src/codegen/js/builtins.hpp` | ビルトイン関数拡充 |
| `src/codegen/js/runtime.hpp` | ランタイムヘルパー追加 |
| `src/codegen/js/types.hpp` | 型マッピング改善 |

### LLVMバックエンド/MIR修正

| ファイル | 変更内容 |
|---------|---------|
| `src/codegen/llvm/core/types.cpp` | Tagged Unionペイロードサイズ計算修正 |
| `src/codegen/llvm/core/mir_to_llvm.cpp` | ペイロードロード修正、自動クロバー検出、naked関数統一、Bug#1/7/8/11/14/16修正 |
| `src/codegen/llvm/native/codegen.cpp` | Bug#13/17: UEFI最適化レベル調整、スタックプローブ無効化 |
| `src/mir/lowering/expr_call.cpp` | println型判定修正 |
| `src/mir/lowering/stmt.cpp` | Bug#10: ptr->method()のself書き戻し |
| `src/mir/lowering/monomorphization_impl.cpp` | typedef引数の型解決修正 |
| `src/mir/lowering/impl.cpp` | impl lowering改善 |
| `src/mir/passes/scalar/folding.cpp` | Bug#6/9: ASM出力・構造体フィールド最適化抑制 |
| `src/mir/passes/loop/licm.cpp` | Bug#5: ASM出力変数のループ不変判定修正 |
| `src/mir/nodes.hpp` | GCC CIビルド修正（unordered_mapヘッダー追加） |
| `src/frontend/types/checking/expr.cpp` | typedef算術演算サポート |
| `src/preprocessor/import.cpp` | Bug#15: 非export関数のシンボル解決修正 |

### 型チェッカー/パーサー

| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/parser/parser.hpp` | `impl T { operator ... }` 構文、インラインユニオン型 |
| `src/frontend/types/checking/expr.cpp` | 複合代入演算子の構造体オーバーロード対応 |
| `src/frontend/types/checking/decl.cpp` | interface存在チェック・operator自動登録 |

### VSCode拡張機能

| ファイル | 変更内容 |
|---------|---------|
| `vscode-extension/` | 拡張機能全体を新規追加 |
| `vscode-extension/syntaxes/cm.tmLanguage.json` | TextMate文法定義（710行） |
| `vscode-extension/scripts/*.ts` | TypeScript版スクリプト |
| `vscode-extension/eslint.config.mjs` | ESLint Flat Config |
| `vscode-extension/.prettierrc` | Prettier設定 |
| `vscode-extension/tsconfig.json` | TypeScript設定 |
| `.github/workflows/ci.yml` | extension-lintジョブ、baremetal-testジョブ追加 |

### チュートリアル・ドキュメント

| ファイル | 変更内容 |
|---------|---------|
| `docs/tutorials/ja/advanced/operators.md` | 演算子チュートリアル全面改訂 |
| `docs/tutorials/en/advanced/operators.md` | 英語版演算子チュートリアル全面改訂 |
| `docs/tutorials/ja/basics/operators.md` | ビット演算子チュートリアル追加 |
| `docs/tutorials/ja/basics/setup.md` | エディタ設定セクション大幅拡充 |
| `docs/tutorials/ja/compiler/uefi.md` | UEFIチュートリアル追加 |
| `docs/tutorials/ja/compiler/js-compilation.md` | JSチュートリアル追加 |
| `docs/releases/v0.14.0.md` | リリースノート更新 |
| `docs/QUICKSTART.md` | VSCode拡張機能リンク追加 |
| `vscode-extension/README.md` | 開発ガイド全面刷新 |

### テスト

| ファイル | 変更内容 |
|---------|---------|
| `tests/programs/interface/operator_arithmetic.*` | 算術演算子テスト（分割） |
| `tests/programs/interface/operator_compare.*` | 比較演算子テスト（分割） |
| `tests/programs/interface/operator_bitwise.*` | ビット演算子テスト（分割） |
| `tests/programs/interface/operator_compound_assign.*` | 算術複合代入テスト（分割） |
| `tests/programs/interface/operator_bitwise_assign.*` | ビット複合代入テスト（分割） |
| `tests/programs/interface/operator_add.*` | impl T構文テスト |
| `tests/programs/enum/associated_data.*` | .error → .expected |
| `tests/programs/asm/.skip` 等 | JSスキップファイル追加 |
| `tests/unified_test_runner.sh` | テストランナー改善 |
| `tests/programs/uefi/uefi_compile/*` | UEFIコンパイルテスト多数追加（Bug#1-17回帰テスト含む） |
| `tests/programs/baremetal/allowed/*` | ベアメタルテスト3件追加（enum/配列/ポインタ） |
| `tests/programs/common/types/ptr_to_int_cast.*` | ポインタ→整数キャストテスト追加 |
| `tests/programs/common/types/typedef_arithmetic.*` | typedef算術演算テスト追加 |

### サンプル

| ファイル | 変更内容 |
|---------|---------|
| `examples/web-app/` | Webアプリサンプル追加 |
| `examples/uefi/` | UEFIサンプル整理 |

---

## 🧪 テスト状況

| バックエンド | 通過 | 失敗 | スキップ |
|------------|-----|------|---------| 
| JIT (O0) | 347 | 0 | 4 |
| LLVM Native | 380 | 0 | 7 |
| LLVM WASM | 346 | 0 | 5 |
| JavaScript | 306 | 0 | 49 |
| Baremetal | 11 | 0 | 0 |
| UEFI | 5 | 0 | 0 |

---

## 📊 統計

- **テスト総数**: 351
- **JIT通過**: 347（0失敗）
- **LLVM通過**: 380（0失敗）
- **WASM通過**: 346（0失敗）
- **JS通過**: 306（0失敗、v0.13.1の206から+100改善）

---

## ✅ チェックリスト

- [x] `make tip` 全テスト通過（347 PASS / 0 FAIL）
- [x] `make tlp` 全テスト通過（380 PASS / 0 FAIL）
- [x] `make tw` 全テスト通過（346 PASS / 0 FAIL）
- [x] `make tjp` 全テスト通過（306 PASS / 0 FAIL）
- [x] VSCode拡張機能 lint通過（compile + ESLint + Prettier）
- [x] ベアメタルテスト通過（11 PASS / 0 FAIL）
- [x] UEFIテスト通過（5 PASS / 0 FAIL）
- [x] GCC/Linux CIビルド通過
- [x] リリースノート更新（`docs/releases/v0.14.0.md`）
- [x] チュートリアル更新（演算子、UEFI、JS、環境構築）
- [x] VSCode拡張機能README更新
- [x] QUICKSTART.md更新
- [x] ローカルパス情報なし

---

**リリース日**: 2026年2月19日
**バージョン**: v0.14.1