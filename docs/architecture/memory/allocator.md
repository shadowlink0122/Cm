# アロケータ設計（native/jit）

Cmランタイムのヒープ確保・解放は、関数ポインタテーブル`CmAllocator`を経由する`cm_alloc`/`cm_dealloc`/`cm_realloc`の単一経路に一本化されており、native/jitの既定実装はlibcの`malloc`/`free`/`realloc`への薄い委譲である。
Cmコードからは`std::mem`の`set_allocator_fns`ファサードでグローバルアロケータを差し替えられ、差し替えは`std::mem`のalloc/deallocだけでなくスライス・文字列などランタイム内部の確保にも一様に反映される。

## 概要

アロケータ抽象の中心は`CmAllocator`構造体（`src/internal/codegen/common/runtime_alloc.h:29`）で、alloc/dealloc/reallocの関数ポインタ3本とuser_dataを持つ。
グローバル状態は`runtime_alloc.c`内の`cm_current_allocator`1本で、初期値はlibc委譲の既定インスタンスを指す（`src/internal/codegen/common/runtime_alloc.c:35`・`:41`）。

確保・解放のAPIは2層ある。

- ランタイムC内部用: `cm_alloc`/`cm_dealloc`/`cm_realloc`は`static inline`で現在のグローバルアロケータを経由する（`src/internal/codegen/common/runtime_alloc.h:78`〜`:92`）
- Cm FFI用: `cm_mem_alloc`/`cm_mem_dealloc`/`cm_mem_realloc`は非inlineのエクスポート実体で、inline版が外部シンボルを持たないためにCmの`extern "C"`宣言から到達できない問題を回避する（`src/internal/codegen/common/runtime_alloc.c:67`〜`:79`）

差し替えは`cm_set_allocator_fns`（`src/internal/codegen/common/runtime_alloc.c:81`）がCm関数ポインタ3本から`CmAllocator`を構成してグローバルへ登録し、`cm_reset_allocator`（`:92`）が既定へ戻す。
Cm側の関数シグネチャ（`void* f(long)` / `void f(void*)` / `void* f(void*, long)`）はLP64のC ABIと互換のため、Cm関数をそのまま関数ポインタとして渡せる。
いずれかがNULLなら何もしない防御があり、部分的な差し替え（allocだけカスタムでdeallocが既定のまま）による確保・解放のペア不一致を構造的に作れないようにしている。

## データ構造とアルゴリズム

### Cmからのファサード（std::mem）

`libs/std/mem/mod.cm`が唯一のCm向け入口で、`cm_mem_*`と差し替えAPIをFFI宣言し（`libs/std/mem/mod.cm:35`〜`:39`）、`alloc`/`dealloc`（`:95`〜`:101`）・`DefaultAllocator`（`:64`〜`:88`）・`set_allocator_fns`/`reset_allocator`（`:111`〜`:118`）をエクスポートする。

```cm
// libs/std/mem/mod.cm:111
export void set_allocator_fns(void* alloc_fn, void* dealloc_fn, void* realloc_fn) {
    cm_set_allocator_fns(alloc_fn, dealloc_fn, realloc_fn);
}
```

`DefaultAllocator`の全メソッドも`cm_mem_*`経由であり、`alloc_zeroed`はゼロ埋めのみ手動で行って確保はcm_alloc経路を通す（`libs/std/mem/mod.cm:77`〜`:87`）。
libcの`malloc`/`free`は生のlibc APIとして`use libc`ブロック（`:23`〜`:28`）に併存するが、これは明示的にlibcを使いたいFFI用途であり、標準経路は`cm_mem_*`である。

Cmコードからの利用は、C ABI互換シグネチャのCm関数3本を定義して関数ポインタとして渡す形になる。

```cm
import std::mem::{ set_allocator_fns, reset_allocator, alloc, dealloc };

void* my_alloc(long size) { /* 計測して既定確保に委譲する等 */ }
void my_dealloc(void* ptr) { /* ... */ }
void* my_realloc(void* ptr, long new_size) { /* ... */ }

set_allocator_fns(my_alloc as void*, my_dealloc as void*, my_realloc as void*);
// 以降のstd::mem::alloc・Vector・ランタイム内部確保が登録アロケータを経由する
reset_allocator();  // 既定（libc委譲）へ戻す
```

### ランタイム内部の消費者

ランタイムの動的確保はすべて`cm_alloc`系を経由する。

- スライス: `cm_slice_new`がヘッダとデータ領域を`cm_alloc`で確保し（`src/internal/codegen/llvm/native/runtime_slice.c:31`・`:38`）、`cm_slice_free`が`cm_dealloc`で返却（`:44`）、growは`cm_realloc`（`:122`）
- 文字列: `cm_str_alloc`がヘッダ付き文字列バッファを`cm_alloc`で確保し（`src/internal/codegen/llvm/native/runtime_format.c:48`〜`:52`）、`cm_string_free`がヘッダから確保起点を復元して`cm_dealloc`する（`:2206`）

このため`set_allocator_fns`での差し替えは、ユーザーの明示確保（`std::mem::alloc`）とコンパイラ挿入の一時解放・`Vector`等の内部確保の双方に同時に効く。

### native/jitへの配線

ランタイムは`runtime.c`が全コンポーネントを`#include`する単一コンパイル単位で（`src/internal/codegen/llvm/native/runtime.c`が`../../common/runtime_alloc.c`等を取り込む）、CMakeが`cm_runtime.o`にビルドする（`CMakeLists.txt`の`CM_RUNTIME_SOURCE`/`CM_RUNTIME_OUTPUT`定義部）。

