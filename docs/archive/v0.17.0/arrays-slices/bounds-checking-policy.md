# 配列・スライスの境界チェック統一ポリシー（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| M1 | スライス/バックエンド | OOB読みはjit/native/wasm=0・js/ts=undefinedのセンチネル分裂、OOB書きはjit/native=SIGSEGV・wasm=不定書き込み・js=配列自動拡張と4分裂（境界トラップはどこにも無い、`--sanitize=bounds`はスライス非対応） | 実装済み（`cm_bounds_error`トラップをnative/wasm/jsへ新設し、MIRレベルの計装パス`instrument_bounds_checks`でスライスアクセス（get系・element_ptr経由の書き込み・delete）へ`0 <= index < len`検査を挿入。jit/native/wasm/jsの全実行系で同一メッセージ「error: index out of bounds: index N, length M」・終了コード1に統一。jsの`--sanitize=bounds`も新たに許可） |

## 背景と根本原因

境界外アクセス（OOB）の挙動がバックエンドごとに分裂しており、境界トラップはどこにも無い。

| 経路 | OOB読み | OOB書き | 根拠 |
|---|---|---|---|
| jit/native | 0（センチネル） | SIGSEGV | native/runtime_slice.c get=`return 0`、element_ptr NULL→ストア |
| wasm | 0（センチネル） | 不定書き込み | wasm/runtime_slice.c 同構造、element_ptr NULL=addr0 |
| js/ts | undefined | 配列自動拡張 | js/builtins.cpp get=`arr[i]`、set=`arr[i]=v` |

### スライスの読み取りアクセス（境界チェック無し）

添字読みは関数名を選ぶだけで、境界比較・トラップ生成が一切無い。

- src/internal/mir/lowering/expr/access.cpp:491-506 のget選択テーブル（:492デフォルト`cm_slice_get_i32`、:497-499スカラ幅別、:500-501 Pointer/String、:502-504 Union/Struct→`cm_slice_get_element_ptr`、:493-494多次元→`cm_slice_get_subslice`）。境界検査はランタイム任せで、`success_block`（:508）を作るが失敗ブロックは無く、Callはunwind先なし（:526の`std::nullopt`）。

### ランタイムはOOBでセンチネル返し（native/wasm）

- native: src/internal/codegen/llvm/native/runtime_slice.c の`cm_slice_get_i8/i16/i32/i64/f32/f64/ptr`（:321-405）が`if (index < 0 || index >= slice->len) return 0;`（例:326-327）でセンチネルを返す。`cm_slice_get_element_ptr`（:728）はOOBで`return NULL`（:732-733）。
- **`cm_slice_set_*`はnativeランタイムに存在しない**。スライス要素への書き込みは`cm_slice_get_element_ptr`が返すポインタ経由でストアする設計のため、OOB時はNULL返し→NULLデリファレンスへのストア→**native/jit=SIGSEGV**。
- wasm: src/internal/codegen/llvm/wasm/runtime_slice.c も同じセンチネル方式。`cm_slice_get_element_ptr`（:743）はOOBで`return NULL`（:747-748）。wasmではNULL=線形メモリのアドレス0が有効領域のため、element_ptr経由ストアがオフセット0へ書き込む→**wasm=不定書き込み**。

### js/tsは素のJS添字（undefined返し・自動拡張）

- 読み: src/internal/codegen/js/builtins.cpp:441-445 で`__cm_unwrap(arr)[i]`（OOBでundefined）。固定長配列も src/internal/codegen/js/emit_expressions.cpp:600-609 で素のJS添字。
- 書き: builtins.cpp:491-495 で`__cm_unwrap(arr)[i] = v`（OOB indexへの代入でJS配列が自動拡張、穴あき配列化）。

### --sanitize=boundsは固定長配列のみ（スライス非対応）

- CLIパース: src/cmd/cm/options.cpp:28-31（有効値`{"address","thread","memory","bounds","undefined"}`）、:156-158。ターゲット許可検証: src/cmd/cm/backend/llvm.cpp:117-125（`llvm_opts.sanitizeBounds = true`）。
- 計装実装: src/internal/codegen/llvm/native/codegen.cpp:618 `instrumentSanitizers()`、:661-662で`BoundsCheckingPass`を追加。コメント（:617）が「静的にサイズが分かるメモリアクセスへ境界チェックを挿入」と明記。
- LLVM `BoundsCheckingPass`は割り当てサイズが静的に判る対象（固定長配列のalloca/GEP）にしかトラップを挿入しない。スライスは`cm_slice_new`/`cm_alloc`で確保した不透明な`malloc`バッファを`cm_slice_get_*`の**関数呼び出し越し**にアクセスするため、パスからは割り当てサイズが見えず計装対象外。これが「boundsがスライスに効かない」根拠。
- 実行系別許可: src/cmd/cm/build.cpp:594-634（JSは`undefined`のみ、JITは`bounds`/`undefined`、sv非対応）。

