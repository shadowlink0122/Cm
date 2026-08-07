# R23: クロスターゲットFFIの能力ガード欠如（native専用モジュールがwasmへ黙ってコンパイル・js::timerコールバック型不能）

**ステータス:** 未修正（第8ラウンド検出）
**重大度:** Medium

ネイティブ専用のFFIモジュール（`std::env`/`std::process`/`std::fs`・`native::net`/`http`/`gpu`/`sync`/`thread`）を`--target=wasm`でコンパイルすると、jsが明確に拒否するのに対しwasmは無診断でコンパイルが通り、実行時（wasmインスタンス化時）に難解な`unknown import`で破綻する。第8ラウンドのD3・D5・D6/D7/D8で横断的に観測された同一根の問題。

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
