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
