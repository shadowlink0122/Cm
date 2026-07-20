---
title: 複数変数キャプチャ対応クロージャ（クロージャ環境化）
parent: v0.17.0 Design
---

# 複数変数キャプチャ対応クロージャ（クロージャ環境化）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C6 | スライス/言語 | 2変数以上キャプチャするクロージャがnativeでLLVM検証エラー、jsではキャプチャを黙って落とし誤答 | 未着手 |

## 背景と根本原因

現在のクロージャ実装は「ラムダを独立関数として切り出し、キャプチャ変数を先頭引数として前置する」方式であり、キャプチャ環境（クロージャオブジェクト）という概念を持たない。
ラムダのlowering自体は複数キャプチャを表現できるが、`map`/`filter`のような高階ランタイム関数がキャプチャを単一スカラ値で受ける前提で組まれているため、2変数以上で破綻する。

### lowering層は複数キャプチャを表現できる

HIR層でラムダは独立関数へ切り出され、キャプチャ変数が先頭パラメータとして前置される（`src/internal/hir/lowering/expr.cpp:1295-1304`）。
生成関数のシグネチャは `fn(cap0, cap1, ..., param0, ...)` となり、ここはキャプチャ数に制限がない。
MIR層のローカル宣言も複数キャプチャを保持できる（`src/internal/mir/nodes.hpp:417-419` の `std::vector<LocalId> captured_locals;`）。
高階呼び出しをクロージャ版へ書き換えるパスは、キャプチャを全件、追加引数として積む（`src/internal/mir/lowering/lowering.cpp:203-205`）。

```cpp
// lowering.cpp:203-205 — キャプチャを全件 args へ push（可変長）
for (LocalId cap_local : local_decl.captured_locals) {
    call_data.args.push_back(MirOperand::copy(MirPlace{cap_local}));
}
```

つまりMIRの時点では `args = [arr, size, fn_ref, cap0, cap1, ...]` と2個以上を渡せる。
破綻するのは下流のランタイム宣言・codegenが単一キャプチャ固定であるためである。

### ランタイムが単一キャプチャ固定（真の根本原因）

高階ランタイム関数はキャプチャを第一引数の単一スカラで受ける（native `src/internal/codegen/llvm/native/runtime_slice.c:447-450`）。

```c
// runtime_slice.c:447-450 — キャプチャは単一 int64
typedef int32_t (*MapFnI32Closure)(int64_t, int32_t);
typedef int64_t (*MapFnI64Closure)(int64_t, int64_t);
typedef int8_t  (*FilterFnI32Closure)(int64_t, int32_t);
typedef int8_t  (*FilterFnI64Closure)(int64_t, int64_t);
```

`__builtin_array_map_closure`（`runtime_slice.c:481`）は `fn(capture, arr[i])` と単一capのみ適用する（`runtime_slice.c:499`）。
クロージャ版が存在するのは `map`/`filter`（i32/i64）のみで、`reduce`/`sort`のクロージャ版はソース全体に存在しない。
native側capは `int64_t`、wasm側capは `int32_t`（`src/internal/codegen/llvm/wasm/runtime_slice.c:462-465`）と幅も食い違う。

### nativeがLLVM検証エラーになる仕組み

LLVMコアはランタイムを4引数固定・非可変長で宣言する（`src/internal/codegen/llvm/core/runtime/builtins.cpp:553-558`）。

```cpp
// builtins.cpp:553-558 — capture 枠は i32 が1個、可変長でない
auto funcType = llvm::FunctionType::get(
    ctx.getPtrType(),
    {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type()}, false);
```

MIRが2キャプチャで実引数を5個 `[ptr, i64, ptr, cap0, cap1]` にするのに対し、宣言のパラメータ数は4で非可変長のため、`CreateCall`の実引数数と`FunctionType`のパラメータ数が不一致になりモジュール検証（verifier）が失敗する。
さらにruntime実体のcapが`int64_t`なのに宣言が`i32`という幅不一致も併存する。

