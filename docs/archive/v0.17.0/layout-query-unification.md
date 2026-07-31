---
title: レイアウト計算の一元化（elem_size手書きスイッチの廃止）
parent: v0.17.0 Design
---

# レイアウト計算の一元化（elem_size手書きスイッチの廃止）

## 概要

型のサイズ・要素ストライド・整列の計算が、MIR lowering・LLVM codegen各所の手書きswitchとして分散している。
「型→サイズ/ストライド」の問い合わせを単一のレイアウトAPIへ集約し、手書きswitchを削除する。
rustcのlayout query（ty::layout_of一箇所で全レイアウトを決定）に相当する規律で、過去のwasm32ポインタ幅バグ（cm_array_to_sliceのelem_size=8固定）やC14のisSmallStruct誤計算（ArrayをDataLayoutで引かず8byte扱い）はこの分散が原因だった。

## 現状の実測と問題

- `elem_size = 4`/`elem_size = 8` の手書き代入が13箇所（construct.cpp 5・let.cpp 6・impl.cpp 1・stmt/control.cpp 1）にあり、それぞれがスカラ幅・ポインタ幅（target_pointer_size）・構造体blob（layout_size）・ネストスライス（sizeof(void*)*4=32）の選択switchを複製している。
- LLVM側にも同型のswitch（translate/function.cppのスライスalloca初期化）が別実装であり、MIR側と一致する保証は目視レビューのみ。
- CmSliceヘッダサイズが `sizeof(void*) * 4`（ホストの8byte前提で32）というマジック値として複数箇所に埋まっており、ターゲット（wasm32）ではランタイム構造体と意味がずれる余地がある。
- slice_dispatch.hppのslice_scalar_infoがスカラ幅を一元化した（C4）が、非スカラ（ポインタ/構造体/ネストスライス）の分岐は依然各サイトの手書きで、X3修正でも同じswitchを新規に1つ書く必要があった。

## 簡素化方針

1. レイアウトAPIを1箇所に定義する: `cm::layout::size_of(type, target)` / `stride_of(elem_type, target)` / `align_of(type, target)` / `slice_header_size(target)`。実装はslice_scalar_info・ctx.layout_size・target_pointer_size()を統合し、ターゲット（native/wasm32）をパラメータ化する。
2. MIR loweringの13箇所とLLVM側のswitchをすべてAPI呼び出しへ置換し、サイズ選択ロジックの新規手書きをレビュー規約で禁止する。
3. CmSliceヘッダサイズはランタイム構造体定義から導出した定数をAPIが返す形にし、マジック値`32`/`sizeof(void*)*4`を排除する。
4. LLVM DataLayoutとの整合はAPIの実装内で一括検証する（debugビルドでDataLayout計算と突き合わせるアサート）。

## type-resolution-simplification.md（領域4）との関係

領域4はスライスビルトインの「関数選択・呼び出し規約」の表化で、本文書は「サイズ・ストライドの数値決定」を扱う。
両者は独立に実施できるが、slice_dispatch表の各行が本APIのstride_ofを参照する形で合流する。

## 段階分割

1. 第1段: レイアウトAPI新設と、MIR lowering 13箇所の置換（挙動不変。既存値との一致をアサートで確認しながら移行）。
2. 第2段: LLVM側switchの置換とDataLayout突き合わせアサートの導入。
3. 第3段: マジック値の排除とwasm32での全スイート検証（ポインタ幅・ヘッダサイズの経路差が消えることの確認）。

## テスト計画

- 全12スイート+O0検証（wasmスイート含む）を各段で完走させる。
- unit: レイアウトAPIの型別期待値（スカラ全種・ポインタ・構造体・ネストスライス、native/wasm32両ターゲット）を固定する。
- 既知バグの回帰（wasm32のstring[2]→string[]変換・int[16384]のsret判定）が新API経由で維持されることを確認する。

## 解決記録

- `src/internal/mir/lowering/layout.hpp` を新設し、レイアウト問い合わせを `cm::mir::layout` 名前空間へ集約した。
- APIは2つのストライド意味論を明示的に分離した: `slice_elem_stride`（cm_slice_newへ渡す格納ストライド。ポインタ・文字列はランタイムスロット規約の8固定）と `array_elem_stride`（cm_array_to_sliceのmemcpy基準の実ストライド。ポインタ・文字列はtarget_pointer_size()でwasm32=4）。単一関数に統合するとwasm32の既知バグ（elem_size=8固定による範囲外読み）を再導入するため、意味論ごとに関数を分けた。
- コア選択はテンプレート関数 `slice_elem_stride_of` / `array_elem_stride_of`（集約サイズ計算をAggregateSizeFnで注入）とし、MIR側はLoweringContext版ラッパ（typedef解決+ctx.layout_size）、LLVM側はDataLayout::getTypeAllocSizeのラムダで同一コアを共有する。これによりMIR/LLVM間の一致が構造的に保証され、目視レビュー依存が解消された。
- MIR loweringの手書きswitchを全廃した: let.cpp 4箇所（スライス格納2・配列実2）・construct.cpp 2箇所・impl.cpp 1箇所・stmt/control.cpp 1箇所。計測時13箇所から並行の型解決簡素化（スライスディスパッチ表化）で吸収済みのサイトを除いた8箇所が対象だった。
- LLVM側 `translate/function.cpp` のスライスalloca初期化switch 2箇所も置換した。この置換で従来欠落していた選択（ローカル側: tiny/utiny→1、構造体フィールド側: tiny/utiny→1と内側スライス→32）が自然に補完された。let.cpp:454相当とimpl.cpp:301相当の配列実ストライドにおけるStruct/Unionケース欠落（潜在バグ）も同様にAPI適用で補完された。
- CmSliceヘッダサイズは `layout::slice_header_size()`（sizeof(void*)*4）へ一元化し、各サイトのマジック値32を排除した。
- unit: `tests/unit/layout_test.cpp` で型別期待値（スカラ全種・ポインタ・文字列・集約・内側スライス・null）とwasm32分岐（配列実ストライドのみ4へ追従し、スライス格納は8固定のまま）を固定した（6テスト）。
- 検証: unit・regression全通過、スライス系代表24ケースをnative/wasm32×O0/O3で突き合わせ全一致、全12スイート完走。
