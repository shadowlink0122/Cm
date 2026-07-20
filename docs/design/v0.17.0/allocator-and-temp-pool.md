---
title: 解放可能なwasmアロケータとアロケータ差し替えの到達可能化
parent: v0.17.0 Design
---

# 解放可能なwasmアロケータとアロケータ差し替えの到達可能化

大規模開発ボトルネック監査の所見H11/M14に対する実装設計である。
wasmランタイムのアロケータが解放不可能な単調バンプであるために長時間実行プログラムが総確保量に比例してメモリを増やす問題と、アロケータ抽象化（`cm_set_allocator`）およびテンポラリ文字列プール（`CmTempStringPool`）が実装されているにもかかわらずCmコードからも生成コードからも到達不能なデッドインフラである問題を扱う。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H11 | メモリ | wasmアロケータは解放不可能な単調バンプ（`free`/`realloc`が旧ブロックを放棄）で長時間実行プログラムは総確保量に比例してメモリ増加 | 実装済み（8バイトヘッダ+サイズクラス別フリーリスト。`free`/`cm_free`が実際に返却し、`realloc`は旧サイズを正確にコピーして旧ブロックを解放、スライスgrowも旧ブロックを返却。マジック検証で非ヒープポインタのfreeは無視。解放→再確保で同一ブロックが再利用されることを実証済み） |
| M14 | メモリ | `CmTempStringPool`は完全なデッドインフラ（呼び出しゼロ）、アロケータ差し替え（`cm_set_allocator`）はCmから到達不能で`std::mem`/`Vector`はlibc mallocを直呼び | 一部実装（`CmTempStringPool`はdropパス方針に合わせて撤去済み。`set_allocator`のCmファサードはカスタムアロケータをinterface値として渡す基盤（H1/H2）が未実装のため保留） |

## 背景と根本原因

### H11: wasmアロケータは解放できない単調バンプ

wasmの`wasm_alloc`（`src/internal/codegen/llvm/wasm/runtime_format.c:56`）は64KBの静的プール`memory_pool`から先頭オフセット`pool_offset`を進めて配り、枯渇後は`memory.grow`で線形メモリを拡張して`grown_ptr`から配り続ける純粋なバンプアロケータである。
以前はプール枯渇時に`pool_offset`を0へ巻き戻して生存中の割り当てを上書きしていたバグがあり、それを避けるため巻き戻しを廃止して常に新しい領域を返す方針になった（同ファイル44-49行のコメント）。
その結果、確保は安全だが解放の余地が一切ない。
`free`（`src/internal/codegen/llvm/wasm/runtime_wasm.c:201`）と`cm_free`（同209行）はいずれもno-opで、`realloc`（同225行）は新規確保してコピーするだけで旧ブロックを放棄する。
`cm_slice_free`（`src/internal/codegen/llvm/wasm/runtime_slice.c:40`）は`cm_free`を呼ぶが、それがno-opであるためスライスを解放しても実メモリは返らない。
このため長時間実行やループの多いプログラムは、たとえ論理的には解放していても総確保量に比例して線形メモリが増え続ける。

### M14: アロケータ抽象化とテンポラリプールがデッド

アロケータ抽象化`CmAllocator`とグローバル差し替えAPI`cm_set_allocator`は`src/internal/codegen/common/runtime_alloc.c:51`に実装されており、`cm_alloc`/`cm_dealloc`/`cm_realloc`（`src/internal/codegen/common/runtime_alloc.h:78-92`）は現在のグローバルアロケータを経由する。
nativeのスライス確保`cm_slice_new`（`src/internal/codegen/llvm/native/runtime_slice.c:31`）は`cm_alloc`経由なので差し替え自体は機能しうるが、`cm_set_allocator`を呼び出すCmファサードや組み込み関数が存在せず、Cmコードからアロケータを差し替える手段がない。
さらに標準ライブラリの`std::mem`（`libs/std/mem/mod.cm:23-28`）はlibcの`malloc`/`free`/`realloc`をFFIで直接宣言し、`alloc`/`dealloc`（同76-82行）も`DefaultAllocator`（53-69行）もlibc mallocを直呼びするため、`cm_set_allocator`で差し替えても`std::mem`経由の確保には一切反映されない。
テンポラリ文字列プール`CmTempStringPool`（`runtime_alloc.h:162`）とその操作`cm_temp_alloc`/`cm_temp_release`/`cm_temp_mark`/`cm_temp_release_to`（同171-203行）も定義されているが、`runtime_alloc.h`以外からの呼び出し箇所はゼロで、print文の一時文字列をまとめて解放する当初意図が全く配線されていない。

## 設計方針

1. **解放可能なwasmアロケータ（H11）**: `wasm_alloc`/`free`/`realloc`を、解放したブロックを再利用できるフリーリスト方式（サイズクラス別バケットまたは境界タグ付きの結合可能フリーリスト）に置き換える。
   各割り当ての直前にサイズ（とサイズクラス）を記録するヘッダを持たせ、`free`で該当サイズクラスのフリーリストへ返却、`wasm_alloc`はまずフリーリストを探索してから`memory.grow`にフォールバックする。
   `realloc`は旧ブロックサイズをヘッダから取得して正確にコピーし、旧ブロックを解放する。
   最小構成では固定サイズクラス（8/16/32/…のべき乗バケット）による単純なセグリゲートフリーリストから始め、断片化が問題になれば結合を導入する。
