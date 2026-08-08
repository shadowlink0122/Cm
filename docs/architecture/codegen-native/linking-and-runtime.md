# リンクとランタイム解決

実行可能ファイル生成は、オブジェクトファイルをプラットフォーム別の外部リンカ（clang++/clang/wasm-ld/lld-link/arm-none-eabi-ld）へ渡すコマンド文字列の組み立てとして実装されている。プログラムが使うランタイム機能は `cm_*` シンボルプレフィックスの走査で自動検出し、必要なランタイムライブラリ（net/sync/thread/http/gpu）だけをリンク行へ追加する。中核ランタイム `cm_runtime.o` は複数パスの探索とオンデマンドコンパイルで必ず解決される。

## 概要

リンク経路は2系統ある。単一モジュールコンパイルの `LLVMCodeGen::emitExecutable()`（`src/internal/codegen/llvm/native/codegen.cpp:1066-1267`）と、モジュール分割コンパイルで複数.oをまとめる `LLVMCodeGen::linkObjects()`（`codegen.cpp:466-666`）で、どちらも同じ判定ロジックでリンクコマンドを構築し `std::system` で実行する。ランタイムのC実体は `src/internal/codegen/llvm/native/runtime.c` を傘ファイルとする分割Cソース群にあり、宣言契約は `src/internal/codegen/common/runtime_common.h` で全バックエンドと共有する。

## データ構造とアルゴリズム

### プラットフォーム別リンカコマンド

ターゲットごとに使うリンカとフラグが分岐する（`codegen.cpp:521-529`・`1083-1091`）。

| ターゲット | リンカコマンドの骨格 |
|---|---|
| Baremetal (ARM) | `arm-none-eabi-ld -T link.ld <objs> -o <out>` |
| BaremetalUEFI | `lld-link /subsystem:efi_application /entry:efi_main /out:<out> <objs>` |
| Wasm | `wasm-ld --entry=_start --allow-undefined -z stack-size=1048576 <objs> <runtime> -o <out>` |
| Native (macOS) | `/usr/bin/clang++ -mmacosx-version-min=15.0 -Wl,-dead_strip [-arch <arch>] <objs> <runtime> ... -o <out>` |
| Native (Linux) | `clang -Wl,--gc-sections <objs> <runtime> ... -o <out>` |

macOSでは `CM_DEFAULT_TARGET_ARCH` により `-arch` を明示してクロスリンクに対応し、`noStd` 構成では `-nostdlib` を付ける（`codegen.cpp:536-541`）。サニタイザ有効時はApple CLTのclang++ではなくHomebrew LLVMのclang++を新しい順に探索してリンクドライバに使う（`findSanitizerLinkDriver` `codegen.cpp:927-940`、理由はLLVM計装のバージョン記号 `__asan_version_mismatch_check_v8` 等をCLTランタイムが持たないため、`codegen.cpp:1100-1105`）。

リンカは外部プロセスなので、実行時間が10秒を超えた場合は「cmはシステムリンカを待っているだけであり、セキュリティソフトの新規バイナリスキャン等が原因になりうる」旨の切り分け警告を出す（`codegen.cpp:657-662`・`1255-1260`）。

### cm_*プレフィックスによるランタイム自動検出

リンクすべきランタイムライブラリは、モジュール内の未定義宣言（またはMIRの呼び出し関数名）のプレフィックス走査で決める。単一モジュール経路はLLVMモジュールの宣言を走査する `checkFor*Usage`（`codegen.cpp:1412-1489`）を使い、モジュール分割経路はMIRの呼び出し関数名を直接走査する（contextにLLVM宣言が揃わないため、`codegen.cpp:474-501`）。

