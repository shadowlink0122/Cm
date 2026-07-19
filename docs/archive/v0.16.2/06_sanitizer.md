# 実装設計: native/wasm向けサニタイザ（--sanitize）

## 背景・課題

Cmのネイティブバックエンドは配列アクセスやポインタ演算に実行時検査を挿入しないため、境界外アクセスやヒープ破壊は不定動作としてサイレントに進行する。
C/C++/Rustエコシステムの標準であるサニタイザ（実行時メモリ検査）を `cm compile` に導入し、native/wasmターゲットで不正メモリアクセスを実行時に検出できるようにする。

## 設計

### CLI

```bash
cm compile --sanitize=address main.cm            # AddressSanitizer（nativeのみ）
cm compile --sanitize=bounds main.cm             # 境界チェック（native/wasm/jit）
cm compile --sanitize=address,bounds main.cm     # 併用
cm compile --target=wasm --sanitize=bounds main.cm
cm compile --sanitize=bounds --run main.cm       # 計装済みバイナリを即実行
cm run --sanitize=bounds main.cm                 # JIT実行に境界チェックを計装
```

### サポートするサニタイザ

| 値 | 検出内容 | native | wasm | jit (`cm run`) | 仕組み |
|---|---|---|---|---|---|
| `address` | ヒープ/スタック/グローバルの境界外アクセス・use-after-free・二重解放 | ○ | ×（エラー） | ×（エラー） | LLVM `AddressSanitizerPass` による計装 + ASanランタイムのリンク |
| `bounds` | コンパイル時にオブジェクトサイズが確定するメモリアクセスの境界外読み書き | ○ | ○ | ○ | LLVM `BoundsCheckingPass`（clangの `-fsanitize=local-bounds` 相当）。違反時は `llvm.trap` で即停止し、ランタイム不要 |

- `address` がwasmで使えないのはASanランタイムがwasm32-wasi向けに存在しないため（Emscripten専用）。wasm指定時は明確なエラーメッセージを出す
- `address` がjitで使えないのは、JITがcmプロセス内で実行されるためASanランタイム（プロセス起動時のmallocインターセプトが前提）を後付けロードできないため。`cm compile --sanitize=address --run` を案内する
- `bounds` はtrap方式でランタイムに依存しないため全ネイティブ環境・wasm（wasmtimeのtrap終了）・JIT（プロセス内trap）で動作する。検出できるのはLLVMがオブジェクトサイズを静的に決定できるアクセスに限られる（部分的カバレッジであることをドキュメントに明記）
- js・svターゲットは非対応としてエラーにする
- baremetal/uefiはランタイム・trapハンドラの前提が成立しないため非対応エラー

### 実装方針

1. **CLIパース**（`src/cmd/cm/options.{hpp,cpp}`）: `Options::sanitizers`（`std::vector<std::string>`）を追加し、`--sanitize=a,b` をカンマ分割でパースする。未知の値は既存の `--funroll-loops` と同じ方式で `has_error` + i18nメッセージを設定する
2. **ターゲット検証**（`src/cmd/cm/backend/llvm.cpp` ほか）: バックエンド決定後に組み合わせを検証し、非対応の場合はi18nエラーで終了する（address×wasm、sanitize×js/sv/jit/baremetal）
3. **計装**（`src/internal/codegen/llvm/native/codegen.cpp`）: `LLVMCodeGen::Options` に `sanitizeAddress` / `sanitizeBounds` を追加し、`compile()` の `optimize()` 直後に新設の `instrumentSanitizers()` を呼ぶ
   - `optimize()` はO0で早期returnするため、計装は独立ステップとして最適化レベルに関わらず実行する
   - `address`: 本体を持つ全関数へ `SanitizeAddress` 属性を付与（`Naked` 属性の純ASM関数は除外）した上で `AddressSanitizerPass` を実行する
   - `bounds`: `createModuleToFunctionPassAdaptor(BoundsCheckingPass())` を実行する
4. **リンク**（`emitExecutable()`）: `address` 有効時はリンクコマンドへ `-fsanitize=address` を追加してASanランタイムをリンクする
   - macOSではAppleクリップのランタイムとLLVM 17計装のバージョン記号（`__asan_version_mismatch_check_v8`）の不一致を避けるため、wasmビルドと同じ探索順でHomebrew LLVMのclangを優先し、見つからなければ `/usr/bin/clang++` にフォールバックする
5. **メッセージ**: 新規MsgId（未知のサニタイザ値・ターゲット非対応2種）を `message_ids.hpp` / `messages.cpp` に追加（英日）
6. **ヘルプ**: `src/cmd/cm/help/{en,ja}.txt` に `--sanitize=<list>` を追記する

### 出力例

```
$ cm compile --sanitize=bounds oob.cm -o oob && ./oob
zsh: trap: illegal hardware instruction  ./oob   # 境界外アクセスでtrap（終了コード非0）

$ cm compile --sanitize=address uaf.cm -o uaf && ./uaf
==12345==ERROR: AddressSanitizer: heap-use-after-free on address 0x... # ASanレポート

$ cm compile --sanitize=address --target=wasm main.cm
error: sanitizer 'address' is not supported on target 'wasm' (only native)

$ cm compile --sanitize=foo main.cm
error: unknown sanitizer 'foo'
valid sanitizers: address, bounds
```

### 段階分割

1. CLIオプション・i18nメッセージ・ターゲット検証
2. LLVM計装パス（bounds → address の順）とリンク対応
3. E2Eテスト（`tests/sanitize/run_tests.sh` + `make test-sanitize` + CI組込み）
4. チュートリアル（ja/en）・リリースノート

### テスト計画

- **E2Eテスト**（`tests/sanitize/run_tests.sh`、`make test-sanitize`）:
  - 正常系: 境界内アクセスのみのプログラムが `--sanitize=address` / `--sanitize=bounds` 付きでも正しい出力・終了コード0で動作する
  - 検出系: 固定長配列の境界外書き込みが `bounds` でtrap（非0終了）、ヒープ境界外アクセスが `address` でASanレポート（非0終了 + stderrに `AddressSanitizer`）になる
  - IR検証: `--emit-llvm` の出力に `sanitize_address` 属性（address）・`__asan_init` 参照が現れること
  - CLI検証: 未知の値・非対応ターゲットの組み合わせが規定のエラーメッセージで非0終了する
  - wasm: `--target=wasm --sanitize=bounds` の境界外アクセスがwasmtimeでtrap終了する（wasmtime未導入環境では明示的にSKIP表示）
- **CI**: Toolingジョブへ `make test-sanitize` を追加（Linux。ASanランタイムはLLVM 17インストールに含まれる）

### 将来課題

- `thread`（TSan）・`memory`（MSan）: LLVMパスは存在するためaddressと同じ構造で追加可能。データ競合E2Eの安定化が課題
- JIT実行時のサニタイザ（ランタイムのプロセス内ロードとシャドウメモリ設定が必要）
- `.cmconfig.yml` の `compile.sanitize` 対応（CLI > config の優先順位で）
- MIRレベルの言語仕様準拠チェック（null参照・ゼロ除算等のCm独自サニタイザ。全バックエンド共通化できる）
