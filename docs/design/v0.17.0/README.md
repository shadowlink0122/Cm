---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書

大規模開発ボトルネック監査（[large-scale-bottleneck-audit.md](large-scale-bottleneck-audit.md)）で検出した所見に対する実装設計文書の索引。局所修正で対応済みの所見はリリースノート（`docs/releases/v0.17.0.md`）に記録し、残りの構造的リファクタリング項目を項目ごとの設計文書としてまとめている。

## 対応済み（リリースノート参照）

C1・C2・C3・C4・C5・C10・C11・C15・H13・M4・M5・M11・M16、およびスライス要素型ディスパッチの一元化は実装・テスト済み。

## 残りの設計文書（テーマ別）

### 型システム・ジェネリクス
- [型同一性の構造化（再帰的型キー）](type-identity-recursive-keys.md) — C7 / C8 / C9
- [マングリング衝突検出](mangling-collision-detection.md) — C16
- [ジェネリックインスタンス化の診断](generic-instantiation-diagnostics.md) — H15（個数検証はPhase 1実装済み）/ L8

### メモリ管理
- [drop パスと寿命管理](memory-drop-and-lifetime.md) — C12 / C13 / H12 / M15
- [アロケータと一時プール](allocator-and-temp-pool.md) — H11 / M14
- [集約コピーのlowering](aggregate-copy-lowering.md) — C14 / M12 / M13

### モジュール・可視性・静的解析
- [モジュール可視性とimport重複排除](module-visibility-and-import-dedup.md) — H7 / M7 / M2
- [確定代入とreturn網羅の解析](definite-assignment-and-correctness-lints.md) — H6 / L4
- [const集約の強制](const-aggregate-enforcement.md) — M3

### 値セマンティクス・バックエンド一貫性
- [複数キャプチャクロージャ](closures-multi-capture.md) — C6
- [集約に入るインターフェイス値](interface-values-in-aggregates.md) — H1 / H2
- [JS/TSの値セマンティクスとBigInt](js-ts-value-semantics.md) — H3 / H5
- [数値出力とキャストの一貫性](numeric-output-and-cast-consistency.md) — M8 / M9

### ランタイム・コレクション・文字列・ビルド
- [コレクションのOption返しAPIとエラー処理](collections-option-api-and-errors.md) — H8 / M17
- [文字列のUTF-8対応とStringBuilder](strings-utf8-and-stringbuilder.md) — H9
- [チェーンレシーバの解決](chain-receiver-resolution.md) — H10
- [インクリメンタルビルドと並列コード生成](incremental-build-and-parallel-codegen.md) — H14 / M6 / M10
- [境界チェックポリシー](bounds-checking-policy.md) — M1
- [その他の診断とLow優先度項目](misc-diagnostics-and-low-priority.md) — M18 / L1 / L2 / L3 / L5 / L6 / L7

## 着手順の推奨

型同一性の構造化（C7/C8/C9）はマングリング衝突（C16）・import重複排除（M7）と根が近く、最初のまとまった基盤リファクタリングとして費用対効果が高い。次いでdropパス（C12/C13）と集約コピー（C14）がコンパイル時間・メモリ実測値に直結する。