- jit: `cm_runtime.o`は`cm`実行ファイル自体にリンクされ、ORC JITの`DynamicLibrarySearchGenerator::GetForCurrentProcess`（`src/internal/codegen/llvm/jit/jit_engine.cpp:88`）がホストプロセスから`cm_mem_alloc`等のシンボルを解決する。明示的なシンボル登録テーブルは持たない
- native: `findRuntimeLibrary`（`src/internal/codegen/llvm/native/codegen.cpp:1270`）が`CM_RUNTIME_PATH`（ビルド埋め込み）→`~/.cm/lib/cm_runtime.o`（make install）→相対パスの順に`cm_runtime.o`を探し、生成オブジェクトとともにリンクする

つまりnative/jitは同一のランタイムソース・同一の既定アロケータ（libc委譲）を共有し、確保・解放の挙動が分裂しない。
なお、名前に`alloc`/`dealloc`/`reallocate`を含むCm関数にはLLVM関数属性`NoInline`が付与され（`src/internal/codegen/llvm/core/translate/signature.cpp:350`）、カスタムアロケータ関数が最適化でインライン展開後に消えて関数ポインタ登録が壊れることを防ぐ。

### 他バックエンドとの境界

wasmはフリーリスト方式の独自アロケータ固定であり、`cm_set_allocator_fns`はno-opとして定義される（`src/internal/codegen/llvm/wasm/runtime_wasm.c:259`）。
js/tsはGC管理のため差し替え対象外である。
本文書の対象はnative/jitであり、wasmアロケータの詳細は[archive設計文書](../../archive/v0.17.0/allocator-and-temp-pool.md)を参照。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/common/runtime_alloc.h` | `CmAllocator`定義、`cm_alloc`/`cm_dealloc`/`cm_realloc`（static inline）、既定アロケータのマクロ選択（no_std対応） |
| `src/internal/codegen/common/runtime_alloc.c` | 既定実装（malloc/free/realloc委譲）、グローバルアロケータ状態、`cm_mem_*`エクスポート、`cm_set_allocator_fns`/`cm_reset_allocator` |
| `libs/std/mem/mod.cm` | Cm側ファサード（`alloc`/`dealloc`/`DefaultAllocator`/`set_allocator_fns`/`reset_allocator`） |
| `src/internal/codegen/llvm/native/runtime.c` | ランタイム全体のアンブレラ（単一コンパイル単位） |
| `src/internal/codegen/llvm/native/runtime_slice.c` | スライスの確保・解放・grow（すべてcm_alloc系経由） |
| `src/internal/codegen/llvm/native/runtime_format.c` | 文字列バッファの確保（`cm_str_alloc`）と解放（`cm_string_free`） |
| `src/internal/codegen/llvm/jit/jit_engine.cpp` | JITのランタイムシンボル解決（ホストプロセス検索） |
| `src/internal/codegen/llvm/native/codegen.cpp` | nativeリンク時の`cm_runtime.o`探索 |
| `src/internal/codegen/llvm/core/translate/signature.cpp` | アロケータ関数へのNoInline付与 |

## 落とし穴とケア

- **確保・解放経路の一本化が防ぐバグ**: 確保がlibc直呼び・解放がカスタムアロケータ（またはその逆）というペア不一致はヒープ破壊に直結する。ランタイムに新しい確保サイトを書くときは必ず`cm_alloc`/`cm_dealloc`/`cm_realloc`（Cm側なら`cm_mem_*`）を使い、`malloc`/`free`を直接呼ばないこと。かつて`std::mem`がlibcを直呼びしていた時代には、`cm_set_allocator`で差し替えても`std::mem`と`Vector`に反映されないという分裂があった（背景は[archive設計文書](../../archive/v0.17.0/allocator-and-temp-pool.md)）
- **inline APIとFFI実体の二重定義**: `cm_alloc`系はヘッダのstatic inlineで、外部シンボルは`cm_mem_*`のみである。Cmから新しいアロケータ機能を公開する場合はinline版でなく非inlineのエクスポート実体を追加し、JITのプロセス内シンボル解決とnativeリンクの両方で解決可能にする
- **差し替え中の解放の整合**: `set_allocator_fns`は既に確保済みのブロックの出自を追跡しない。カスタムアロケータの登録・解除をまたいで生存するオブジェクトは、確保時と同じ実装で解放される保証がユーザー側の責務になる。計測用のカウンタアロケータのように内部でlibc mallocへ委譲する実装であれば境界をまたいでも安全である
- **NoInline不変条件**: アロケータ関数のNoInline付与を外すと、O2でインライン展開後に元関数が削除され、`set_allocator_fns`へ渡した関数ポインタがダングリングする
- **既定実装は薄く保つ**: 既定アロケータはlibcへの委譲以上のことをしない。ここに計測やプール等を足すと全確保に波及するため、そうした機能は差し替え側で実装する
- **回帰テスト**: 差し替えの到達性は`tests/common/allocator/set_allocator_facade.cm`（カウンタアロケータで`std::mem::alloc`の経由と`reset_allocator`の復帰を観測）、インターフェース経由の確保は`tests/common/allocator/allocator_interface.cm`で固定している

## 関連資料

- [解放可能なwasmアロケータとアロケータ差し替えの到達可能化（archive設計文書）](../../archive/v0.17.0/allocator-and-temp-pool.md)
- [RAII・dropパスと所有権](drop-and-ownership.md) — この確保・解放経路を呼び出す解放挿入の設計
- [集約コピーのlowering](aggregate-copy.md)
