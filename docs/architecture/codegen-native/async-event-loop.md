---
title: 非同期実行機構（async/await構文と非同期ランタイム）
---

# 非同期実行機構（async/await構文と非同期ランタイム）

Cmは `async` 関数と `await` 式を言語構文として持つが、非同期の実行意味論を実装しているのはJSバックエンドのみであり、native/jitターゲットではMIR検証段階で明示エラーとして拒否される。一方でnativeランタイム側にはFuture・エグゼキュータ・イベントループのC実装（runtime_async.c / runtime_event_loop.c）が存在するが、これらはコアランタイム `cm_runtime.o` のビルド対象に含まれておらず、native/jitのコード生成もこれらのAPI呼び出しを一切生成しない準備実装である。現状のnative/jitにおける実際の並行処理は、pthreadをバッキングとする `native::thread`・`native::sync` ランタイムが担う。

## 概要

async/awaitはフロントエンド全段を素通りするフラグとして実装されている。レクサが `async`/`await` をキーワード化し（src/internal/syntax/lexer/lexer.cpp:74, 78）、パーサは関数修飾子 `async` を `FunctionDecl::is_async` に（src/internal/syntax/parser/parser_decl.cpp:185, 430）、前置式 `await expr` を `AwaitExpr` ノードに載せる（src/internal/syntax/parser/expr/binary.cpp:393-399、src/internal/syntax/ast/expr.hpp:563-566）。HIR loweringは `AwaitExpr` のオペランドが関数呼び出しなら `HirCall::is_awaited` を立てて式としては透過し（src/internal/hir/lowering/expr.cpp:335-344）、MIR loweringがそれを `MirTerminator::CallData::is_awaited` へ引き継ぐ（src/internal/mir/lowering/expr_call.cpp:336-346、src/internal/mir/nodes.hpp:363-364）。つまりHIR/MIRに専用の非同期表現（ステートマシン化やCPS変換）は存在せず、「この呼び出しはawaitされている」というマーカーだけが伝播する。

このマーカーの消費はターゲットごとに分かれる。JSバックエンドは `is_async` を `async function` キーワードと `Promise<T>` 戻り値注釈に、`is_awaited` を `await` 演算子にそのまま写像し（src/internal/codegen/js/emit_function.cpp:166-199、src/internal/codegen/js/emit_statements.cpp:560-563）、async mainは即時実行async関数式で包む（src/internal/codegen/js/codegen.cpp:273-286）。SVバックエンドは `async` を非同期プロセス（ノンブロッキング代入・エッジセンシティブ判定）の指示として別の意味で再利用する（src/internal/codegen/sv/emit_control.cpp:428、src/internal/codegen/sv/analyze.cpp:1429）。native/jit/wasmを含むそれ以外のターゲットでは、ドライバがMIR全関数を走査して `is_async`/`is_awaited` を検出し、検出時は「async/awaitはJSターゲット専用」というエラーで停止する（src/cmd/cm/build.cpp:554-601）。

## データ構造とアルゴリズム

### native/jitターゲットでの拒否

MIR loweringまでは全ターゲット共通で通し、コード生成分岐直前の一箇所でバリデーションする。関数名付きのエラーを出すためにMIRを走査する設計であり、パーサで拒否しないのは同一ソースを `--target=js` では受理する必要があるためである。

```cpp
// src/cmd/cm/build.cpp:568-585（要約）
for (const auto& func : mir.functions) {
    if (func->is_async) { has_async = true; async_func_name = func->name; }
    for (const auto& block : func->basic_blocks) {
        ...
        if (data.is_awaited) { has_await = true; await_func_name = func->name; }
    }
}
```

`cm run`（JIT）はターゲット未指定でこの検査経路に入るため、JITでもasync/awaitはコンパイルエラーになる。

### 非同期ランタイムのデータ構造（未リンクの準備実装）

runtime_async.hはRustのFuture/Waker/Executorモデルを簡略化したポーリングベースの構造を定義する。

```c
// src/internal/codegen/llvm/native/runtime_async.h:25-31
typedef struct CmFuture {
    void* state;                                   // ステートマシン状態
    CmPollState (*poll)(struct CmFuture*, void*);  // ポーリング関数
    void (*drop)(struct CmFuture*);                // デストラクタ
    void* result;                                  // 結果値へのポインタ
    size_t result_size;                            // 結果値のサイズ
} CmFuture;
```

`CmExecutor` はタスク（`CmTask`）の単方向リンクドリストを保持するシングルスレッドエグゼキュータで（runtime_async.h:57-71）、`cm_spawn` はリスト先頭への挿入（runtime_async.c:112-124）、`cm_run_until_complete` は全タスクを完了までラウンドロビンでポーリングする（runtime_async.c:130-168）。`cm_block_on` は単一Futureを `CM_POLL_READY` までビジーループでポーリングする同期実行プリミティブである（runtime_async.c:54-81）。`CmWaker` 構造体は定義されているが、実装上はゼロ初期化のダミーが渡されるだけで、PENDINGなFutureの再スケジュール通知は機能していない。グローバルエグゼキュータは `__attribute__((constructor))` でプロセス起動時に生成される（runtime_async.c:239-248）。

