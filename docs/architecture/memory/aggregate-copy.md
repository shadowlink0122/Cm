# 集約コピーのlowering（native/jit）

構造体・固定長配列といった集約の代入・引数渡し・戻り値は、小さいものだけを第一級SSA値として動かし、大きいものはmemcpy・ポインタ渡し・sret（隠し出力ポインタ）に落とすことで、LLVMのSROA/instcombineによる全要素SSA展開とそれに伴うコンパイル時間・メモリの超線形爆発を防ぐ。
サイズ判定はLLVM DataLayoutの正確な値に一本化されており、MIR側のレイアウト計算も`LoweringContext::layout_size`が唯一の情報源である。

## 概要

集約は原則として`alloca`されたメモリとして扱われ、オペランド変換は構造体allocaをロードせずポインタのまま返す（`src/internal/codegen/llvm/core/operand.cpp`の構造体経路）。
値として動かす必要が生じる箇所ごとに、サイズに応じて3つの戦略を使い分ける。

| 経路 | 小さい集約 | 大きい集約 |
|---|---|---|
| 代入（コピー） | `load %S` → `store %S`の第一級集約コピー | 128バイト以上は`CreateMemCpy` |
| 引数渡し | 16バイト以下の構造体は値渡し（System V ABI準拠） | 16バイト超はポインタ渡し＋呼び出し先エントリコピー |
| 戻り値 | 16バイト以下は第一級値でreturn | 16バイト超の構造体はsret（先頭の隠し出力ポインタ）へ変換 |

第一級集約のままO2に渡すと、SROA/instcombineが集約を要素単位のSSA値へ分解するためIRが要素数に対して超線形に膨張する。
`int[16384]`フィールドを持つ構造体のコピーや戻り値がこの経路に落ちると、コンパイルが実用不能な時間・メモリに達することが実測されており、本設計はその爆発源を経路ごとに塞いでいる（背景と実測は[archive設計文書](../../archive/v0.17.0/memory/aggregate-copy-lowering.md)）。

## データ構造とアルゴリズム

### 閾値超集約のmemcpy化（代入）

構造体どうしの代入はソースallocaのポインタから行われ、コピーサイズをDataLayoutで測って閾値以上ならload/storeを発行せずmemcpy1発で転写する（`src/internal/codegen/llvm/core/statement/assign.cpp:444`〜`:450`）。

```cpp
// src/internal/codegen/llvm/core/statement/assign.cpp:444
const auto& dataLayout = module->getDataLayout();
const uint64_t copySize = dataLayout.getTypeAllocSize(targetType);
constexpr uint64_t kAggregateMemcpyThreshold = 128;
if (copySize >= kAggregateMemcpyThreshold && addr != rvalue) {
    builder->CreateMemCpy(addr, llvm::MaybeAlign(), rvalue, llvm::MaybeAlign(), copySize);
    return;
}
// ポインタからロードして構造体値を取得
rvalue = builder->CreateLoad(targetType, rvalue, "struct_load");
```

閾値未満の小構造体は従来どおり`struct_load`/`store`で、mem2reg・SROAによるレジスタ昇格の恩恵を受ける。
Tagged Unionペイロードへの書き込みと配列要素への構造体格納はサイズに依らずmemcpyを使う既存条件`needsStructCopy`（`src/internal/codegen/llvm/core/statement/assign.cpp:681`）が別途ある。

### 引数のポインタ渡しと値セマンティクスの維持

構造体引数の渡し方は`isSmallStruct`（`src/internal/codegen/llvm/core/mir_to_llvm.cpp:58`）が決める。
判定はLLVM DataLayoutの`getTypeAllocSize`で16バイト以下かを見る正確なサイズ計算であり、手計算のフィールド見積り（Arrayをdefault 8バイト扱いする類）を使わない。
これは`int[16384]`のような大配列フィールド構造体を「小」と誤判定して第一級集約の値渡しに落とすと、SROA全展開でO2/Ozのコンパイル時間が爆発するためである。