### jsがキャプチャを黙って落とす仕組み

JSバックエンドの高階クロージャ変換は `argStrs[3]`（キャプチャ第1個）だけを使う（`src/internal/codegen/js/builtins.cpp:431-434`）。

```cpp
// builtins.cpp:431-434 — argStrs[3] の1個しか渡さない
if (name == "__builtin_array_map_closure" && argStrs.size() >= 4) {
    return "__cm_unwrap(" + argStrs[0] + ").map((x) => " + argStrs[2] + "(x, " + argStrs[3] + "))";
}
```

`argStrs[4]`以降（2個目以降のキャプチャ）は無言で破棄されエラーも警告も出ないため誤答になる。
加えてnativeは`fn(capture, elem)`（`runtime_slice.c:499`）、JSは`fn(elem, capture)`（`builtins.cpp:433`）と引数順が逆であり、単一キャプチャでも順序不整合の疑いがある（生成ラムダのparam順はキャプチャ先頭）。

### 正しく動く既存経路（設計の参考）

ラムダを変数に束ねて直接呼ぶ経路（`let f = |...| ...; f(x)`）は複数キャプチャで壊れない。
LLVMは実際に生成済みのラムダ関数型を使って全キャプチャを先頭に積む（`src/internal/codegen/llvm/core/terminator/invoke.cpp:440-479`）。
JSは `.bind(null, cap0, cap1, ...)` で全キャプチャをbindする（`src/internal/codegen/js/emit_expressions.cpp:500-511`）。
破綻するのは高階ランタイム（`.map()`/`.filter()`）経由の受け渡しに限られる。

## 設計方針

複数キャプチャを1個のキャプチャ環境（環境ポインタ）に集約し、高階ランタイムには「関数ポインタ+環境ポインタ」の対（クロージャオブジェクト）を単一引数として渡す方式へ統一する。
これによりランタイム関数のシグネチャをキャプチャ数に非依存にし、native検証エラーとjsキャプチャ落ちを同時に解消する。
既存の単一キャプチャfast-pathは破壊せず、方式を追加する形で移行する（言語設計原則: 破壊的変更回避）。

1. キャプチャ環境の表現統一
   - 2個以上のキャプチャを、キャプチャ変数を順に格納したヒープ確保の環境構造体（環境ポインタ `void* env`）へ集約する。
   - 生成ラムダのシグネチャを `fn(void* env, elem)` に統一し、本体先頭で `env` から各キャプチャをロードする（現在の「キャプチャ先頭引数展開」を環境ロードに置換）。
   - 0キャプチャは `env=null`、1キャプチャは互換のため当面スカラfast-pathを維持（または環境化に一本化）。

2. ランタイム関数のシグネチャをキャプチャ非依存化
   - `MapFnI32Closure` 等を `int32_t (*)(void* env, int32_t)` に変更し、`__builtin_array_map_closure` 等の第4引数を `void* env` に統一する（native/wasm両方、幅不一致も解消）。
   - LLVMコアの宣言（`builtins.cpp:553-574`）を `void* env`（ptr型）1個へ揃える。

3. codegenの環境構築と受け渡し
   - MIR書き換えパス（`lowering.cpp:142` `rewrite_hof_calls_for_closures`）で、複数キャプチャを環境構造体allocへまとめ、`args = [arr, size, fn, env]` の固定4引数へ正規化する。
   - JS（`builtins.cpp:431-438`）は `(x) => fn(env, x)` の形へ変更し、環境オブジェクトへ全キャプチャを格納する。

4. 黙殺の排除
   - キャプチャ数がランタイム想定と不一致になる経路（`builtins.cpp` のcodegen）で、解決不能なら診断付きハードエラーにし、黙って落とさない（監査ロードマップ第2段「黙殺禁止インバリアント」に整合）。

