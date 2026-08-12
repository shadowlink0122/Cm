# クロージャのlowering

Cmのラムダは「独立関数への切り出し + キャプチャ変数の先頭引数前置」を基本方式とし、キャプチャ数に依存できない高階ランタイム関数（`map`/`filter` 等）へ渡すときだけ「キャプチャ列をスタック上のi64環境配列（env）へ集約し、envから復元して実ラムダを呼ぶサンクを合成する」環境化を行う。
本書はAST→HIR→MIR→LLVMの各段でのクロージャ表現と、`__lambda_` 命名規約、環境化の仕組みを記述する。

## 概要

ラムダ式はHIR loweringで `__lambda_N` という名前の独立した `HirFunction` に切り出され、キャプチャ変数はその関数の先頭パラメータとして前置される（シグネチャは `fn(cap0, cap1, ..., param0, ...)`）。
この方式はキャプチャ数に上限がなく、`let f = |x| ...; f(v)` のような直接呼び出し経路では呼び出し点でキャプチャ値を引数先頭に積むだけで完結するため、クロージャオブジェクトのヒープ確保が不要である。
一方、`xs.map(|x| ...)` のような高階ランタイム関数はC実装のシグネチャが固定であり、可変個のキャプチャを引数として受けられない。
そこでLLVMコード生成時に、キャプチャ列を「関数ポインタ + 環境ポインタ（`void* env`）」の対へ正規化し、ランタイム側のシグネチャをキャプチャ数非依存にしている。
この二方式併存の設計判断と背景は [複数変数キャプチャ対応クロージャ（archive）](../../archive/v0.17.0/interfaces-derive/closures-multi-capture.md) に詳しい。

## データ構造とアルゴリズム

### AST・型検査: キャプチャの解析

ラムダは `struct LambdaExpr`（`src/internal/syntax/ast/expr.hpp:379-393`）で、パラメータ・本体に加えて入れ子構造体 `struct Capture { std::string name; TypePtr type; bool by_ref; }` のリスト `captures` を持つ（`expr.hpp:385-390`）。
キャプチャリストは構文ではなく型検査が埋める: `TypeChecker::infer_lambda`（`src/internal/types/checking/expr/lambda.cpp:20`）が本体中の識別子を収集し、パラメータとラムダ内ローカルを除いた外部変数をキャプチャとして特定する（`lambda.cpp:159-181`）。
キャプチャは常に値キャプチャであり、`cap.by_ref = false` が固定で設定される（`lambda.cpp:174`）。
ラムダの型は `TypeKind::Function`（戻り値型 + パラメータ型列）で、キャプチャは型に含まれない（`lambda.cpp:184-188`）。

### HIR lowering: 独立関数への切り出しと __lambda_ 命名

`HirLowering::lower_lambda`（`src/internal/hir/lowering/expr.cpp:1292`）がラムダを `HirFunction` へ切り出す。

```cpp
// src/internal/hir/lowering/expr.cpp:1309-1310 — 命名の唯一の生成箇所
static int lambda_counter = 0;
std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);
```

キャプチャ変数は生成関数の先頭パラメータとして前置され（`expr.cpp:1316-1325`）、生成関数は `lambda_functions_` に蓄積される（`expr.cpp:1371`）。
ラムダ式自体の値は、キャプチャを除いたパラメータ列で作った関数ポインタ型（`hir::make_function_ptr`、`expr.cpp:1373-1379`）を持つ `HirVarRef` であり、キャプチャがある場合は `is_closure = true` と `captured_vars`（変数名と型の列）が設定される（`expr.cpp:1384-1400`、構造体定義は `src/internal/hir/nodes.hpp:37-52`）。

`__lambda_` プレフィックスは下流の複数箇所で特別扱いの判定キーになる。

```cpp
// src/internal/codegen/llvm/core/translate/signature.cpp:31-34 — ラムダはオーバーロード用マングリングをスキップ
// ラムダ関数はそのまま
if (func.name.find("__lambda_") == 0) {
    return func.name;
}
```

呼び出し側の関数ID解決 `generateCallFunctionId` にも同じ判定があり（`signature.cpp:129-132`）、ラムダには引数型サフィックス（`_i`/`_i64`/`_Sxxx` 等、`signature.cpp:52-118`）が付かない。
さらに、到達可能関数の起点集合に `__lambda_` を常に含めてDCEで消されないようにし（`src/internal/codegen/llvm/core/translate/program.cpp:396`）、MIRインライナはラムダ・クロージャをインライン化対象から除外する（`src/internal/mir/passes/interprocedural/inlining.cpp:118-121`）。

### MIR: LocalDeclのサイド情報としてのクロージャ

