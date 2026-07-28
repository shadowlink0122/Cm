---
title: 集約コピーのmemcpy化とレイアウト計算の一本化
parent: v0.17.0 Design
---

# 集約コピーのmemcpy化とレイアウト計算の一本化

大規模開発ボトルネック監査の所見C14/M12/M13に対する実装設計である。
固定長配列を持つ構造体のコピー・値渡しがO2で二次爆発する問題（構造体コピーが第一級SSA集約として出るためSROA/instcombineが要素数に対し超線形に膨張する）、一時変数経由の二重コピーがそれを増幅する問題、そしてMIR側に残る手計算レイアウトがネスト・配列・スライスを誤計算する地雷を扱う。
これらは監査総括テーマ5「集約を値として動かす経路の未成熟」に属する。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C14 | コンパイル時間 | 固定長配列を持つ構造体の代入・値渡しがO2で二次爆発（`int[16384]`フィールドで24秒/6.4GB、コピーがmemcpyでなく全要素SSA展開） | 全Phase実装済み（Phase 1: `assign.cpp`の構造体コピーで128バイト以上の集約を`CreateMemCpy`化。Phase 4: 16バイト超構造体の戻り値をsret（先頭の隠し出力ポインタ）へ変換し、`int[16384]`戻り値のO2コンパイルが実測60秒超タイムアウト→0.6秒。引数側は`isSmallStruct`をDataLayoutの正確なサイズ判定へ修正し、大配列フィールド構造体（従来はArray=8バイト誤計算で第一級集約渡しになりwasm Ozで145秒）もポインタ渡し+呼び出し先エントリコピーへ統一（値渡し隔離の既存バグ——呼び出し先の変更が呼び出し元へ波及、jsのH3値渡しと分裂——も同時に修正。selfはMIR型Pointerのため従来どおり参照渡し）。Bug#45引数個数チェックのsret非対応も+1補正。除外条件: extern関数=FFI ABI互換、アドレス取得された関数=間接呼び出しのシグネチャ追跡不能、インターフェイス値=fat pointer16バイト。LLVMのbyval属性は「呼び出し側コピー」の意味論で、本実装の呼び出し先コピー方式と不一致のため付与しない判断で確定） |
| M12 | コンパイル時間 | 構造体代入が一時変数経由で二重コピーされC14を増幅（コピー省略なし） | 対応済み（MIRの`CopyPropagation`パス（O1以上）が集約の一時経由コピー（コピー元→一時→最終先）を単一コピーへ畳み込むことを確認し、`tests/unit/mir_pass_test.cpp`の`CopyPropagation_FoldsAggregateCopyChain`で回帰固定。`P b = a; P c = b;`の連鎖・関数戻り値経由とも最適化後MIRで一時が消えることを実測。O0は素直な逐次コピーのまま（未最適化ビルドの期待挙動）） |
| M13 | 型システム | MIR側の手計算レイアウト（base.cpp:443・mono_structs.cpp:271）がArray/ネスト構造体/スライスをdefault 8/8で誤計算（現在は未参照のデッドコードだが、参照した瞬間に壊れる地雷、正しい`layout_size`と二重管理） | 実装済み（読み手ゼロのデッドデータだった`MirStructField.offset`・`MirStruct.size`・`MirStruct.align`をノード定義ごと撤去し、base.cpp・mono_structs.cpp両方の手計算レイアウトを削除。レイアウトは`LoweringContext::layout_size`/`layout_align`とLLVMのDataLayoutが唯一の情報源になり、参照した瞬間に食い違う地雷と未初期化フィールドを根絶） |

## 背景と根本原因

### C14: 構造体コピーが第一級SSA集約として出る

構造体値のコピーは、コピー元allocaのポインタを取得（`src/internal/codegen/llvm/core/operand.cpp:351-352`で構造体allocaはロードせずポインタを返す）した後、代入側で第一級集約としてロードされる。
`src/internal/codegen/llvm/core/statement/assign.cpp:374-378`では、ソースがポインタ・ターゲットが構造体型・rvalueがallocaのとき`CreateLoad(targetType, rvalue, "struct_load")`で構造体全体をSSA値としてロードし、最終的に`CreateStore(rvalue, addr)`（同656行）で書き戻す。
この`load %S` → `store %S`という第一級集約コピーは、O2のSROA/instcombineが構造体を要素単位に分解して個々のSSA値に展開するため、要素数に対し超線形にIRが膨張する。
`int[16384]`フィールドを持つ構造体では監査実測で24秒・6.4GBに達した。
`CreateMemCpy`を使う経路は`assign.cpp`に存在するが、その発火条件`needsStructCopy`（同587-589行）はTagged Unionペイロードへの書き込みか配列要素へのIndex projection代入に限られ、通常の構造体どうしの代入・値渡しは対象外である。
そのため大きな集約でもmemcpyでなく要素展開経路に落ちる。

### M12: 一時変数経由の二重コピー