## 構文例・出力例

言語構文は既存のラムダ構文のまま（新構文なし）。差分は生成コードのみである。

```cm
fn main() {
    int base = 10;
    int scale = 3;
    int[] xs = [1, 2, 3];
    // 2変数キャプチャ（base, scale）
    int[] ys = xs.map(|x| (x + base) * scale);
    println("{ys[0]} {ys[1]} {ys[2]}");   // 期待: 33 36 39
}
```

現状の誤動作:

- native/jit/wasm: LLVM検証エラーでコンパイル失敗（`builtins.cpp:553` の4引数固定宣言に5引数呼び出し）。
- js/ts: `scale` が黙って落ち、`(x + base)` のみ評価されて `11 12 13` 等の誤答。

設計適用後の生成イメージ（JS、環境オブジェクト方式）:

```js
// 環境に全キャプチャを格納し、単一 env として渡す
const __env0 = { base: base, scale: scale };
const ys = __cm_unwrap(xs).map((x) => __lambda_0(__env0, x));
```

## 実装の段階分割

1. ランタイムシグネチャの環境化（native/wasm `runtime_slice.c` の `*Closure` typedefと `__builtin_array_*_closure` を `void* env` へ、native/wasm間の幅統一を含む）。
2. LLVMコア宣言の追従（`builtins.cpp:553-574` を `void* env` 1個へ）と、MIR書き換えパスでの環境構造体構築（`lowering.cpp:142-205`）。
3. 生成ラムダ本体の環境ロード化（`hir/lowering/expr.cpp:1295-1304` のキャプチャ前置を環境ロードへ、`invoke.cpp:440-479` の直接呼び出し経路との整合）。
4. JS/TSのクロージャ変換を環境方式へ（`js/builtins.cpp:431-438`）、引数順の統一。
5. `reduce`/`sort`等クロージャ非対応の高階関数へのクロージャ対応拡張、またはクロージャ引数を型検査で明示拒否（黙殺排除）。

## テスト計画（tests/common/配下）

- `tests/common/lambda/` に複数キャプチャケースを追加（2変数・3変数、int/long混在、`map`/`filter`）。
- `tests/common/array_higher_order/` に高階関数×複数キャプチャの全バックエンド一致テストを追加（jit/native/wasm/js/tsで同一出力を期待）。
- 回帰: 0キャプチャ・1キャプチャの既存ラムダが従来どおり動くこと、`let f = |..| ..; f(x)` 直接呼び出し経路の非退行。
- ネガティブ: `reduce`/`sort`へクロージャを渡した場合に診断付きエラー（未対応のまま黙って落とさない）。

## リスクと非互換性

- 単一キャプチャfast-pathを環境方式に一本化すると、既存生成物のABI（`int64 capture`）が変わる。ランタイムとcodegenを同時更新するため外部影響はないが、`runtime_slice.c` の`.o`キャッシュ再生成が必要。
- native/wasmでcap幅が食い違っていた（int64 vs int32）ため、環境ポインタ化で幅依存の潜在バグが顕在化する可能性がある。段階1で幅統一を先行する。
- 環境をヒープ確保する場合、監査所見C12（一時オブジェクト未解放）と同じリーク経路に載る。環境の寿命管理はdropパス導入（監査ロードマップ第2段）と整合させる。
- 言語構文の変更はなく後方互換。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（C6、推奨対応ロードマップ第3段）
- 関連所見: C12（一時オブジェクト未解放）— 環境のヒープ寿命管理
- 主要ファイル: `src/internal/codegen/llvm/native/runtime_slice.c`, `src/internal/codegen/llvm/wasm/runtime_slice.c`, `src/internal/codegen/llvm/core/runtime/builtins.cpp`, `src/internal/mir/lowering/lowering.cpp`, `src/internal/hir/lowering/expr.cpp`, `src/internal/codegen/js/builtins.cpp`