```cpp
// codegen.cpp:488-499 — MIR呼び出し名のプレフィックス判定
needsGPU = needsGPU || has_prefix(called, "gpu_");
needsNet = needsNet || has_prefix(called, "cm_tcp_") ||
           has_prefix(called, "cm_udp_") || has_prefix(called, "cm_dns_") ||
           has_prefix(called, "cm_socket_");
needsSync =
    needsSync || has_prefix(called, "cm_mutex_") ||
    has_prefix(called, "cm_rwlock_") || has_prefix(called, "cm_atomic_") ||
    has_prefix(called, "cm_channel_") || has_prefix(called, "cm_once_") || ...
needsThread = needsThread || has_prefix(called, "cm_thread_");
needsHTTP = needsHTTP || has_prefix(called, "cm_http_");
```

検出結果に応じてリンク行へ追加されるもの（macOSの例、`codegen.cpp:544-606`）:

- GPU: `cm_gpu_runtime.o` + `-framework Metal -framework Foundation`
- net/sync/thread/http: `findStdRuntimeLibrary("net"|"sync"|"thread"|"http")` の各.o/.a
- http: さらにOpenSSLを探索して `-L<prefix>/lib -lssl -lcrypto`（Homebrew既知プレフィックス→`brew --prefix openssl@3` の順、`codegen.cpp:570-601`）
- sync/threadのいずれかで `-lpthread`、いずれかのC++実装ランタイムがあれば `-lc++`（Linuxは `-lstdc++`）（`codegen.cpp:509-510`・`603-606`・`641-644`）

この方式により、ネットワークもスレッドも使わないプログラムのリンク行は `cm_runtime.o` だけになり、余計なライブラリ依存やフレームワークリンクが混入しない。

### cm_runtime.oの探索順

中核ランタイムは `findRuntimeLibrary()`（`codegen.cpp:1270-1310`）が次の順で解決する。

1. ビルド時マクロ `CM_RUNTIME_PATH` が指すパス（存在すれば最優先、`codegen.cpp:1283-1287`）
2. `build/lib/cm_runtime.o`・`./build/lib/`・`../build/lib/`・`.tmp/cm_runtime.o`（リポジトリ内ビルド向け、`codegen.cpp:1294-1299`）
3. `$HOME/.cm/lib/cm_runtime.o`（`make install` 済み環境、`codegen.cpp:1289-1301`）
4. どれも無ければ `compileRuntimeOnDemand()`（`codegen.cpp:1313-1346`）がソース `src/internal/codegen/llvm/native/runtime.c` を探して `clang -c ... -O2` で `build/lib/cm_runtime.o` を生成する

Wasmターゲットは同様に `CM_RUNTIME_WASM_PATH`→`build/lib/cm_runtime_wasm.o`→オンデマンドコンパイルの順で、オンデマンド時はHomebrew LLVMのclangを探して `--target=wasm32-wasi -nostdlib` でビルドする（`codegen.cpp:1271-1281`・`1349-1409`）。net/sync/thread/http/gpuの各ランタイムも同型の探索（`CM_*_RUNTIME_PATH` マクロ→`build/lib`→`~/.cm/lib`）を持つ（`findStdRuntimeLibrary` `codegen.cpp:1520-1565`、`findGPURuntimeLibrary` `codegen.cpp:1492-1517`）。syncのみ拡張子が `.a` で他は `.o` である（`codegen.cpp:1542-1543`）。

### ランタイムCシムの構成

`cm_runtime.o` の実体は傘ファイル `runtime.c` が分割Cソースを1コンパイル単位に束ねたものである。

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

| ソース | 内容 |
|---|---|
| `common/runtime_alloc.c` | メモリアロケータ抽象（バックエンド共通） |
| `common/runtime_file.c` | ファイルI/O・stdin入力（バックエンド共通） |
| `native/runtime_platform.c` | プラットフォーム固有の低レベル出力 |
| `native/runtime_print.c` | `cm_print_*`/`cm_println_*` 出力関数 |
| `native/runtime_format.c` | `cm_format_*`/`cm_*_to_string` 書式化と長さヘッダ付き文字列 |
| `native/runtime_slice.c` | スライス（動的配列）操作 `cm_slice_*` |
| `native/runtime_io.c` | POSIX I/Oラッパ `cm_io_*` |
| `native/runtime_asm.c` | ASM連携補助 |