構造体代入がコピー省略（copy elision）を行わず一時変数を経由するため、同じ集約が二度コピーされC14の膨張を増幅する。
コピー元→一時→最終先の各段が上記の第一級集約load/storeとして展開されるため、要素展開のコストが多重にかかる。

### M13: MIR側の手計算レイアウトが地雷

構造体レイアウトのサイズ・アライメントを手計算するコードが`src/internal/mir/lowering/base.cpp`（デフォルト分岐`src/internal/mir/lowering/base.cpp:443`で`size=8, align=8`）と`src/internal/mir/lowering/mono_structs.cpp`（同`src/internal/mir/lowering/mono_structs.cpp:271`のデフォルト分岐で`size=8, align=8`）の2箇所に重複して存在する。
これらのswitchはプリミティブ（Short/Int/Long/Double/Pointer/String）だけを列挙し、Array・ネスト構造体・スライスはdefault分岐に落ちてサイズ8・アライメント8で誤計算する。
正しいサイズ計算は`LoweringContext::layout_size`（`src/internal/mir/lowering/context.cpp:468`）に集約されており、Array（要素サイズ×要素数、511行）・ネスト構造体（フィールド再帰、502行）・Union（最大バリアント、522行）を正しく扱う。
現状この手計算レイアウトは未参照のデッドコードだが、二重管理であり参照した瞬間に`layout_size`と食い違って壊れる地雷である。

## 設計方針

1. **閾値超集約のmemcpy化（C14）**: `assign.cpp`の`needsStructCopy`条件を拡張し、通常の構造体どうしの代入でも構造体サイズがしきい値（例: レジスタ数個ぶんを超えるサイズ）を超える場合は`CreateLoad`+`CreateStore`の第一級集約コピーではなく`CreateMemCpy`を用いる。
   これによりO2のSROA/instcombineが要素展開する対象から大きな集約を外し、コピーを単一のmemcpy呼び出しに保つ。
   小さな構造体は従来どおりload/storeを許容し、しきい値で切り替える。
2. **byval/sret渡し（C14）**: 大きな構造体を関数引数・戻り値として渡す際、第一級集約値ではなく`byval`（引数はポインタ渡し＋呼び出し側でコピー）・`sret`（戻り値は隠し出力ポインタ）属性を用いる。
   現状LLVMバックエンドは`byval`/`sret`属性を一切使っていないため、集約渡しがすべて第一級値経由になっている点を是正する。

   sretの詳細設計（実測: `int[16384]`フィールド構造体を返す関数はO2で60秒超タイムアウト——戻り値の第一級集約がSROA全展開される。引数側は既存の`isSmallStruct`判定で16バイト超がポインタ渡し済みのため、残る爆発源は戻り値）:
   - 変換対象: 非extern関数で、戻り値型が構造体かつ`isSmallStruct`でない（16バイト超）もの。extern "C"関数はlibc/FFIのABI互換を壊すため対象外
   - アドレス取得された関数は除外: 関数ポインタ・vtableエントリ経由の間接呼び出しはシグネチャ変換を追跡できないため、MIRプログラム全体を事前走査して「FunctionRefが呼び出し先以外のオペランドに現れる関数」の集合を作り、その関数は従来ABIを維持する（vtableに入るimplメソッドは自動的に除外される）
   - シグネチャ変換: 戻り値をvoidにし、先頭へ`sret`属性付きポインタパラメータを追加する。判定は呼び出し側・定義側の双方がMIRの型情報から同一の述語で再計算する（決定的なので別途メタデータ不要）
   - 関数本体: return_localのallocaを廃し、sretパラメータのポインタをreturn_localの格納先として束縛する（本体の代入は呼び出し元バッファへ直接書き、Return終端はret voidになる。戻り値ローカルはreturnまで他から読まれないためエイリアスしても安全）
   - 呼び出し側: 格納先placeのallocaをそのままsret引数として渡す（destinationが無い場合はダミーallocaを渡す）。戻り値のstoreが消えるためC14 Phase 1のmemcpyすら不要になる（コピー0回）
   - 段階導入: sret（戻り値）を先行させ、byval（引数の呼び出し側コピー明示化）は「ポインタ渡し引数を呼び出し先が変更した場合に呼び出し元へ波及する」値セマンティクス検証とあわせて次段とする
3. **コピー省略（M12）**: 構造体代入で不要な一時変数経由の二重コピーを避け、最終先へ直接memcpyする経路を導入する。
   MIRで一時ローカルを経由する代入パターンを検出し、コピー元から最終先への単一コピーに畳み込む。
4. **手計算レイアウトの一本化（M13）**: `base.cpp:443`・`mono_structs.cpp:271`の手計算レイアウトを撤去し、サイズ・アライメント計算を`layout_size`（およびアライメント計算）へ一本化する。
   デッドコードであっても二重管理を残さず、単一の正しい実装を唯一の真実の源にする。

## 構文例・出力例

C14の再現形（構文自体は正常で、コンパイル時間とメモリが爆発する）を示す。

