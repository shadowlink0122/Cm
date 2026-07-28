---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書

大規模開発ボトルネック監査（[large-scale-bottleneck-audit.md](large-scale-bottleneck-audit.md)）で検出した所見に対する実装設計文書の索引。局所修正で対応済みの所見はリリースノート（`docs/releases/v0.17.0.md`）に記録し、残りの構造的リファクタリング項目を項目ごとの設計文書としてまとめている。

## 対応済み（リリースノート参照）

C1・C2・C3・C4・C5・C6・C7・C8・C9・C10・C11・C12(文字列・スライス一時)・C13・C14(Phase1)・C15・C16・M2・M6・M7・H1・H2・H3・H4・H8・H11・H13・H15・L8・M4・M5・M1・M8・M9・M11・M15・M16・M17・M18・L1・L2・L3・L6(assert_eq)、およびスライス要素型ディスパッチの一元化は実装・テスト済み。実装完了した設計文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（H4: uninitialized-struct-fields、C7/C8/C9: type-identity-recursive-keys、C16: mangling-collision-detection、H15/L8: generic-instantiation-diagnostics、M8/M9: numeric-output-and-cast-consistency、H8/M17: collections-option-api-and-errors、M1: bounds-checking-policy、C6: closures-multi-capture、H1/H2: interface-values-in-aggregates）。

## 残りの設計文書（テーマ別）

### 型システム・ジェネリクス
（すべて実装済み・archiveへ移動）

### メモリ管理
- [drop パスと寿命管理](memory-drop-and-lifetime.md) — C12 / C13 / H12 / M15
- [アロケータと一時プール](allocator-and-temp-pool.md) — H11 / M14
- [集約コピーのlowering](../../archive/v0.17.0/aggregate-copy-lowering.md) — C14 / M12 / M13（全Phase実装済み・archiveへ移動）

### モジュール・可視性・静的解析
- [モジュール可視性とimport重複排除](module-visibility-and-import-dedup.md) — H7 / M7（実装済み） / M2（実装済み）
- [確定代入とreturn網羅の解析](definite-assignment-and-correctness-lints.md) — H6 / L4
- [const集約の強制](const-aggregate-enforcement.md) — M3（警告として段階導入済み・エラー化は将来）

### 値セマンティクス・バックエンド一貫性
- [JS/TSの値セマンティクスとBigInt](js-ts-value-semantics.md) — H3（実装済み） / H5

### ランタイム・コレクション・文字列・ビルド
- [文字列のUTF-8対応とStringBuilder](strings-utf8-and-stringbuilder.md) — H9
- [チェーンレシーバの解決](chain-receiver-resolution.md) — H10
- [インクリメンタルビルドと並列コード生成](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md) — H14 / M6 / M10（全項目実装済み・archiveへ移動。CM_MODULE_CODEGENの既定有効化はsoak後判断）
- [その他の診断とLow優先度項目](../../archive/v0.17.0/misc-diagnostics-and-low-priority.md) — M18 / L1 / L2 / L3 / L5 / L6 / L7（全項目実装済み・archiveへ移動）

## 着手順の推奨

型システム・ジェネリクス群（C7/C8/C9・C16・H15/L8）とdropパス（C12の文字列一時/C13）・集約コピー（C14 Phase1）は実装済み。クロージャ環境化（C6）も実装済み。インターフェイス値の集約格納（H1/H2）も実装済み。残りはH12のフロー解析化・M12/M13（byval/sret）・JS値セマンティクス（H3/H5）・文字列基盤（H9）・ビルド基盤（H14）等の構造的リファクタリングが中心となる。