このほかasync/イベントループ（`native/runtime_async.c`・`runtime_event_loop.c`）があり、net/sync/thread/http/gpuの各ランタイムは別ライブラリとしてビルドされ前述の検出で条件リンクされる。関数宣言の契約は `common/runtime_common.h` にあり、LLVMコア側は `runtime/builtins.cpp`・`runtime/system.cpp` が同じシグネチャで宣言だけを生成し、実体はリンク時にこれらの.oで解決される。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/native/codegen.cpp` | emitExecutable/linkObjectsのリンクコマンド組み立て、`cm_*` 検出、ランタイム探索・オンデマンドコンパイル |
| `src/internal/codegen/llvm/native/codegen.hpp` | LLVMCodeGenのオプション（sanitize系・OutputFormat）と探索APIの宣言 |
| `src/internal/codegen/llvm/native/runtime.c` | ランタイム傘ファイル（分割Cソースを単一コンパイル単位へ結合） |
| `src/internal/codegen/llvm/native/runtime_*.c` | ネイティブ向けランタイム実体（format/print/slice/io/platform/asm/async/event_loop） |
| `src/internal/codegen/common/runtime_common.h` | LLVM/WASM共有のランタイム関数宣言契約 |
| `src/internal/codegen/common/runtime_alloc.c` / `runtime_file.c` | バックエンド共通のアロケータ・ファイルI/O実体 |
| `src/internal/codegen/llvm/core/runtime/builtins.cpp` / `system.cpp` | LLVM IR側のランタイム関数宣言生成（実体はリンクで解決） |
| `src/internal/codegen/llvm/wasm/runtime_*.c` | Wasm向けランタイム実体（`wasm-ld` でリンクされる別実装） |

## 落とし穴とケア

- 新しいランタイム機能群を追加するときは、シンボルプレフィックスの検出条件を「LLVM宣言走査（`checkFor*Usage`）」と「MIR呼び出し名走査（`linkObjects` 内）」の両方へ追加する必要がある（`codegen.cpp:480-508`）。片方だけだとモジュール分割ビルドまたは単一ビルドの一方で未解決シンボルのリンクエラーになる。
- プレフィックス集合とランタイムライブラリの対応（`cm_tcp_*`→net等）が実際のCソースの関数命名と一致していることが不変条件である。ランタイム関数の改名はこの検出を静かに壊す。
- `cm_runtime.o` の探索は相対パス（`build/lib` 等）を含むため、カレントディレクトリに依存する。インストール環境では `CM_RUNTIME_PATH`（ビルド時埋め込み）と `~/.cm/lib` が効くので、`make install` 後の反映漏れに注意する。
- オンデマンドコンパイルは開発リポジトリ内でのみ機能する救済手段であり（ソース相対パス探索、`codegen.cpp:1314-1318`）、失敗時は「コンパイラを再ビルドせよ」というエラーになる。配布物ではランタイム.oの同梱が前提である。
- リンクコマンドは `std::system` へ渡す単一文字列であり、出力パス等に空白を含むケースの引用処理はリンカ呼び出し全体で一貫させる必要がある。
- リンカ長時間化の警告閾値（10秒）はcm自身の処理時間と外部要因を切り分けるためのもので、タイムアウトではない（リンクは中断しない、`codegen.cpp:1253-1260`）。
- 回帰テスト: net/sync/thread/http/gpuを使うサンプルを含むバックエンドスイート（`tests/common/` を `make test-llvm` 系で実行）が、検出→条件リンク→実行の全経路を検証する。

## 関連資料

- [オブジェクトファイル出力](object-emission.md)
- [MIR→LLVM IR変換の構造](mir-to-llvm.md)
- [数値出力とキャストの一貫性](numeric-and-casts.md)（runtime_format.cの書式化実装）
- アロケータと一時プールの設計: [../../archive/v0.17.0/memory/allocator-and-temp-pool.md](../../archive/v0.17.0/memory/allocator-and-temp-pool.md)