```cm
struct Big {
    int[16384] data;      // 大きな固定長配列フィールド
    int tag;
}

Big a = make_big();
Big b = a;                // 現状: load %Big / store %Big で全要素SSA展開 → O2二次爆発
                          // 目標: サイズ閾値超のため CreateMemCpy 単発に
```

生成IRの意図する差分を示す（概念表現）。

```llvm
; 現状（第一級集約コピー、SROAが要素展開）
%tmp = load %Big, ptr %a
store %Big %tmp, ptr %b

; 目標（memcpy化、要素展開なし）
call void @llvm.memcpy.p0.p0.i64(ptr %b, ptr %a, i64 65540, i1 false)
```

M13は内部レイアウト計算の一本化であり、Cmソース上の構文例・出力例は無い（該当なし）。

## 実装の段階分割

- **Phase 1（C14）**: `assign.cpp`の`needsStructCopy`を拡張し、サイズ閾値超の通常構造体代入を`CreateMemCpy`化する。しきい値を導入し、小構造体は従来経路を維持する。
- **Phase 2（M13）**: `base.cpp`・`mono_structs.cpp`の手計算レイアウトを`layout_size`へ一本化して撤去する。デッドコードの地雷を除去する。
- **Phase 3（M12）**: 構造体代入のコピー省略を実装し、一時変数経由の二重コピーを単一コピーに畳み込む。
- **Phase 4（C14）**: 大きな構造体の関数引数・戻り値を第一級値経由から外す。（実装済み: 戻り値はsret変換、引数はDataLayout判定のポインタ渡し+呼び出し先エントリコピー。byval属性は呼び出し側コピーの意味論で本方式と不一致のため非適用）

Phase 1は最も被害の大きいコンパイル時間爆発を直接緩和するため先頭に置く。
Phase 2はデッドコード整理として独立に実施でき、後続の集約改修時の混乱を防ぐ。

## テスト計画（tests/common/配下）

- **C14回帰**: `int[16384]`等の大きな固定長配列フィールドを持つ構造体の代入・値渡しを含むプログラムを`tests/common/`に追加し、O2でのコンパイル時間・メモリが実用範囲に収まることを確認する。コピー後の全要素の値が正しいこと（memcpy化で内容が保存されること）をjit/native/wasmで検証する。
- **M12回帰**: 構造体代入の連鎖（`c = b = a`等）でコピー結果が正しいことと、生成IRで不要な二重コピーが畳み込まれることを確認する。
- **M13回帰**: Array・ネスト構造体・スライスをフィールドに持つ構造体で、サイズ・アライメント計算が`layout_size`と一致し、フィールドオフセットが正しいことを`tests/regression/`のMIR/レイアウト検査ケースで確認する。
- **値セマンティクス維持**: 小構造体・混在フィールド・構造体配列stride・union payloadのコピーが従来どおり正しいことを全バックエンドで対照し、memcpy化・byval/sret導入による退行が無いことを保証する（監査で健全と確認済みの領域を守る）。
- unit/regression層では、閾値超構造体代入のMIR/IRにmemcpyが現れ、閾値未満ではload/storeが維持されることをコード生成検査ケースで確認する。

## リスクと非互換性

- **しきい値の選定（C14）**: memcpy化の閾値が小さすぎると小構造体でmemcpy呼び出しオーバーヘッドが増え、大きすぎると爆発を防げない。代表的な構造体サイズで計測して決める。
- **byval/sret導入の呼び出し規約影響（C14）**: 引数・戻り値の渡し方を変えると、既存の集約渡し経路（メソッドの`self`渡し・戻り値）と整合を取る必要がある。監査で健全と確認済みの値セマンティクス（値渡し隔離・3段ネストコピー等）を壊さないよう、段階導入と広い回帰で守る。
- **コピー省略の正当性（M12）**: 一時経由を畳み込む際、コピー元がエイリアスや後続使用を持つ場合は省略できない。畳み込み可能条件を保守的に判定する。
- **レイアウト一本化の副作用（M13）**: 手計算は現状デッドコードだが、撤去時に間接的な参照が無いことを確認する。`layout_size`との一致は既存レイアウト前提のテストで担保する。
- **バックエンド差異**: memcpy化・byval/sretはLLVM系（jit/native/wasm）に対する改修であり、js/tsバックエンドは別経路のため対象外。バックエンド間の値セマンティクス一致を回帰で確認する。

## 関連

- `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（監査本体、テーマ5「集約を値として動かす経路の未成熟」）
- `src/internal/codegen/llvm/core/statement/assign.cpp`（構造体代入・memcpy発火条件`needsStructCopy`）
- `src/internal/codegen/llvm/core/operand.cpp`（構造体allocaのオペランド変換）
- `src/internal/mir/lowering/context.cpp`（`layout_size`、正しいレイアウト計算）
- `src/internal/mir/lowering/base.cpp` / `mono_structs.cpp`（撤去対象の手計算レイアウト）