MIRにクロージャ専用の値表現はなく、関数参照を保持するローカル変数のサイド情報として表現される（`src/internal/mir/nodes.hpp:417-419`）。

```cpp
// src/internal/mir/nodes.hpp:417-419
bool is_closure = false;
std::string closure_func_name;         // 実際のクロージャ関数名
std::vector<LocalId> captured_locals;  // キャプチャされた変数のローカルID
```

`ExprLowering::lower_var_ref`（`src/internal/mir/lowering/expr/basic.cpp:328-354`）が `HirVarRef` の `captured_vars` を `resolve_variable` でLocalIdへ解決して `captured_locals` に転記する。
クロージャ値が別のローカルへ代入・let束縛されると情報が途切れるため、`MirLowering::propagate_closure_info`（`src/internal/mir/lowering/lowering.cpp:217`、呼び出しは `lowering.cpp:63`）がUse/Copy/Move代入をたどって `is_closure`/`closure_func_name`/`captured_locals` を宛先ローカルへ固定点まで伝播する（let束縛側の転記は `src/internal/mir/lowering/stmt/let.cpp:638-647`）。

### 直接呼び出し経路: キャプチャ前置

変数経由でクロージャを呼ぶ場合、MIR呼び出しloweringはローカルの `is_closure` を見て関数参照を `closure_func_name` に差し替え、`captured_locals` 全件を引数の先頭に挿入する（`src/internal/mir/lowering/expr_call.cpp:264-271`、挿入は `expr_call.cpp:301-311`）。
MIRの時点で解決できず間接呼び出しとしてLLVMまで届いた場合も、コード生成が `LocalDecl` のクロージャ情報から直接呼び出し + キャプチャ前置へ変換する（`src/internal/codegen/llvm/core/terminator/invoke.cpp:618-702`、キャプチャのload・前置は `invoke.cpp:638-663`）。
この経路は環境構造体を作らず、ヒープ確保も発生しない。

### 高階ランタイム経路: 環境配列とサンク合成

`xs.map(closure)` はMIR loweringの `rewrite_hof_calls_for_closures`（`src/internal/mir/lowering/lowering.cpp:143`、呼び出しは `lowering.cpp:68`）が `__builtin_array_map_closure` 等のクロージャ変種呼び出しへ書き換え、キャプチャ全件を末尾引数として積む（`lowering.cpp:203-211`）。
LLVMコード生成は `_closure` サフィックスのビルトイン呼び出しを検出して `normalizeHofClosureArgs` を呼ぶ（`src/internal/codegen/llvm/core/terminator/call.cpp:306-311`）。
`MIRToLLVM::normalizeHofClosureArgs`（`src/internal/codegen/llvm/core/terminator/invoke.cpp:18`）は次の手順で可変個キャプチャを固定引数へ正規化する。

1. キャプチャ型がi64スロットへ可逆に格納できること（64bit以下の整数・ポインタ・浮動小数）を事前検証し、構造体などの集約キャプチャは変換せずLLVM検証エラーで顕在化させる（`invoke.cpp:61-71`）。
2. キャプチャ数分の `[N x i64]` 配列を関数エントリブロックへallocaし（ループ内呼び出しでスタックが伸び続けるのを防ぐ）、各キャプチャをPtrToInt/BitCast/SExt等でi64へ正規化して格納する（`invoke.cpp:89-113`）。

```cpp
// src/internal/codegen/llvm/core/terminator/invoke.cpp:89-93
auto* envArrTy = llvm::ArrayType::get(i64Ty, capCount);
llvm::Function* curFn = builder->GetInsertBlock()->getParent();
llvm::IRBuilder<> entryBuilder(&curFn->getEntryBlock(), curFn->getEntryBlock().begin());
auto* envAlloca = entryBuilder.CreateAlloca(envArrTy, nullptr, "hof_env");
```

3. `fn(void* env, [acc,] elem)` シグネチャのサンク関数をラムダ×ランタイム変種ごとに1つ合成する（名前は `lambdaName + "$env_thunk_" + variant`、`invoke.cpp:115-126`）。サンク本体はenvから各キャプチャをロードして型調整し、キャプチャ前置版の実ラムダを呼ぶ（`invoke.cpp:155-167`）。
4. 実引数列からキャプチャを除去し、関数ポインタをサンクへ差し替え、envを末尾へ追加して `[arr, size, サンク, env]`（reduceは `[arr, size, サンク, init, env]`）へ正規化する（`invoke.cpp:183-188`）。

