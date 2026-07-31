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