### イベントループ（kqueue / epoll / pollの三段選択）

runtime_event_loop.hはプラットフォームごとのI/O多重化APIをコンパイル時に選択する。

```c
// src/internal/codegen/llvm/native/runtime_event_loop.h:14-24
#ifdef __APPLE__
#include <sys/event.h>  // kqueue
#define CM_USE_KQUEUE 1
#elif defined(__linux__)
#include <sys/epoll.h>  // epoll
#define CM_USE_EPOLL 1
#else
#include <poll.h>
#define CM_USE_POLL 1
#endif
```

`CmEventLoop` はkqueue fd・epoll fd・pollfd配列のいずれかと、発火イベントを詰め替える `pending_events` バッファを持つ（runtime_event_loop.h:49-63）。`cm_event_loop_run` は「エグゼキュータの全タスクを1周ポーリング→未完了タスクが残っていればI/Oイベントを10msタイムアウトで待機」を繰り返すハイブリッド方式で、タイマーFutureのようなfdを持たない待機と、fd登録型のI/O待機を1つのループで両立させる構造になっている（runtime_event_loop.c:266-303）。時刻源はmacOSでは `mach_absolute_time`、それ以外は `CLOCK_MONOTONIC` のモノトニック時計で統一する（runtime_event_loop.c:26-39）。組み込みFutureとして即値完了の `cm_ready_future_i64`（runtime_async.c:202-222）と満了時刻比較方式の `cm_sleep_ms`（runtime_event_loop.c:340-361）が用意されている。

### リンク実態: コアランタイムに含まれない

nativeコード生成がリンクする `cm_runtime.o` は runtime.c を単一翻訳単位としてビルドされるが、そのinclude連結に runtime_async.c と runtime_event_loop.c は含まれていない。

```c
// src/internal/codegen/llvm/native/runtime.c:18-25
#include "../../common/runtime_alloc.c"
#include "../../common/runtime_file.c"
#include "runtime_asm.c"
#include "runtime_format.c"
#include "runtime_io.c"
#include "runtime_platform.c"
#include "runtime_print.c"
#include "runtime_slice.c"
```

またnative/jitのコード生成（MIR→LLVM IR変換およびランタイム関数宣言の登録）には `cm_block_on`・`cm_spawn`・`CmFuture` 等への参照が存在しない。したがって非同期ランタイムは「コンパイラが呼び出しを生成せず、リンクもされない」状態のC実装であり、async/awaitをnativeで実行可能にする際のランタイム側の下地として保守されている。Cm側のラッパー `libs/std/core/async/mod.cm` も、Future型がCm側で表現できないため実際にexportしているのはモノトニック時刻の `now_ms()`（extern `cm_now_ms`）とlibc `usleep` によるブロッキングsleep、経過時間計測の `Timer` のみである。

### native::thread / native::sync ランタイムとの関係

