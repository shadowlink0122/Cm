---
title: インクリメンタルビルドと並列コード生成
---

# インクリメンタルビルドと並列コード生成

LLVMバックエンドのビルド基盤は、既定の「全体を単一モジュールとしてコンパイルする経路」と、`CM_MODULE_CODEGEN=1` で有効になる「モジュール分割+内容アドレスキャッシュ+ワーカスレッド並列コンパイル」の2経路を持つ。分割経路はモジュールごとのMIR内容ハッシュをキーに `.o` を内容アドレスでキャッシュし、ミスしたモジュールだけを独立LLVMContextで並列にコード生成してリンク段で結合することで、関数数に対して超線形化しやすいコード生成・最適化コストをモジュール単位に分割・省略する。

## 概要

既定経路は `LLVMCodeGen::compile`（src/internal/codegen/llvm/native/codegen.cpp:35-105）で、初期化→MIR→LLVM IR変換→検証→最適化→サニタイザ計装→出力を単一モジュールで行う。ドライバの `compileWithModuleInfo` はこの全体コンパイルへ委譲するラッパである（codegen.cpp:110-120）。

分割経路はドライバ `emit_llvm` の実験ゲートで選択される（src/cmd/cm/backend/llvm.cpp:170-192）。有効条件は「`CM_MODULE_CODEGEN=1` かつ nativeターゲット かつ 実行形式出力 かつ サニタイザ無効 かつ `--lir-opt` 非指定」で、条件を満たさない場合は従来の全体コンパイルへ自動フォールバックする（llvm.cpp:174-179）。選択時はキャッシュディレクトリ `.tmp/module-cache` を使い、`compileModules` で得たモジュール別 `.o` を `linkObjects` で結合する（llvm.cpp:182-189）。

```
MirProgram → MirSplitter::split_by_module → モジュール別キャッシュキー計算
  → ヒット: 既存 <モジュール>-<ハッシュ>.o を再利用
  → ミス:   独立LLVMContextでワーカ並列に IR変換→検証→最適化→.o出力
→ linkObjects で全 .o + ランタイムライブラリをシステムリンカで結合
```

## データ構造とアルゴリズム

### モジュール分割（MirSplitter / ModuleProgram）

`MirSplitter::split_by_module`（src/internal/mir/mir_splitter.cpp:135）はMIRプログラムをソースファイル由来のモジュール名ごとに分割する。分割結果 `ModuleProgram`（src/internal/mir/mir_splitter.hpp:16-45）はゼロコピーのビューで、自モジュールの関数・構造体・enumと、他モジュール定義要素のextern宣言用リスト、全モジュール共通のインターフェイス・vtableを元プログラムへのポインタで保持する。グローバル変数は「定義を持つ所有モジュール」と「extern宣言のみ生成するモジュール」を分離し、定義の重複による状態分裂を防ぐ（mir_splitter.hpp:34-38）。`origin` は分割元 `MirProgram` への参照で、vtable等プログラム全体のメタデータ参照に使う（mir_splitter.hpp:43-44）。

### キャッシュキー設計（内容アドレス方式）

`compileModules`（codegen.cpp:160-463）はソースファイルの差分検知に依存せず、モジュールのMIR内容そのものからキーを計算する。ハッシュはFNV-1a 64bitをseed違いで2本連結した128bit相当である（codegen.cpp:122-129, 259-264）。

キーは全モジュール共通部とモジュール固有部の連結で構成する。

- 共通部（codegen.cpp:210-247）: コンパイラ同一性（実行バイナリのパス+サイズ+更新時刻、codegen.cpp:131-154）、ターゲットtriple/CPU/features、最適化レベル・デバッグ情報フラグ、全構造体のフィールドレイアウト、全enumのメンバとタグ値、全グローバル変数の型、全インターフェイス名。構造体レイアウトの変更はGEPオフセットとして全モジュールのコードへ波及するため、共通キーに含めて全体を無効化する（codegen.cpp:210-211）。
- 固有部（codegen.cpp:249-265）: 自モジュール関数のMIR全文と、extern関数のMIR全文。extern関数はシグネチャ宣言に加えbodyも生成される経路があるため、全文を含めて過剰無効化側に倒す。

キャッシュ成果物は `output_dir/<モジュール名>-<ハッシュ>.o` の内容アドレス名で置かれ（codegen.cpp:300-301）、`changed_modules` に指定されたモジュールは内容ハッシュが一致してもキャッシュを使わない（codegen.cpp:170-171, 303）。旧API互換の呼び出し元指定キャッシュ（`cached_objects` マップ）も先に確認する（codegen.cpp:284-297）。