シグネチャ構築では16バイト以下を値渡し、16バイト超をポインタ渡しにする（`src/internal/codegen/llvm/core/translate/signature.cpp:270`〜`:276`）。
ポインタ渡しはそのままでは呼び出し先の変更が呼び出し元へ波及して値セマンティクスが壊れるため、呼び出し先の関数エントリでローカルコピーを作る（`src/internal/codegen/llvm/core/translate/function.cpp:285`〜`:299`、`byval_copy`のmemcpy）。
`self`はMIR型がPointerなのでこの分岐に入らず、従来どおり参照渡しである。
LLVMの`byval`属性は「呼び出し側がコピーする」意味論であり、この呼び出し先コピー方式と一致しないため意図的に付与していない。

### 大構造体戻り値のsret化

戻り値の第一級集約returnも同じ爆発源になるため、16バイト超の構造体を返す関数はsretへ変換する。
判定述語は`needsSretReturn`（`src/internal/codegen/llvm/core/mir_to_llvm.cpp:83`）で、非extern・非main・戻り値がDataLayoutで16バイト超の構造体・インターフェイス値でない（fat pointer16バイトのため不要）・アドレス未取得、をすべて満たす場合にtrueを返す。
アドレス取得の判定は`collectAddressTakenFunctions`（`src/internal/codegen/llvm/core/mir_to_llvm.cpp:114`、呼び出し起点は`src/internal/codegen/llvm/core/translate/program.cpp:33`・`:511`）がMIRプログラム全体を事前走査し、`FunctionRef`がCall終端の呼び出し先以外の位置（rvalueオペランド・呼び出し引数）に現れる関数を除外集合に入れる。
関数ポインタ・クロージャ・vtable経由の間接呼び出しはシグネチャ変換を追跡できないためである。

変換は3箇所が同一の述語を再計算して整合させる（決定的な計算のため別途メタデータは持たない）。

- シグネチャ: 戻り値をvoidにし、先頭へポインタパラメータを挿入して`StructRet`+`NoAlias`属性を付与する（`src/internal/codegen/llvm/core/translate/signature.cpp:293`〜`:300`・`:338`〜`:342`）
- 関数本体: LLVM引数とMIR引数の対応を1つずらし（`src/internal/codegen/llvm/core/translate/function.cpp:262`〜`:270`）、Return終端でreturn_local allocaの内容をsretポインタへmemcpyして`ret void`する（`src/internal/codegen/llvm/core/terminator.cpp:38`〜`:50`、フォールバック終端は`src/internal/codegen/llvm/core/terminator/invoke.cpp:800`）
- 呼び出し側: 呼び出し先の第0パラメータに`StructRet`属性があれば、格納先placeのalloca（無ければ`sret_discard`のダミーalloca）を引数先頭へ挿入する（`src/internal/codegen/llvm/core/terminator/invoke.cpp:326`〜`:342`）。引数個数の整合チェックもsret分を+1補正する（`:203`〜`:206`）

呼び出し側の格納先allocaを直接渡すため、戻り値のコピーは0回になる（関数本体が呼び出し元バッファへ直接書く）。

### MIR層の補助

