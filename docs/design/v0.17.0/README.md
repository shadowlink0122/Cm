---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書

大規模開発ボトルネック監査（[large-scale-bottleneck-audit.md](../../archive/v0.17.0/large-scale-bottleneck-audit.md)、全57所見対応完了・archiveへ移動）で検出した所見に対する実装設計文書の索引。局所修正で対応済みの所見はリリースノート（`docs/releases/v0.17.0.md`）に記録し、残りの構造的リファクタリング項目を項目ごとの設計文書としてまとめている。

## 対応済み（リリースノート参照）

C1・C2・C3・C4・C5・C6・C7・C8・C9・C10・C11・C12(文字列・スライス一時)・C13・C14(Phase1)・C15・C16・M2・M6・M7・H1・H2・H3・H4・H8・H11・H13・H15・L8・M4・M5・M1・M8・M9・M11・M15・M16・M17・M18・L1・L2・L3・L6(assert_eq)、およびスライス要素型ディスパッチの一元化は実装・テスト済み。実装完了した設計文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（H4: uninitialized-struct-fields、C7/C8/C9: type-identity-recursive-keys、C16: mangling-collision-detection、H15/L8: generic-instantiation-diagnostics、M8/M9: numeric-output-and-cast-consistency、H8/M17: collections-option-api-and-errors、M1: bounds-checking-policy、C6: closures-multi-capture、H1/H2: interface-values-in-aggregates）。

## 残りの設計文書（テーマ別）

### 型システム・ジェネリクス
（すべて実装済み・archiveへ移動）

### メモリ管理
- [drop パスと寿命管理](../../archive/v0.17.0/memory-drop-and-lifetime.md) — C12 / C13 / H12 / M15（全項目実装済み・archiveへ移動。C12のO0再代入解放とH12線形フロー診断で完結、厳密CFG化は将来課題として記録）
- [アロケータと一時プール](../../archive/v0.17.0/allocator-and-temp-pool.md) — H11 / M14（全項目実装済み・archiveへ移動。set_allocator_fnsファサードとcm_mem_*経路一本化で完結）
- [集約コピーのlowering](../../archive/v0.17.0/aggregate-copy-lowering.md) — C14 / M12 / M13（全Phase実装済み・archiveへ移動）

### モジュール・可視性・静的解析
- [モジュール可視性とimport重複排除](../../archive/v0.17.0/module-visibility-and-import-dedup.md) — H7 / M7 / M2（全段階実装済み・archiveへ移動。段階4は生ソース収集+パス鍵伝搬の改名方式で完結、エラー化は将来として記録）
- [確定代入とreturn網羅の解析](../../archive/v0.17.0/definite-assignment-and-correctness-lints.md) — H6 / L4（段階1-3実装済み・archiveへ移動。--strictでエラー昇格、恒真条件枠とCFG精密化は将来課題として記録）
- [const集約の強制](../../archive/v0.17.0/const-aggregate-enforcement.md) — M3（段階1-4実装済み・archiveへ移動。フィールド/要素代入とconstポインタ化の警告まで完了、エラー化は将来として記録）

### 値セマンティクス・バックエンド一貫性
- [JS/TSの値セマンティクスとBigInt](../../archive/v0.17.0/js-ts-value-semantics.md) — H3 / H5（全項目実装済み・archiveへ移動。BigInt表現移行で5系一致）

### セルフホスト準備
- [セルフホスト準備（OS連携APIの整備）](../../archive/v0.17.0/self-hosting-preparation.md) — S1〜S9（全4段実装済み・archiveへ移動。env/process/path/bytes/split・ランタイムシム（readdir/バイナリI/O/current_exe）・argv対応・セルフホスト素振りexampleのCI通し検証で完結）

### ランタイム・コレクション・文字列・ビルド
- [文字列のUTF-8対応とStringBuilder](../../archive/v0.17.0/strings-utf8-and-stringbuilder.md) — H9（全5段実装済み・archiveへ移動。第4段はchar*互換のSDSヘッダ方式でO(1)バイト長・埋め込みNUL・from_bytesを実現）
- [チェーンレシーバの解決](../../archive/v0.17.0/chain-receiver-resolution.md) — H10（全5段実装済み・archiveへ移動。添字レシーバ・混合チェーン・多重添字読みで完結）
- [インクリメンタルビルドと並列コード生成](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md) — H14 / M6 / M10（全項目実装済み・archiveへ移動。CM_MODULE_CODEGENの既定有効化はsoak後判断）
- [その他の診断とLow優先度項目](../../archive/v0.17.0/misc-diagnostics-and-low-priority.md) — M18 / L1 / L2 / L3 / L5 / L6 / L7（全項目実装済み・archiveへ移動）

## 状態

v0.17.0の設計文書は監査全57所見の対応とセルフホスト準備（S1〜S9）を含め全て実装完了し、[archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み。各文書に将来課題（エラー化・CM_MODULE_CODEGEN既定有効化等）を記録している。セルフホスト本体（CmコンパイラのCm実装）は1.0以降に別設計文書で扱う。