### 並列コード生成ワーカー（CM_CODEGEN_JOBS）

キャッシュミスしたモジュールはジョブキューに積み、ワーカスレッドで並列コンパイルする（codegen.cpp:321-460）。

```cpp
// codegen.cpp:328-336
size_t worker_count = std::thread::hardware_concurrency();
if (const char* jobs_env = std::getenv("CM_CODEGEN_JOBS")) {
    long v = std::strtol(jobs_env, nullptr, 10);
    if (v > 0)
        worker_count = static_cast<size_t>(v);
}
```

各ジョブは独立の `LLVMContext`・`TargetManager`・`IntrinsicsManager`・`MIRToLLVM` を作って共有状態を持たずに変換する（codegen.cpp:352-364）。検証（codegen.cpp:367-374）の後、モジュール単位のPassBuilderパイプラインをO1/O2/O3で実行し、O2以上では `PipelineTuningOptions::MergeFunctions` による同一コード折り畳み（ICF）を分割経路でも有効化する（codegen.cpp:380-408）。ジョブ分配は `std::atomic<size_t>` のfetch_addによるワークスティーリング風の単純キューで、ワーカ内例外は `exception_ptr` に退避してjoin後に再送出する（codegen.cpp:339-340, 429-459）。結果は `ModuleObjectFile`（モジュール名・`.o` パス・`from_cache` フラグ、src/internal/codegen/llvm/native/codegen.hpp:96-100）としてリンク段へ渡る。

`.o` の出力は一時名（`.tmpXXXX`）へ書いてから `rename` で原子的に置き、並走する別プロセスが同一キーを書く場合の衝突を防ぐ（codegen.cpp:419-426）。

### リンク段での成果物結合

`linkObjects`（codegen.cpp:466-666）は全 `.o` とCmランタイムをシステムリンカ（macOSは `/usr/bin/clang++`、他は `clang`、wasmは `wasm-ld` 等）で結合する。分割経路では `this->context` が空のリンク用モジュールになりLLVM宣言ベースのライブラリ要否判定が機能しないため、MIRの呼び出し関数名接頭辞（`gpu_`・`cm_tcp_`・`cm_mutex_`・`cm_thread_`・`cm_http_` 等）を `MirSplitter::collect_called_functions` で走査してnet/sync/thread/GPU/HTTPランタイムのリンク要否を判定する（codegen.cpp:474-508、codegen.hpp:110-114）。リンクが10秒を超えた場合は「cmはシステムリンカ待ちである」旨の切り分け警告を出す（codegen.cpp:652-662）。

### コンパイル時間の超線形化を防ぐ設計

コード生成・最適化コストは関数数に対して超線形に増えやすく、複数の層で抑え込む。

- **モジュール分割**: 最適化とISelのコストをモジュール単位に分割し、変更のないモジュールはキャッシュヒットで丸ごとスキップする。フロントエンド（パース〜MIR）は毎回実行し、超線形なコード生成・最適化だけを省く内容アドレス方式である。
- **ICF（MergeFunctions）**: モノモーフ化がレイアウト同一の特殊化（`pick__int`/`pick__uint` 等）を別関数として複製するため、O2以上で証明可能に同一なIR本体を正準実装へのサンクに折り畳み、生成物サイズと後段コンパイル時間の乗算的膨張を抑える（全体経路 codegen.cpp:834-841、分割経路 codegen.cpp:385-387）。名前ベースのフロント側エイリアス化は符号性が比較・除算・拡張の意味論を変えるため不採用で、IR構造比較のみで折り畳む。
- **最適化レベルの自動調整**: MIRパターン検出（codegen.cpp:45-54）とLLVM IRパターン検出（codegen.cpp:74-86, 773-788）が、爆発しやすいパターンを検出した場合に最適化レベルを引き下げる。
- **タイムアウトのプロセス分離**: `.o` 出力はPOSIXではfork子プロセスで行い、タイムアウト（既定30秒、`CM_CODEGEN_TIMEOUT` で上書き可）や出力サイズ超過（100MB）時はSIGKILLで計算資源ごと回収する（src/internal/codegen/llvm/native/safe_codegen.hpp:36-37, 50-59, 69-139）。スレッドdetach方式が抱えていた「GB級メモリを保持したまま残留し後続ビルドを劣化させる」「破棄済みスタックへのuse-after-free」というバグのクラスをプロセス境界で根絶する（safe_codegen.hpp:62-68）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/cmd/cm/backend/llvm.cpp:170-192 | `CM_MODULE_CODEGEN=1` ゲートと分割経路/全体経路の選択、`.tmp/module-cache` の指定 |
| src/internal/codegen/llvm/native/codegen.cpp:35-105 | 既定の全体コンパイル経路（compile） |
| src/internal/codegen/llvm/native/codegen.cpp:122-154 | FNV-1aハッシュとコンパイラ同一性 |
| src/internal/codegen/llvm/native/codegen.cpp:160-463 | compileModules（キャッシュ判定+並列コンパイル） |
| src/internal/codegen/llvm/native/codegen.cpp:466-666 | linkObjects（成果物結合とランタイムライブラリ判定） |
| src/internal/codegen/llvm/native/codegen.hpp:96-114 | `ModuleObjectFile`・compileModules/linkObjectsの宣言 |
| src/internal/mir/mir_splitter.hpp / mir_splitter.cpp | `ModuleProgram`（ゼロコピー分割ビュー）と `split_by_module`・`collect_called_functions` |
| src/internal/codegen/llvm/native/target.cpp:162-177, 182 | emitObjectFile（fork分離のタイムアウト付き）と emitObjectFileDirect（ワーカ用直接版） |
| src/internal/codegen/llvm/native/safe_codegen.hpp | `generateToFileForked`（fork分離・SIGKILL回収）と `CM_CODEGEN_TIMEOUT` |

