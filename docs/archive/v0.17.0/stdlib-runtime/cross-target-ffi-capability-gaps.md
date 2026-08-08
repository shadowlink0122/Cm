# R23: クロスターゲットFFIの能力ガード欠如（native専用モジュールがwasmへ黙ってコンパイル・js::timerコールバック型不能）

**ステータス:** 修正済み（wasmリンクの--allow-undefined全面許可を廃止しコンパイル時検出へ。js::timerの死んだコールバックAPIは撤去し制限を明文化）
**重大度:** Medium

ネイティブ専用のFFIモジュール（`std::env`/`std::process`/`std::fs`・`native::net`/`http`/`gpu`/`sync`/`thread`）を`--target=wasm`でコンパイルすると、jsが明確に拒否するのに対しwasmは無診断でコンパイルが通り、実行時（wasmインスタンス化時）に難解な`unknown import`で破綻する。同調査のD3・D5・D6/D7/D8で横断的に観測された同一根の問題。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt8/{os,concurrency,netgpuweb,verify}/`）

### バグ1【Medium】native専用モジュールがwasmで能力ガードなくコンパイル成功

```cm
import std::env;
int main() { return 0; }
```
実測:
- `--target=js`: **rc=1・明確診断** `error[JS]: the void* type is not available on the JS target (function: getenv)` + hint。
- `--target=wasm`: **rc=0・診断なし** `✓ compilation complete`。

同様に`std::process`/`std::fs`/`native::net`/`native::gpu`/`native::sync`/`native::thread`が全てwasmで無診断コンパイルされ、解決不能なenv importを出力する。実wasmを`wasmtime`で起動して初めて破綻する:
```
env: unknown import: `env::getenv` has not been defined
fs:  unknown import: `env::cm_file_read_all` has not been defined
net: unknown import: `env::cm_tcp_connect` has not been defined
gpu: unknown import: `env::gpu_device_create` has not been defined
```
原因: jsバックエンドは`void*`到達で偶発的にコンパイル時拒否できるが、wasmは`void*`を許容するため、ネイティブ専用FFI（BSDソケット・Metal・pthread）に**ターゲット能力ガードが無い**。各`mod.cm`冒頭は「JS/WASMバックエンドは未対応」と明記しているのに、その意図がコンパイラで強制されていない。純Cm実装の`std::bytes`/`std::path`はwasmで正しく動作する（正当）。

### バグ2【Low〜Medium】js::timerのコールバックがint型宣言で使用不能

`libs/js/`のtimerは`use js { int setTimeout(int callback, ...) }`とコールバックを`int`型で宣言しており、実関数を渡す手段がない。`--target=js`+nodeで`setTimeout(0, 5)`を実行するとnodeが未捕捉クラッシュ:
```
TypeError [ERR_INVALID_ARG_TYPE]: The "callback" argument must be of type function. Received type number (0)
```
コールバックAPIが事実上使用不能で、誤用時に診断でなくnode例外で落ちる。

### 健全だった点

js方向のクロスターゲット診断（`void*`禁止による拒否）は高品質。net/http・gpuのnative実行とエラーパス（接続拒否・DNS失敗・不正シェーダ）はクラッシュ/ハングなく健全。web::htmlのエスケープ（XSS遮断）はnative/js一致で健全。

## 修正方針

- **バグ1**: FFIモジュール（`use libc`/`use "..."`のネイティブ専用宣言）へターゲット能力タグを付け、非対応ターゲットでのコンパイルを「このモジュールは<target>では未対応」の専用診断で停止する。理想はランタイムビルトインレジストリ（runtime-builtin-registry）に各シンボルの対応ターゲット集合を持たせ、コンパイル時に照合する（wasm/jsを対称に扱い、`void*`到達の偶発的検出に依存しない）。
- **バグ2**: js::timerのコールバック引数を関数型（ラムダ）で宣言できるようにするか、現状の`int`型宣言では使えない旨をドキュメント化しつつ、非関数を渡した時にnode例外でなくコンパイル時診断へ。

## テスト計画

`tests/`へ: native専用モジュールを各非対応ターゲット（wasm/js）でコンパイルしたとき専用診断で停止する負のテスト（`unknown import`まで先送りしない）。js::timerのコールバック正常系または誤用診断。純Cmモジュール（bytes/path）はwasmで正常動作の回帰。

## 実装記録

- **バグ1（wasmの能力ガード欠如）**: 根本原因はwasmリンクコマンドの`--allow-undefined`全面許可で、未解決のネイティブFFIシンボルが黙ってenv importになっていた。フラグを撤去し、正当なWASI import（`__attribute__((import_module))`付き宣言）はそのまま機能させつつ、native専用FFI（getenv・cm_tcp_*・gpu_*・pthread等）は**コンパイル時にwasm-ldのundefined symbolエラー**（シンボル名つき）で停止するようにした。両リンクサイトの失敗時に「native専用FFIモジュールはwasm未対応」のヒントを追加。wasmスイート全件（600超）が無修正で通過し、正当なプログラムが全面許可へ依存していなかったことも確認した。理想形（レジストリのターゲット対応集合との照合による事前診断）はランタイムビルトインレジストリ拡張と合流する将来課題。
- **バグ2（js::timerのコールバック型不能）**: `int callback`宣言のsetTimeout/setInterval/clear系は実関数を渡す手段が無く、何を渡してもNode.jsのTypeErrorで未捕捉クラッシュする「死んだAPI」だった（動くコードが存在し得ないため撤去は非破壊）。宣言を撤去し、モジュールへ制限事項（FFIコールバックの関数型宣言は言語未サポート・遅延実行はasync/await推奨・関数型FFIサポートは将来課題）を明文化した。
- **テスト**: `tests/common/modules/wasm_native_ffi_reject.cm`（std::envのwasmコンパイルがリンク段で失敗することの負のテスト、platform: wasm限定）。純Cmモジュール（bytes/path）のwasm動作は既存スイートで担保。
- **教訓**: 「リンカの全面許可フラグ」は能力ガードの穴そのもの——許可は個別・属性ベース（import_module）で行い、未解決シンボルは既定でエラーにする。