native/jitで現実に使える並行処理は、非同期エグゼキュータとは独立したpthreadバッキングのスレッドAPIである。`libs/native/thread/mod.cm` が `cm_thread_create`/`cm_thread_join`/`cm_thread_detach` 等をextern宣言してspawn/join/detach/sleep_msとしてexportし、実装は `libs/native/thread/thread_runtime.cpp` のpthreadラッパーが提供する。同様に `libs/native/sync` がMutex・RwLock・atomic・Channel（`cm_mutex_*`・`cm_channel_*` 等）を提供する。コード生成側はこれらのシンボルの型シグネチャを組み込み宣言として登録し（src/internal/codegen/llvm/core/runtime/system.cpp:267-303）、AOTリンク時はLLVMモジュール内の呼び出し名を接頭辞（`cm_thread_`・`cm_mutex_`・`cm_channel_` 等）で走査して `needsThread`/`needsSync` を立て、必要な `cm_thread_runtime.o`/`cm_sync_runtime.a` と `-lpthread` だけをリンクコマンドへ追加する（src/internal/codegen/llvm/native/codegen.cpp:478-510, 556-566、探索は findStdRuntimeLibrary、codegen.cpp:1520-1560）。JIT実行ではこれらの実装ソースがcmバイナリ自体にコンパイルされているため（CMakeLists.txt:487-491）、ホストプロセスからのシンボル解決で追加リンクなしに動作する。つまり「タスクの並行実行はOSスレッド、スレッド間通信はチャネル・Mutex」がnative/jitの並行モデルであり、シングルスレッドのFutureエグゼキュータはこのモデルと接続されていない。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/syntax/lexer/lexer.cpp:74, 78 | `async`/`await` のキーワード登録 |
| src/internal/syntax/parser/parser_decl.cpp:185, 430 | 関数修飾子 `async` のパースと `FunctionDecl::is_async` 設定 |
| src/internal/syntax/parser/expr/binary.cpp:393-399 | 前置式 `await expr` のパース（`AwaitExpr` 生成） |
| src/internal/hir/lowering/expr.cpp:335-344 | `AwaitExpr` → `HirCall::is_awaited` フラグ化 |
| src/internal/mir/lowering/expr_call.cpp:336-346 | `is_awaited` の `MirTerminator::CallData` への伝播 |
| src/cmd/cm/build.cpp:554-601 | 非JS/SVターゲットでのasync/await拒否バリデーション |
| src/internal/codegen/js/emit_function.cpp, emit_statements.cpp | `async function`・`await`・`Promise<T>` へのJS lowering（境界のみ） |
| src/internal/codegen/llvm/native/runtime_async.{h,c} | Future・Waker・Task・Executorと `cm_block_on`/`cm_spawn`（未リンク） |
| src/internal/codegen/llvm/native/runtime_event_loop.{h,c} | kqueue/epoll/pollイベントループ・タイマーFuture・`cm_now_ms`（未リンク） |
| src/internal/codegen/llvm/native/runtime.c:18-25 | コアランタイムのinclude連結（async系を含まないことの根拠） |
| libs/std/core/async/mod.cm | Cm側ラッパー（now_ms・ブロッキングsleep・Timerのみ） |
| libs/native/thread/, libs/native/sync/ | pthreadバッキングのスレッド・同期・チャネル実装 |
| src/internal/codegen/llvm/core/runtime/system.cpp:267-303 | `cm_thread_*` 等のコード生成用シグネチャ登録 |
| src/internal/codegen/llvm/native/codegen.cpp:478-510 | 呼び出しシンボル接頭辞走査によるランタイムライブラリ自動リンク判定 |

## 落とし穴とケア

- **async/awaitをnativeで「動くはず」と誤解しない**: 構文・AST・HIR・MIRまで全段が受理するため一見サポート済みに見えるが、実行意味論を持つのはJSバックエンドだけである。native/jit/wasmではMIR検証がエラーにするので、非同期処理が必要なら `native::thread`+`native::sync` へ設計を寄せるか `--target=js` を使う。
- **runtime_async.c / runtime_event_loop.c はリンクされていない**: これらに関数を追加してもcm_runtime.oには入らず、externしたCmコードはJITのシンボルlookupまたはAOTリンクで未解決になる。実際に `libs/std/core/async/mod.cm` の `now_ms()` が依存する `cm_now_ms` はruntime_event_loop.cにしか定義がなく、リンク経路が存在しない。async実行機構をnativeへ導入する際は、runtime.cのinclude連結とCMakeのDEPENDS（CMakeLists.txt:606-624）への追加が前提条件になる。
- **`cm_block_on` はビジーウェイトである**: WakerがダミーのためPENDINGのFutureを待つ手段がポーリング再試行しかなく、そのままリンクして使うとCPUを100%消費する。イベントループ統合（`cm_event_loop_run` の10msタイムアウト待機）かWakerの実装が先に必要で、これがこのランタイムを既定リンクに含めていない理由でもある。
- **SVターゲットの `async` は別物**: 同じ `is_async` フラグをSVバックエンドはノンブロッキング代入・エッジセンシティブなプロセス生成の指示として解釈する。このためasync拒否バリデーションはSVターゲットを除外しており、SV向けコードをnativeへ流用すると同じ構文が今度はエラーになる。
- **エグゼキュータのタスク順序はLIFO**: `cm_spawn` はリンクドリスト先頭への挿入のため、実行順はspawn順の逆になる。順序に依存するテストや設計をこの上に載せないこと。
- **スレッドランタイムのリンクは接頭辞走査に依存**: `cm_thread_*` 等の命名規約から必要ライブラリを自動判定するため、この接頭辞に従わない関数をthread/syncランタイムへ追加するとAOTでリンク漏れになる。詳細は[リンクとランタイム解決](linking-and-runtime.md)を参照。

## 関連資料

- [async/await設計（未実装アーカイブ）](../../archive/unimplemented/async.md)
- [リンクとランタイム解決](linking-and-runtime.md)
- [JITエンジン（LLVM ORC / LLJIT）](../codegen-jit/lljit-engine.md)
- [コンパイルパイプライン全体像](../pipeline/overview.md)
- [スレッドチュートリアル](../../tutorials/ja/advanced/thread.md)・[チャネルチュートリアル](../../tutorials/ja/stdlib/concurrency/channel.md)