2. **アロケータ差し替えのCmからの到達可能化（M14）**: `cm_set_allocator`/`cm_get_allocator`/`cm_reset_allocator`をCmから呼べる組み込みまたは`std::mem`のファサード関数として公開し、カスタム`CmAllocator`を登録できるようにする。
   同時に`std::mem`のFFI直呼び（libc malloc）を`cm_alloc`/`cm_dealloc`/`cm_realloc`経由に切り替え、差し替えが`std::mem`と`Vector`の双方に効くよう確保経路を一本化する。
3. **テンポラリプールの配線または撤去（M14）**: `CmTempStringPool`を実際にprint/format経路へ配線して一時文字列をまとめ解放するか、dropパス（別文書memory-drop-and-lifetime）で一時解放を統一する方針に合わせてデッドコードとして撤去するかを決める。
   dropパス側で一時オブジェクトのlast-use解放を統一実装する場合、プールは重複インフラになるため撤去を基本方針とする。

## 構文例・出力例

アロケータ差し替えをCmから利用する想定APIの例を示す（現状は到達不能で、公開方法はPhase 2で確定する）。

```cm
import std::mem::{ Allocator, set_allocator, reset_allocator };

// カウンタ付きアロケータで確保量を計測する等の用途
struct CountingAllocator { ... }
impl CountingAllocator for Allocator { ... }

CountingAllocator a = CountingAllocator{};
set_allocator(a);        // 以降の std::mem / Vector 確保が a を経由
// ... 処理 ...
reset_allocator();       // 既定（libc/wasmランタイム）へ戻す
```

H11の効果はメモリ挙動であり構文出力の変化は無い。
スライスを確保・解放するループをwasmで回したとき、従来は反復回数に比例して線形メモリが増加するのに対し、フリーリスト化後は定常メモリが平坦になることを計測で確認する。

## 実装の段階分割

- **Phase 1（H11）**: wasm向けにフリーリスト方式アロケータを実装し、`wasm_alloc`/`free`/`realloc`/`cm_free`を解放可能な実装に置き換える。まずは固定サイズクラスのセグリゲートフリーリストで解放と再利用を成立させる。
- **Phase 2（M14）**: `cm_set_allocator`系をCmから到達可能なファサード（`std::mem`）として公開し、`std::mem`の確保経路をlibc直呼びから`cm_alloc`経由へ一本化する。
- **Phase 3（M14）**: `CmTempStringPool`をdropパス方針に合わせて撤去する（またはprint/format経路へ配線する）。デッドインフラを解消する。
- **Phase 4（H11）**: 断片化計測に基づき、必要なら隣接ブロック結合（coalescing）を導入してフリーリストの実効利用率を高める。

## テスト計画（tests/common/配下）

- **H11回帰**: `tests/common/`にスライス・文字列を確保しては解放するループを回すプログラムを追加し、wasmで定常メモリが反復回数に依存しない（線形増加しない）ことを計測で確認する。確保直後の内容が正しく読めること（フリーリスト再利用でデータ破壊が無いこと）も検証する。
- **M14回帰**: カスタムアロケータを`set_allocator`で登録し、`std::mem`と`Vector`の確保がそのアロケータを経由することを、アロケータ側のカウンタ等で観測するプログラムを追加する。`reset_allocator`で既定に戻ることも確認する。
- **アロケータ健全性**: alloc/free/reallocの往復でヒープ破壊が起きないことを、境界サイズ（サイズクラス境界・0バイト・大サイズでの`memory.grow`）を含めてjit/native/wasmで確認する。native/wasmの結果一致を対照する。
- unit/regression層では、`std::mem`の確保が`cm_alloc`系シンボルを参照することをコード生成検査ケースで確認する。

## リスクと非互換性

- **アロケータ実装の正しさ（H11）**: フリーリストのヘッダ管理やサイズクラス境界を誤るとヒープ破壊やuse-after-freeを招く。既存のバンプは巻き戻しバグを避けた安全側の実装であり、置き換えは慎重な境界テストを要する。
- **性能特性の変化（H11）**: バンプは確保が定数時間で最速だが解放できない。フリーリスト化で確保コストは上がるが実メモリが返る。ホットパスの確保コストを計測し、初期プールの高速パスは維持する。
- **確保経路一本化の影響（M14）**: `std::mem`をlibc直呼びから`cm_alloc`経由へ変えると、既定アロケータのオーバーヘッドや挙動差が全確保に波及する。既定は従来と等価な薄いラッパにして挙動を保つ。
- **wasmでの到達性（H11とdropパスの補完）**: 別文書memory-drop-and-lifetimeのdropパスは論理的な解放呼び出しを挿入するが、wasm側が解放不能な限り実メモリは返らない。本文書のH11改修が揃って初めてwasmで実効的なメモリ回収が成立する。
- **デッドコード撤去の判断（M14）**: `CmTempStringPool`はヘッダで公開APIとして定義されているため、撤去する場合は外部参照が無いことを確認する。dropパスとの役割整理を先に確定させる。

## 関連

- `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（監査本体、テーマ4「メモリを解放しない」）
- `docs/design/v0.17.0/memory-drop-and-lifetime.md`（C12/C13、一時オブジェクトのdrop挿入。wasmでの実解放は本文書H11と補完関係）
- `src/internal/codegen/llvm/wasm/runtime_format.c` / `runtime_wasm.c`（wasmアロケータ）
- `src/internal/codegen/common/runtime_alloc.c` / `runtime_alloc.h`（アロケータ抽象化・テンポラリプール）
- `libs/std/mem/mod.cm`（`std::mem`のFFI確保経路）