nativeランタイム側は環境ポインタを受けるシグネチャで統一されている（`src/internal/codegen/llvm/native/runtime/slice.c:494-497` の `typedef int32_t (*MapFnI32Closure)(void*, int32_t);` 等）。
`__builtin_array_map_closure`（`runtime/slice.c:528`）は要素ごとに `fn(env, arr[i])`（`runtime/slice.c:546`）とサンクを適用する。
LLVMコア側のビルトイン宣言も第4引数が `ptr`（env）で固定されている（`src/internal/codegen/llvm/core/runtime/builtins.cpp:605-611`、reduce/forEach等の `_closure` 変種は `builtins.cpp:632-673`）。

## 実装箇所

| 役割 | ファイル |
|---|---|
| ラムダAST・キャプチャ表現 | `src/internal/syntax/ast/expr.hpp:379-393` |
| キャプチャ解析（型検査） | `src/internal/types/checking/expr/lambda.cpp` |
| ラムダ切り出し・__lambda_命名 | `src/internal/hir/lowering/expr.cpp:1292-1408` |
| MIRのクロージャサイド情報 | `src/internal/mir/nodes.hpp:407-430` |
| クロージャ情報の転記・伝播 | `src/internal/mir/lowering/expr/basic.cpp:328-354`, `src/internal/mir/lowering/lowering.cpp:217`, `src/internal/mir/lowering/stmt/let.cpp:638-647` |
| 直接呼び出しのキャプチャ前置（MIR） | `src/internal/mir/lowering/expr_call.cpp:264-311` |
| HOF呼び出しのクロージャ変種書き換え | `src/internal/mir/lowering/lowering.cpp:143-214` |
| 間接呼び出し→直接化・env正規化（LLVM） | `src/internal/codegen/llvm/core/terminator/invoke.cpp:18-188`, `invoke.cpp:618-702` |
| _closure 変種の検出 | `src/internal/codegen/llvm/core/terminator/call.cpp:306-311` |
| マングリング除外・到達可能起点・インライン禁止 | `src/internal/codegen/llvm/core/translate/signature.cpp:31-34,129-132`, `core/translate/program.cpp:396`, `src/internal/mir/passes/interprocedural/inlining.cpp:118-121` |
| nativeランタイム（env版HOF） | `src/internal/codegen/llvm/native/runtime/slice.c:494-548` |

## 落とし穴とケア

- 環境化はランタイムシグネチャをキャプチャ数非依存にするための設計であり、これが「2変数以上のキャプチャで宣言と実引数数が食い違いLLVM verifierが失敗する」バグのクラスを防いでいる（背景は [archive文書](../../archive/v0.17.0/interfaces-derive/closures-multi-capture.md)）。ランタイムの `_closure` 変種を追加するときは必ず `void* env` を第1引数で受ける形に揃えること。
- クロージャ性は型ではなく `LocalDecl` のサイド情報である。クロージャ値の新しい代入経路（新しい文種や式種）を追加した場合、`propagate_closure_info`（`lowering.cpp:217`）が追跡できないとキャプチャが黙って失われ通常の関数ポインタ呼び出しになるため、伝播対象に追加すること。
- envスロットはi64固定であり、集約型（構造体等）のキャプチャは意図的に変換せずLLVM検証エラーとして顕在化させる（`invoke.cpp:69` のコメント）。この「黙って落とさない」振る舞いを、フォールバックの追加などで静かな誤答へ退化させないこと。
- 環境配列のallocaは必ず関数エントリブロックに置く（`invoke.cpp:89-93`）。呼び出し点のブロックにallocaするとループ内HOF呼び出しでスタックが単調に伸びる。
- `__lambda_` プレフィックスは命名規約であると同時に「マングリング除外」「DCE到達可能起点」「MIRインライン禁止」の判定キーである。命名を変更する場合は `signature.cpp`・`program.cpp`・`inlining.cpp` の判定をすべて同期させる必要がある（生成箇所は `hir/lowering/expr.cpp:1310` の一箇所のみ）。
- キャプチャは常に値キャプチャ（`by_ref = false` 固定）であり、キャプチャ後の元変数の変更はクロージャへ反映されない。参照キャプチャを導入する場合は環境スロットの表現（現状i64値のコピー）から見直しが必要になる。
- 回帰テスト: `tests/common/lambda/`（`closure_basic.cm`・`closure_multi_capture.cm` 等）と `tests/common/array_higher_order/`（`closure_multi_capture_hof.cm`・`lambda_combined.cm` 等）をバックエンドスイートで実行し、jit/native間の出力一致を確認する。

## 関連資料

- [複数変数キャプチャ対応クロージャ（設計文書、archive）](../../archive/v0.17.0/interfaces-derive/closures-multi-capture.md)
- [MIRの設計](../pipeline/mir-design.md)
- [コンパイルパイプライン全体像](../pipeline/overview.md)