### 境界専用トラップ関数は存在しない

- panic機構は存在する（src/internal/codegen/llvm/core/context.cpp:159 `declarePanicHandler`の`__cm_panic`、src/internal/codegen/llvm/core/utils.cpp:167 `generatePanic`、js側 builtins.cpp:211-214）が、呼び出し元はゼロ除算（operators.cpp:49）・不正unionキャスト（rvalue.cpp:326）・Result unwrap（expr_member.cpp:133-184）のみ。
- `cm_bounds_error`/`cm_oob`等の境界専用トラップ関数は全ツリーに存在しない（grepゼロ件）。

## 設計方針

「境界チェックポリシー」を導入し、全バックエンドで統一する。既定は無チェック（性能）だが、`--sanitize=bounds`を全アクセス（配列 + スライス）で機能させ、OOBを統一トラップにする。

### 1. 境界専用トラップ関数の新設

- `cm_bounds_error(int64_t index, int64_t len)`（仮）を全ランタイム（native/wasm）に追加し、`__cm_panic`経路（context.cpp:159 / utils.cpp:167）へ接続してメッセージ付きで即停止する。jsは`process.exit`（builtins.cpp:211-214）ベースの境界パニックを出す。
- これにより「境界トラップがどこにも無い」を解消する。

### 2. --sanitize=boundsをスライス対応にする

LLVMの`BoundsCheckingPass`（固定長配列専用）に依存せず、Cm側でスライスアクセスに明示的な境界チェックを挿入する。

- lowering段（access.cpp:491-526）でsanitizeBounds有効時に、`cm_slice_get_*`/element_ptr呼び出しの前に`index < 0 || index >= len`の比較を挿入し、違反時は`cm_bounds_error`へ分岐する。既存の`success_block`（:508）に対応する失敗ブロックを実際に生成する。
- 書き込み側（stmt/assign.cpp:157-195のIndex projection、およびelement_ptr経由ストア）にも同じチェックを挿入する。
- ランタイム関数側でも、sanitizeビルド用の境界チェック版を用意する選択肢を検討する（lowering挿入とランタイム内挿入のどちらが保守的か比較）。
- 固定長配列は既存の`BoundsCheckingPass`（codegen.cpp:661-662）を継続利用しつつ、スライスと同じ`cm_bounds_error`メッセージへ揃える。

### 3. OOB挙動の統一

sanitize無効時（既定）のOOB挙動も、可能な範囲で分裂を縮める。

- 最低限、OOB書きの分裂（SIGSEGV / 不定書き込み / 自動拡張）は最も危険なので、wasmの「アドレス0への不定書き込み」とjsの「自動拡張」を、少なくともsanitize有効時はトラップへ寄せる。
- OOB読みのセンチネル（0 / undefined）も、sanitize有効時はトラップで統一する。
- 既定（無チェック）の挙動をどこまで統一するかは性能とのトレードオフであり、段階分割で「まずsanitize有効時を統一」→「将来的に既定の書き込みトラップ化を検討」とする。

## 構文例・出力例

```cm
int[] a = [1, 2, 3];
println(a[10]);   // --sanitize=bounds 有効時: 境界トラップで停止
a[10] = 5;        // --sanitize=bounds 有効時: 境界トラップで停止（全バックエンド一致）
```

sanitize有効時のトラップ出力（設計目標）。

```
error: index out of bounds: index 10, length 3
```

sanitize無効時（既定）は従来通り性能優先だが、OOB書きの最も危険な分裂（wasm不定書き込み・js自動拡張）は将来的にトラップ寄せを検討する。

## 実装の段階分割

1. 第1段: `cm_bounds_error`トラップ関数をnative/wasm/jsに新設し、`__cm_panic`経路へ接続。
2. 第2段: `--sanitize=bounds`有効時に、スライスの読みアクセス（access.cpp:491-526）へ境界チェックを挿入し違反でトラップ。固定長配列と同じメッセージへ統一。
3. 第3段: スライス書き込み（element_ptr経由ストア、assign.cpp:157-195）へも境界チェックを挿入。
4. 第4段: js/tsのsanitize=boundsを実装（現状JITとnative/wasmのみ対応、build.cpp:594-634）。素のJS添字（builtins.cpp:441-445, 491-495）に境界チェックを付ける。
5. 第5段（検討）: 既定ビルドでのOOB書きトラップ化（wasmアドレス0書き込み・js自動拡張の抑止）。