- レイアウト計算の一本化: サイズ・アライメントは`LoweringContext::layout_size`/`layout_align`（`src/internal/mir/lowering/context.cpp:481`・`:431`）に集約され、Array（要素サイズ×要素数）・ネスト構造体（フィールド再帰）・Union（最大バリアント）を正しく扱う。MIRノード側に手計算のsize/offsetフィールドは存在せず、LLVM側はDataLayoutが情報源のため、二重管理による食い違いが構造的に起きない
- 集約コピーチェーンの畳み込み: `CopyPropagation`（`src/internal/mir/passes/scalar/propagation.hpp:15`、O1以上で`src/internal/mir/passes/core/manager.cpp`が登録）が「コピー元→一時→最終先」の集約コピー連鎖を単一コピーへ畳み込み、memcpy化してもなお残る二重コピーのコストを削る。js/ts系は構造体コピーが深いクローン意味論のため、この集約伝播を無効化するフラグで除外される（`src/cmd/cm/build.cpp:492`の`no_aggregate_copy_prop`）

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/statement/assign.cpp` | 代入のmemcpy化閾値（`kAggregateMemcpyThreshold`）とTagged Union/配列要素のmemcpy経路 |
| `src/internal/codegen/llvm/core/mir_to_llvm.cpp` / `mir_to_llvm.hpp` | `isSmallStruct`・`needsSretReturn`・`collectAddressTakenFunctions` |
| `src/internal/codegen/llvm/core/translate/signature.cpp` | 引数の値渡し/ポインタ渡し決定、sretシグネチャ変換と属性付与 |
| `src/internal/codegen/llvm/core/translate/function.cpp` | sret引数のずらし、ポインタ渡し引数の呼び出し先エントリコピー |
| `src/internal/codegen/llvm/core/terminator.cpp` / `terminator/invoke.cpp` | sret関数のreturn経路と呼び出し側の格納先挿入 |
| `src/internal/codegen/llvm/core/translate/program.cpp` | アドレス取得関数の全体走査（sret除外集合） |
| `src/internal/codegen/llvm/core/operand.cpp` | 構造体allocaをポインタのまま返すオペランド変換 |
| `src/internal/mir/lowering/context.cpp` | `layout_size`/`layout_align`（MIR側レイアウトの唯一の情報源） |
| `src/internal/mir/passes/scalar/propagation.hpp` | 集約コピーチェーンを畳む`CopyPropagation` |

## 落とし穴とケア

- **防ぐバグのクラス**: この設計が塞いでいるのは「集約サイズに対して超線形のコンパイル時間・メモリ」（第一級集約のSROA全展開）と「値渡しのはずの構造体が呼び出し先の変更で書き換わる」（ポインタ渡しの隔離漏れ）の2クラスである。集約を動かす新しい経路を追加する際は、必ずサイズ閾値で第一級値経路とmemcpy/ポインタ経路を分けること
- **sret除外条件の維持**: extern関数（FFIのABI互換）・アドレス取得された関数（間接呼び出しのシグネチャ追跡不能）・インターフェイス値（fat pointer）はsret変換してはならない。特に`FunctionRef`が新しいMIR位置に現れる構文を追加した場合、`collectAddressTakenFunctions`の走査対象に含めないと間接呼び出しとの引数不一致でクラッシュする
- **述語の3点一致**: sretの適用判定はシグネチャ構築・関数本体・呼び出し側が独立に`needsSretReturn`を再計算して一致させる方式である。判定に非決定的な要素（走査順依存等）を入れると3箇所が食い違い、引数が1つずれた呼び出しが生成される
- **byval属性を使わない判断**: LLVMの`byval`は呼び出し側コピーの意味論で、本実装の呼び出し先コピー方式と不一致のため付与しない。将来ABI最適化で導入する場合はコピー責務の反転を全経路で行う必要がある
- **閾値の性格**: memcpy閾値（128バイト）と値渡し閾値（16バイト）は独立で、前者はSROA展開の抑制、後者はSystem V ABIのレジスタ渡し境界に対応する。小さくしすぎるとmemcpy呼び出しと隔離コピーのオーバーヘッドが増え、大きくしすぎると爆発を防げない
- **バックエンド境界**: memcpy化・sret・ポインタ渡しはLLVM系（jit/native/wasm）の改修であり、js/tsは別経路である。値セマンティクス（値渡し隔離・ネストコピー）の観測結果が全バックエンドで一致することを回帰で守る
- **回帰テスト**: 大集約のコピー・戻り値の正しさは`tests/common/structs/big_struct_copy.cm`・`big_struct_return.cm`で固定している（コンパイル時間が実用範囲に収まることの確認を兼ねる）。集約コピーチェーンの畳み込みは`tests/unit/mir_pass_test.cpp`の`CopyPropagation_FoldsAggregateCopyChain`で固定している

## 関連資料

- [集約コピーのmemcpy化とレイアウト計算の一本化（archive設計文書）](../../archive/v0.17.0/memory/aggregate-copy-lowering.md)
- [RAII・dropパスと所有権](drop-and-ownership.md) — 集約ローカルのゼロ初期化と解放挿入
- [アロケータ設計](allocator.md)