## 落とし穴とケア

- **キャッシュ無効化漏れが最大のリスク**: 古い `.o` を使う誤ビルドを防ぐため、「生成コードに影響する入力はすべてキーに含める」が不変条件である。コンパイラ自身の更新（compilerIdentity、codegen.cpp:131-133）、型レイアウト（GEPオフセット波及のため共通キー、codegen.cpp:210-211）、extern関数の全文（codegen.cpp:249-250）をキーに混ぜているのはいずれも実際に必要だった無効化条件であり、疑わしい入力は過剰無効化側に倒す。
- **死蔵経路の接続で発覚したバグのクラス**: グローバル変数の全モジュール複製定義による状態分裂（所有/externの分離で防止、mir_splitter.hpp:34-38）、空contextでのランタイムライブラリ要否誤判定（MIRベース判定で防止、codegen.cpp:474-480）、body付きextern関数の重複定義などは、分割経路を変更する際に再発しやすい。経路変更時は [archive設計文書](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md) の8クラスの不具合一覧を確認すること。
- **並列化の不変条件**: LLVMターゲットレジストリの初期化は非スレッドセーフのため、ワーカ起動前にメインスレッドで1回行う（codegen.cpp:321-326）。ワーカスレッドからのforkはmallocロック競合でデッドロックしうるため、モジュール `.o` の出力はfork分離なしの `emitObjectFileDirect` を使う（codegen.cpp:419-425）。モジュール間で `LLVMContext` を共有しないことが並列安全性の前提である。
- **同一キャッシュへの並行書き込み**: `.o` は内容アドレス名のため並列コンパイル間で共有しても衝突しないが、書き込み自体は一時名+`rename` の原子的置換を維持する必要がある（codegen.cpp:420-426）。直接書き込みに変えると並走プロセスが壊れた `.o` を読むレースになる。
- **UEFIターゲットの最適化無効化**: LLVM最適化・CodeGen最適化が `efi_main` のcall/ret命令を削除しフォールスルークラッシュを起こすため、分割経路でも `optLevel = 0` とパススキップを維持する（codegen.cpp:197-203, 377-379）。
- **フォールバック条件の維持**: サニタイザ有効時・非native・非実行形式出力は分割経路が未対応のため全体コンパイルへフォールバックする（llvm.cpp:171-179）。分割経路の対応範囲を広げる際はこのゲート条件と同期して更新する。
- **回帰テストの場所**: 分割経路・ICF・キャッシュは挙動不変が合格条件で、全バックエンドスイート（`make test`、Makefile:527）で確認する。ICFの正当性（レイアウト同一特殊化が折り畳まれても値が壊れないこと）は tests/common/generics/layout_equal_specialization_test.cm が全バックエンドで検証する。タイムアウト経路は `CM_CODEGEN_TIMEOUT` を小さくすることでE2E検証できる。

## 関連資料

- [インクリメンタルビルド・並列コード生成・ICF・タイムアウトのプロセス分離（設計文書）](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md)
- [大規模ボトルネック監査](../../archive/v0.17.0/large-scale-bottleneck-audit.md)
- [JITエンジン（cm runの実行系）](../codegen-jit/lljit-engine.md)
