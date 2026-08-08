---
title: 配列HOFランタイムの共通ソース化（format系の段階的単一化）
parent: v0.17.0 Design
---

# 配列HOFランタイムの共通ソース化（format系の段階的単一化）

## 概要

native/runtime_format.c（3,058行）とwasm/runtime_format.c（2,888行）は関数名の約79%を共有するが本体の一致は35〜40%で、新規ビルトイン追加のたびに両ファイルへの二重実装とレジストリのソート維持が必要になっている（Z1実装時の実痛点）。
スライス系はruntime_slice_core.inc（1,147行）+5フック（CM_RT_ALLOC/FREE/MEMCPY/MEMMOVE/GROW）方式で共通ソース化が完了済みであり、同方式を適用できる残り領域を実測で切り分けた。

- **共通化の機が熟している**: 配列HOF/検索ビルトイン群。CM_HOF_UNWRAPは現在両ファイルに同一定義があり（native:1382・wasm:2219、40箇所同一使用）、ストライド処理も既にバイト一致（native:2054-2066＝wasm:2323-2335）。native-onlyのevery/some/includes全幅変種はwasmへの単純コピー＝重複追加待ちの状態。
- **共通化を急がない**: 文字列フォーマット系（約85関数）。nativeはSDSヘッダ+最短round-trip dtoa、wasmはwasm_int_to_str/wasm_dtoa_prec独自層で、実装が根本的に異なる。runtime-builtin-registryの「wasm文字列基盤のSDS化まで非対象」の判断は実測でも妥当。

## リファクタリング方針

1. **第1段**: 配列HOF/検索ビルトイン群（__builtin_array_every/some/includes/find/first/last/index/forEach/reduce/reverse/slice/sortの全幅+str+closure変種）をruntime_format_core.inc（仮称）へ抽出し、slice方式の5フック+CM_HOF_UNWRAPで両ターゲットから包含する。native-only変種はこの時点でwasmにも有効化される。
2. **第2段**: check_builtin_signatures.py（lint/CI済み）に「共通.incに定義があるのに片側の.cへ重複定義がある」検査を追加し、二重実装の再発を機械的に防ぐ。
3. **将来（本文書の対象外）**: wasm文字列基盤のSDS化が完了した時点でフォーマット系の共通化を別文書で再検討する。

## テスト計画

- HOF/検索ビルトインの既存回帰（Z1で追加した全幅+strマトリクス）をnative/jit/wasmの3系で。wasm側に新規有効化されるevery/some/includes変種の値検証を追加する。

## 検出経緯

全体複雑度レビュー（2026-08-05）で両ランタイムをdiff実測し、slice方式の適用可能領域を切り分けた。

## 実装記録（修正済み・第1段+第2段完遂）

1. **第1段**: `__builtin_array_*`全54関数（slice/HOF/検索・全幅+str+closure変種）とヘルパー（CmHofSlice/CM_HOF_UNWRAP/CmSlice_fmt/compare_i32/64）を`src/internal/codegen/common/runtime_hof_core.inc`（826行）へ抽出し、slice方式のフック（CM_RT_ALLOC/FREE/MEMCPY）で両targetから包含した。native/runtime_format.cは3,058→2,255行、wasmは2,888→2,129行。ソートはcm_qsort（両target定義済み）へ統一し、文字列比較はstrcmp（wasmは自前定義がinclude位置より前にある）を使う。CMakeの両ランタイムビルドDEPENDSへ.incを追加した。
2. **抽出時に確定した実装差の解消**: 文書起票時にnative-onlyだったevery/some/includes変種はZ1で既に両targetへ追加済みで名前集合は一致していた（新規有効化はなし）。本体差分31関数は表記（bool/_Bool・波括弧）とフック差が主で、実質差は3点を統一で解消した——wasm sort_i32/i64のalloc失敗時のスライスヘッダリーク（nativeの解放パスを採用）、wasmの自前挿入ソート（cm_qsortへ統一・int配列で結果同一）、nativeの委譲ラッパの冗長CM_HOF_UNWRAP（無害・native形を維持）。
3. **第2段**: `scripts/check_builtin_signatures.py`へ「共通.incに定義があるのにプラットフォーム.cへ重複定義がある」検査を追加した（共通コア重複検査111件・make lint/CIで常時実行）。ネガティブテスト（重複定義の一時追加）で検出を確認済み。
4. **検証**: array/array_higher_order/dynamic_arrayスイートをjit/llvm/llvm-wasmで全通過、make test全スイート緑。ユーザー可視の挙動変更はなし（純内部統合）。
5. 文字列フォーマット系（約85関数）はwasm SDS化まで非対象の既存判断を維持する（方針3のとおり将来の別文書）。