## テスト計画（tests/common/配下）

境界チェックはsanitize有効時に停止するため、通常のexpect一致ではなく「終了コード/エラー出力」を検証する形が必要。既存の`tests/common/array/`や`errors`スイートの枠組みを使う。

- `tests/common/array/oob_read_sanitize_test.cm` + `.expect`: `--sanitize=bounds`でスライスOOB読みが全バックエンドでトラップ（同一メッセージ・非ゼロ終了）することを確認。sanitizeを付けない場合との差も確認。
- `tests/common/array/oob_write_sanitize_test.cm` + `.expect`: スライスOOB書きが全バックエンドでトラップし、wasmの不定書き込み・jsの自動拡張が起きないこと（negative check）を確認。
- `tests/common/array/oob_fixed_array_test.cm` + `.expect`: 固定長配列のOOBが既存`BoundsCheckingPass`とスライスで同一メッセージへ揃うことを確認。
- `tests/common/array/inbounds_no_overhead_test.cm` + `.expect`: 正常範囲アクセスがsanitize有無いずれでも正しい値を返す（誤検知が無い）ことを確認。

## リスクと非互換性

- sanitize有効時に、これまで「たまたま動いていた」（OOBで0を読んで先に進んでいた）プログラムがトラップで停止するようになる。これはバグ検出であり意図した挙動だが、リリースノートで明記する。
- 境界チェック挿入は性能オーバーヘッドを持つため、既定ビルドでは無効のまま（sanitize時のみ）とし、性能回帰を避ける。
- js/tsの境界チェックはJS添字を関数呼び出し or ラップへ変える必要があり、既存の素直な添字生成（emit_expressions.cpp:600-609）とのコード生成差分に注意する。
- ランタイム関数選択テーブル（access.cpp:491-506）は監査C4のelem_size一元化と同じ「手書き複数テーブル」問題を共有する。境界チェック挿入はこの一元化（ロードマップ第2段）と整合させ、ケース漏れを避ける。
- svバックエンドは配列/スライスの動的アクセス自体が限定的であり、本ポリシーの対象外（sv非対応、build.cpp:601-602）。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（M1、および型ディスパッチ一元化のC4・ロードマップ第2段/第3段）
- lowering: src/internal/mir/lowering/expr/access.cpp:491-526, src/internal/mir/lowering/stmt/assign.cpp:157-195
- ランタイム: src/internal/codegen/llvm/native/runtime_slice.c:321-405,:728, wasm/runtime_slice.c:743-752
- sanitize: src/cmd/cm/options.cpp:28-31,:156-158, src/cmd/cm/backend/llvm.cpp:117-125, src/internal/codegen/llvm/native/codegen.cpp:617-662, src/cmd/cm/build.cpp:594-634
- panic/trap: src/internal/codegen/llvm/core/context.cpp:159, utils.cpp:167, js/builtins.cpp:211-214,:441-445,:491-495

## 実装記録

- `cm_bounds_error(index, len)` をnative（runtime_print.c）・wasm（runtime_wasm.c、libc非依存のi64出力）・js（builtins.cppのインライン発行）へ追加。全実行系で同一メッセージ・終了コード1。
- `src/internal/mir/passes/instrumentation/bounds.cpp` に計装パスを新設。`--sanitize=undefined`と同じ「MIR最適化後に1回適用」の方式で、スライスアクセス呼び出しのブロックを分割し `cm_slice_len` 取得→負判定→範囲判定→`cm_bounds_error` の連鎖を挿入する。LLVM系に依存しないためjsでも同一動作。
- 固定長配列は既存のLLVM `BoundsCheckingPass` を継続利用（build.cpp）。
- テスト: `tests/sanitize/cases/oob/slice_read.cm`・`slice_write.cm` を追加し、native/jit/wasm/jsでメッセージ付きトラップと、sanitize無効時の既定挙動維持をE2E検証（52ケース全緑）。
- 残課題: 固定長配列のLLVM `BoundsCheckingPass` はシグナルトラップでありスライスのメッセージ形式と未統一。既定ビルド（sanitize無効時）のOOB書きトラップ化（第5段）は性能トレードオフの検討事項として残す。
