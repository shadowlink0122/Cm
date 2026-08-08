---
title: 最適化パスの共有解析基盤（効果モデルの一元化）
parent: v0.17.0 Design
---

# 最適化パスの共有解析基盤（効果モデルの一元化）

## 概要

MIR最適化パス群（SCCP・ConstantFolding・GVN・CopyPropagation・DSE・DCE・LICM・ConstantLoopUnroll）は、「この文は何を書き換えるか・何を無効化すべきか」という効果情報をパスごとに独自実装しており、1つの意味論追加が全パスへの個別修正を要求する。
文の効果（書き込み先・クロバー範囲・最適化不可属性）を単一の共有モデルとして定義し、各パスはそれを消費するだけにする。

## 現状の実測と問題

- no_opt（mustブロック）の個別処理が8パスに分散し、B3ではCopyPropagation・SCCP・DSE等へ同じ「must書き込みの無効化」を並行追加する必要があった。
- グローバル/静的変数の呼び出し越しクロバーは、W4でLICM・ConstantFolding・SCCPの3パスへ別々のガードを追加して解消した（SCCPは楽観格子のUndefined初期化という別表現でも同じ知識を要求した）。GVN・DSE等の残パスに同種の穴が無いことは目視監査に依存している。
- detect_multi_assignedがfolding.cppとpropagation.cppへ同一実装で複製されている（ASM出力カウントの特例含む）。
- ASM文の出力オペランド無効化・Derefクロバー（全ローカル無効化）・Call終端の戻り先無効化も各パスの手書きで、書式だけ違う同じコードが繰り返される。

## 簡素化方針

1. 効果モデルAPIを新設する: `StmtEffects effects_of(const MirStatement&)` / `TermEffects effects_of(const MirTerminator&)`。内容は {書き込みLocal集合, クロバー種別(なし/Deref全域/Call時グローバル・静的), no_opt, ASM出力Local集合}。
2. 共有ユーティリティ: multi-assign検出・ブロックローカルの追跡表無効化（効果モデル適用）を1実装にし、folding/propagation/GVN/SCCPが共用する。
3. 「グローバル/静的はCallでクロバーされる」「no_opt文は読み書きとも最適化対象外」という意味論を効果モデルの1箇所にだけ書き、各パスの個別ガード（W4/B3で追加した分岐群）を共有実装参照へ置換する。
4. 新パス追加時のチェックリストを「効果モデルを消費しているか」の1点に還元し、W4型の「あるパスだけ意味論が抜ける」バグを構造的に防ぐ。
5. 収束マネージャ（既存のOptimizationPipeline）はそのまま使い、クエリ化・依存解析などの大掛かりな基盤刷新は非目標とする。

## 段階分割

1. 第1段: 効果モデルAPIと共有ユーティリティを新設し、folding/propagationのdetect_multi_assigned複製と無効化コードを置換する（挙動不変）。
2. 第2段: SCCP・GVN・DSE・DCE・LICM・const_unrollを効果モデル消費へ移行し、no_opt/グローバルガードの個別実装を削除する。
3. 第3段: 効果モデルのunitテスト（must/ASM/Deref/Call×グローバルの行列）を固定し、パス側の意味論テストを効果モデル側へ集約する。

## テスト計画

- 全12スイート+O0/O1検証を各段で完走させる（B3/W4/X1の回帰テストが特に重要）。
- unit: 効果モデルの文種別×属性の行列検証を追加する。
- 移行完了後、意図的に効果モデルへ新属性を追加した場合に全パスへ自動反映されることを、グローバルクロバーの派生ケース（静的変数・ポインタ経由）で確認する。

## 解決記録

- `src/internal/mir/passes/core/effects.hpp` を新設し、効果モデルを `StmtEffects effects_of(const MirStatement&)`（writes/direct_write/deref_clobber/no_opt/asm_outputs）と共有述語 `is_call_clobbered`（W4のCall越しグローバル・静的クロバー）・`is_externally_visible`（DCE/DSEの外部観測可能性。集合は同一だが意味が異なるため別名）として定義した。
- `detect_multi_assigned`（ASM出力の代入カウント特例含む）をfolding.cpp/propagation.cppの同一複製から効果モデルの共有実装へ統合し、両パスのメンバ実装を削除した。
- 8パスすべてを効果モデル消費へ移行した: folding/propagation（ASM出力除外・no_opt書き込み無効化・Derefクロバー・W4グローバルガード）、SCCP（楽観格子のOverdefined初期化2箇所・can_bind_constant・ASM出力の事前マーキングとtransfer/rewriteの3箇所）、GVN（書き込み無効化とDerefクロバー）、DSE（no_opt文の書き込み追跡除外）、DCE（グローバル書き込みの外部観測ガード）、LICM（ループ内変更ローカル収集とis_invariantのW4ガード）、const_unroll（グローバル誘導変数の展開禁止）。
- effects_ofのASM出力抽出は定数オペランド（i/n制約。local_idが0固定で無効）を除外する。従来この検査はLICMのみが持ち他7パスは欠落していたため、共有化で全パスに揃った。
- no_opt文の意味論は「値の置換・削除・並べ替えの対象外だが、書き込み効果は有効なまま消費する（B3）」として効果モデルの1箇所に固定した。
- unit: `tests/unit/mir_pass_test.cpp` に文種別×属性の行列（単純代入・投影付き代入・Deref代入・no_opt・ASM出力制約/入力制約/定数制約・複数回代入検出・グローバル/静的述語）を7テストとして追加した。
- 検証: unit・regression・must/global_var/static/asm回帰のO0/O1/O3突き合わせ・全12スイート完走。
