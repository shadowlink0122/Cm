# v0.17.0 実装済み設計文書アーカイブ（索引）

v0.17.0で処置が完了した設計文書をサブシステム別カテゴリに整理したもの。全文書は「設計方針・段階分割・実装記録・不採用判断・将来課題」を記録している。変更の要約はリリースノート（[docs/releases/v0.17.0.md](../../releases/v0.17.0.md)）、調査系列の索引は設計側README（[docs/design/v0.17.0/README.md](../../design/v0.17.0/README.md)）を参照。

## type-system/ — 型システム（型・ジェネリクス・モノモーフ化・型解決）

- [chain-receiver-resolution.md](type-system/chain-receiver-resolution.md) — チェーンレシーバ解決の任意式対応
- [generic-pointer-param-inference.md](type-system/generic-pointer-param-inference.md) — R3: ジェネリック`T*`引数の型パラメータ束縛失敗（swap等が全経路SIGSEGV）
- [generic-struct-literal.md](type-system/generic-struct-literal.md) — ジェネリック構造体リテラルの構築不能
- [monomorphization-typed-instantiation.md](type-system/monomorphization-typed-instantiation.md) — モノモーフ化の型駆動化（名前マングリング逆算の廃止）
- [nested-anonymous-struct-literal-loss.md](type-system/nested-anonymous-struct-literal-loss.md) — 構造体リテラル内配列の無名構造体リテラル喪失（W1）
- [nested-generic-type-arg-string.md](type-system/nested-generic-type-arg-string.md) — ネストしたジェネリック型引数のstringフィールド読みが無言死（Q2）
- [parenthesized-type-and-typeof-cast.md](type-system/parenthesized-type-and-typeof-cast.md) — 括弧付き型とtypeof型の静的解決（as/宣言/sizeofで具体型へ・JSキャスト解決）
- [type-identity-recursive-keys.md](type-system/type-identity-recursive-keys.md) — 型同一性の構造化（再帰的型キー）（実装済み）
- [typed-hir-single-source.md](type-system/typed-hir-single-source.md) — 型付きHIRの単一情報源化（下流での型再推論の禁止）
- [typedef-struct-literal-resolution.md](type-system/typedef-struct-literal-resolution.md) — B8: 構造体typedef別名のリテラル使用不可

## numeric/ — 数値変換・キャスト

- [cast-null-pointer-comparison.md](numeric/cast-null-pointer-comparison.md) — B5: キャスト付きnull比較が文字列比較になる
- [implicit-explicit-cast-design.md](numeric/implicit-explicit-cast-design.md) — 暗黙変換と明示キャストの設計整理（Z5）
- [int-literal-to-float-conversion.md](numeric/int-literal-to-float-conversion.md) — B2: 整数値→浮動小数文脈のビット再解釈
- [numeric-output-and-cast-consistency.md](numeric/numeric-output-and-cast-consistency.md) — 数値出力精度とfloat→intキャスト挙動のバックエンド統一（実装済み）
- [numeric-promotion-binary-ops.md](numeric/numeric-promotion-binary-ops.md) — int×double混合二項演算が不正IR・SIGBUSになる（Y4）

## memory/ — メモリ・drop・アロケータ・初期化

- [aggregate-copy-lowering.md](memory/aggregate-copy-lowering.md) — 集約コピーのmemcpy化とレイアウト計算の一本化
- [allocator-and-temp-pool.md](memory/allocator-and-temp-pool.md) — 解放可能なwasmアロケータとアロケータ差し替えの到達可能化
- [const-aggregate-enforcement.md](memory/const-aggregate-enforcement.md) — const集約への深い代入禁止（const構造体・const配列のフィールド/要素保護）
- [const-global-aggregate-init.md](memory/const-global-aggregate-init.md) — B1: const集約グローバルのrodata書き込み
- [memory-drop-and-lifetime.md](memory/memory-drop-and-lifetime.md) — 一時オブジェクトのdropパスとループ本体の寿命管理
- [static-block-scope-init-loss.md](memory/static-block-scope-init-loss.md) — ブロックスコープstatic変数の初期化子喪失とO1以上での値破壊（X1）
- [uninitialized-struct-fields.md](memory/uninitialized-struct-fields.md) — 未初期化構造体フィールドのゼロ初期化（実装済み）

## strings/ — 文字列・補間

- [interp-chain-lowering-failures.md](strings/interp-chain-lowering-failures.md) — 補間内チェーン式の誤lowering（クラッシュ・誤値・未解決シンボル）（W5）
- [interp-nested-slice-index.md](strings/interp-nested-slice-index.md) — 補間内の多次元スライス添字の誤読
- [interpolation-brace-from-runtime-value.md](strings/interpolation-brace-from-runtime-value.md) — R24: 文字列補間が実行時値に含まれる波括弧で破壊される（Debug出力等）
- [interpolation-format-backend-divergence.md](strings/interpolation-format-backend-divergence.md) — R20: 文字列補間・書式指定子のバックエンド分岐（単項~の補間破壊・幅/科学記法の不一致）
- [string-codepoint-byte-api-split.md](strings/string-codepoint-byte-api-split.md) — R2: 文字列APIのコードポイント/バイト単位不一致（len()とcharAt()の分裂・バックエンド差）
- [string-escape-and-raw-semantics.md](strings/string-escape-and-raw-semantics.md) — R5: 文字列エスケープの黙殺・raw文字列のエスケープ解釈・補間エスケープ不能
- [string-switch-miscompile.md](strings/string-switch-miscompile.md) — 文字列switchのLLVM検証失敗とjsの誤分岐
- [strings-utf8-and-stringbuilder.md](strings/strings-utf8-and-stringbuilder.md) — 文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入

## arrays-slices/ — 配列・スライス・ビット操作

- [array-builtin-elem-dispatch.md](arrays-slices/array-builtin-elem-dispatch.md) — 配列検索ビルトインの要素型ディスパッチ欠落（Z1）
- [array-literal-element-type-checking.md](arrays-slices/array-literal-element-type-checking.md) — 配列リテラル要素の型検査（多次元の内側要素まで再帰・void\*免除）
- [array-to-slice-elem-size.md](arrays-slices/array-to-slice-elem-size.md) — 固定長配列→スライス変換の手書き要素サイズ残存（Z2）
- [bounds-checking-policy.md](arrays-slices/bounds-checking-policy.md) — 配列・スライスの境界チェック統一ポリシー（実装済み）
- [fixed-array-to-slice-argument.md](arrays-slices/fixed-array-to-slice-argument.md) — 固定長配列→スライス引数の暗黙変換欠落でゴミ値（Y5）
- [generic-slice-element-garbage.md](arrays-slices/generic-slice-element-garbage.md) — ジェネリック関数のT[]要素読みがnative/jitでガベージを返す
- [multidim-partial-array-extraction.md](arrays-slices/multidim-partial-array-extraction.md) — 多次元配列の低次元部分配列返却（宣言パース・次元検査・要素コピー）
- [nested-member-slice-chain.md](arrays-slices/nested-member-slice-chain.md) — B4: ネストメンバスライスのチェーン変異でSIGSEGV
- [nested-slice-element-write-crash.md](arrays-slices/nested-slice-element-write-crash.md) — スライスofスライス要素への直接代入がSIGSEGV（W2）
- [slice-of-fixed-array-elements.md](arrays-slices/slice-of-fixed-array-elements.md) — スライスof固定長配列（int[2][]）の要素格納表現が未定義（Y6）
- [slice-push-anonymous-struct-literal.md](arrays-slices/slice-push-anonymous-struct-literal.md) — push(無名構造体リテラル)のフィールドずれ・喪失（X4）
- [slice-push-array-literal-corruption.md](arrays-slices/slice-push-array-literal-corruption.md) — push(配列リテラル)が壊れた要素をpushする（X3）
- [slice-struct-pop-value-crash.md](arrays-slices/slice-struct-pop-value-crash.md) — 構造体要素スライスのpop()戻り値受け取りがSIGSEGV（W3）

## enums-unions/ — enum・union

- [enum-inherent-impl-methods.md](enums-unions/enum-inherent-impl-methods.md) — enumへのinherent implメソッドが未サポート（Q5）
- [enum-multi-payload-match.md](enums-unions/enum-multi-payload-match.md) — enum複数ペイロードのmatch束縛不能とアリティ検査欠落
- [union-construction-sites.md](enums-unions/union-construction-sites.md) — ユニオン構築（タグ書き込み）の消費サイト欠落（Y1〜Y3）
- [wasm-union-in-struct-tag.md](enums-unions/wasm-union-in-struct-tag.md) — 構造体内ユニオンフィールドのタグがwasmで読めない（Z3）

## interfaces-derive/ — インターフェース・impl・derive・クロージャ・演算子

- [arith-operator-interface-decl.md](interfaces-derive/arith-operator-interface-decl.md) — 算術演算子インターフェースのimpl形が内部エラー（Q4）
- [closure-mutation-semantics.md](interfaces-derive/closure-mutation-semantics.md) — R4: クロージャの外側変数書き込みが黙殺・構造体キャプチャがjsだけ伝播
- [closures-multi-capture.md](interfaces-derive/closures-multi-capture.md) — 複数変数キャプチャ対応クロージャ（クロージャ環境化）（実装済み）
- [derive-as-source-expansion.md](interfaces-derive/derive-as-source-expansion.md) — 自動実装（with/derive）のソース展開化（手組みMIR生成の廃止）
- [derive-generic-and-field-gaps.md](interfaces-derive/derive-generic-and-field-gaps.md) — R21: derive/with自動実装のジェネリック型引数・フィールド型ギャップ（無言誤値・no-op・リンク失敗）
- [interface-bound-method-return-type.md](interfaces-derive/interface-bound-method-return-type.md) — B6: インターフェイス境界経由メソッド呼び出しの戻り値型誤り
- [interface-method-interpolation-type.md](interfaces-derive/interface-method-interpolation-type.md) — B7: インターフェイスメソッド戻り値の直接補間の型取り違え
- [interface-return-fat-pointer.md](interfaces-derive/interface-return-fat-pointer.md) — インターフェース戻り値のfat pointer構築欠落（Q3）
- [interface-values-in-aggregates.md](interfaces-derive/interface-values-in-aggregates.md) — 集約へのインターフェイス値格納（fat pointer構築の伝播）（実装済み）

## modules/ — モジュール・import・可視性・マングリング

- [mangling-collision-detection.md](modules/mangling-collision-detection.md) — マングリング名の衝突検出（実装済み）
- [module-system-structural-imports.md](modules/module-system-structural-imports.md) — モジュールシステムの構造化（テキストインライン展開の廃止）
- [module-visibility-and-import-dedup.md](modules/module-visibility-and-import-dedup.md) — モジュール可視性の強制と選択importの重複排除
- [private-field-access-unchecked.md](modules/private-field-access-unchecked.md) — privateフィールドの外部アクセスが無検査（X2）
- [private-method-cross-impl-visibility.md](modules/private-method-cross-impl-visibility.md) — privateメソッドが同一構造体の別implブロックから呼べる（X6）

## stdlib-runtime/ — 標準ライブラリ・ランタイム

- [collections-option-api-and-errors.md](stdlib-runtime/collections-option-api-and-errors.md) — コレクションのOption返しAPIとエラー型統合（実装済み）
- [concurrency-optimizer-and-join-gaps.md](stdlib-runtime/concurrency-optimizer-and-join-gaps.md) — R25: 並行処理の最適化・戻り値の穴（spin-waitがO1+でコンパイル不能・join戻り値のint32切り詰め）
- [cross-target-ffi-capability-gaps.md](stdlib-runtime/cross-target-ffi-capability-gaps.md) — R23: クロスターゲットFFIの能力ガード欠如（native専用モジュールがwasmへ黙ってコンパイル・js::timerコールバック型不能）
- [freestanding-nostd-enforcement-gaps.md](stdlib-runtime/freestanding-nostd-enforcement-gaps.md) — R18: フリースタンディング制約の強制漏れ（文字列連結のヒープ確保・関数ポインタ経由バイパス・float非対称）
- [hashmap-resize-loses-entries.md](stdlib-runtime/hashmap-resize-loses-entries.md) — HashMapが17要素以上で挿入済み要素を喪失（Q7）
- [json-parser-robustness.md](stdlib-runtime/json-parser-robustness.md) — R1: std::jsonパーサの堅牢性（アリーナ超過で無限ループ・甘い受理・\uエスケープ破壊）
- [runtime-builtin-registry.md](stdlib-runtime/runtime-builtin-registry.md) — ランタイムビルトインのレジストリ化（文字列名分散と多重宣言の解消）
- [runtime-hof-common-source.md](stdlib-runtime/runtime-hof-common-source.md) — 配列HOFランタイムの共通ソース化（format系の段階的単一化）
- [stdlib-shipping-defects.md](stdlib-runtime/stdlib-shipping-defects.md) — R9: stdlibの出荷不良（std::iterがコンパイル不能・コレクションのアロケータ素通し・std::io入力の再export解決不能）

## syntax-parse/ — 字句・構文・プリプロセッサ・属性・修飾子・文

- [attribute-validation-registry.md](syntax-parse/attribute-validation-registry.md) — R7: 属性の検証レジストリ（タイポ黙認によるテスト黙殺・未解釈属性の無警告受理・#[cfg]/#[target]の不活性）
- [default-arg-prev-param-zero.md](syntax-parse/default-arg-prev-param-zero.md) — R8: デフォルト引数での前引数参照が無診断でゼロ値になる
- [defer-implicit-function-end.md](syntax-parse/defer-implicit-function-end.md) — B9: 暗黙の関数終端でdeferが発火しない
- [export-on-impl-method-parse-error.md](syntax-parse/export-on-impl-method-parse-error.md) — R22: implブロック内メソッドの`export`修飾子がパースエラー（native::sync/io高レベルAPIが全損）
- [forin-iterator-protocol-checks.md](syntax-parse/forin-iterator-protocol-checks.md) — for-inイテレータプロトコルの検査穴（Q1）
- [match-pattern-and-flow-gaps.md](syntax-parse/match-pattern-and-flow-gaps.md) — R12: matchの負数パターン不可・網羅matchのreturn漏れ誤検知
- [modifier-implementation-gaps.md](syntax-parse/modifier-implementation-gaps.md) — R11: 修飾子の未実装・黙殺（constexpr・inline・volatile・ufloat/udouble）
- [must-block-field-assignment.md](syntax-parse/must-block-field-assignment.md) — B3: must{}内の構造体フィールド代入の誤コンパイル
- [preprocessor-conditional-robustness.md](syntax-parse/preprocessor-conditional-robustness.md) — R6: 条件付きコンパイルディレクティブの堅牢性（#endif非認識・閉じ忘れ黙殺・診断位置欠落）
- [syntax-error-diagnostic-quality.md](syntax-parse/syntax-error-diagnostic-quality.md) — R14: 構文・プリプロセッサ診断の品質（行番号欠落・"imported module"誤表記・誤誘導メッセージ）
- [syntax-error-position-and-token-display.md](syntax-parse/syntax-error-position-and-token-display.md) — 構文エラーの行番号がimport展開後の位置を指す（X5）
- [unimplemented-documented-syntax.md](syntax-parse/unimplemented-documented-syntax.md) — R13: 文法書・仕様書に定義があるが未実装の構文（実装かドキュメント削除かの判断待ち一覧）

## diagnostics/ — 診断エンジン・checker・lint

- [checker-error-coverage-holes.md](diagnostics/checker-error-coverage-holes.md) — 型検査のエラー検出漏れ3件（Z4）
- [checker-silent-holes.md](diagnostics/checker-silent-holes.md) — R10: 型検査の黙殺穴（未定義型の変数宣言・型不一致マクロ・const generic半実装）
- [definite-assignment-and-correctness-lints.md](diagnostics/definite-assignment-and-correctness-lints.md) — 確定代入解析とreturn網羅解析（正当性系lint）
- [diagnostics-engine-unification.md](diagnostics/diagnostics-engine-unification.md) — 診断エンジンの統一（発行・表示・エラー型隔離）
- [generic-instantiation-diagnostics.md](diagnostics/generic-instantiation-diagnostics.md) — ジェネリックインスタンス化の診断（実装済み）
- [misc-diagnostics-and-low-priority.md](diagnostics/misc-diagnostics-and-low-priority.md) — 診断・低優先度所見のまとめ

## optimizer-codegen/ — 最適化・LICM・レイアウト・インクリメンタルビルド

- [incremental-build-and-parallel-codegen.md](optimizer-codegen/incremental-build-and-parallel-codegen.md) — インクリメンタルビルド・並列コード生成・ICF・タイムアウトのプロセス分離
- [layout-query-unification.md](optimizer-codegen/layout-query-unification.md) — レイアウト計算の一元化（elem_size手書きスイッチの廃止）
- [layout-size-single-source.md](optimizer-codegen/layout-size-single-source.md) — 型サイズ照会の一本化（見積もり実装の削除）
- [licm-global-clobber-miscompile.md](optimizer-codegen/licm-global-clobber-miscompile.md) — LICMがグローバル変数を呼び出し越しに不変とみなすmiscompile（W4）
- [optimizer-shared-analysis.md](optimizer-codegen/optimizer-shared-analysis.md) — 最適化パスの共有解析基盤（効果モデルの一元化）

## backends/ — バックエンド固有（SV/wasm/js/ts/baremetal）・バックエンド分裂

- [01_js_npm_interop.md](backends/01_js_npm_interop.md) — 実装設計: JSバックエンドのnpmパッケージ連携（Phase 1）
- [baremetal-arm-startup-broken.md](backends/baremetal-arm-startup-broken.md) — R17: baremetal-armターゲットが起動コードのmemcpy型不一致で全滅
- [js-string-index-bigint.md](backends/js-string-index-bigint.md) — jsの文字列添字にlong型変数を使うとTypeError
- [js-ts-value-semantics.md](backends/js-ts-value-semantics.md) — JS/TSバックエンドの値セマンティクスと64bit整数表現の統一
- [negative-radix-format.md](backends/negative-radix-format.md) — 負数の基数書式指定子の型幅無視とバックエンド分裂
- [sv-codegen-silent-invalid.md](backends/sv-codegen-silent-invalid.md) — R16: SVコード生成が不正な構文を無診断で受理（bit[0]・桁あふれ・入力ポート書込み・always_ff黙殺・属性タイポ）
- [sv-test-verification-soundness.md](backends/sv-test-verification-soundness.md) — R15: SVテスト検証の健全性（`//! test:`期待値が非検証・assertのx楽観性）
- [ts-bigint-number-generation.md](backends/ts-bigint-number-generation.md) — R19: TS出力がlong/ulongフィールドへnumberリテラルを代入しtscを通らない
- [wasm-reduce-closure-trap.md](backends/wasm-reduce-closure-trap.md) — wasmのreduceクロージャのランタイムトラップ

## sv/ — SVバックエンドの新規実装項目（SVギャップ調査）

- [reduction-operators.md](sv/reduction-operators.md) — SV-N2: リダクション演算子の組み込み関数（reduce_and/or/xor/nand/nor/xnor。SVはnative単項演算子・非SVは算術脱糖）
- [casez-casex-priority.md](sv/casez-casex-priority.md) — SV-N3: don't-care matchのnative casez出力とcase修飾属性（#[sv::priority]/#[sv::unique0]）
- [native-bit-part-select.md](sv/native-bit-part-select.md) — SV-N1: ビットスライスのnative part-select出力（[hi:lo]/[+:]/[-:]・左辺part-select代入）と下降方向`-:`新構文・bit基点許容
- [misc-synth-gaps.md](sv/misc-synth-gaps.md) — SV-N8: 小粒ギャップ集（$readmemb・struct型名キャスト・#[sv::unpacked]を実装、SV task/$time/final/native reg・2-state bitは見送り/現状維持の判断を記録）

## architecture/ — コンパイラ基盤の構造的リファクタリング

- [compiler-architecture-restructure.md](architecture/compiler-architecture-restructure.md) — コンパイラ全体構成の再編（ステージ分離とサブコマンドの切り分け）
- [type-resolution-simplification.md](architecture/type-resolution-simplification.md) — 型解決とチェーンloweringの単純化（重複実装の統合）

## audit/ — 監査・網羅検証

- [large-scale-bottleneck-audit.md](audit/large-scale-bottleneck-audit.md) — Cm大規模開発ボトルネック監査
- [move-closure-interp-audit.md](audit/move-closure-interp-audit.md) — native/jit網羅検証で検出したバグ（move・クロージャ・補間式添字）
- [syntax-audit-bugfixes.md](audit/syntax-audit-bugfixes.md) — 構文網羅検証で検出したバグの修正（索引）

## tooling/ — ツール（VSCode拡張・セルフホスト準備）

- [self-hosting-preparation.md](tooling/self-hosting-preparation.md) — セルフホスト準備（OS連携APIの整備）
- [vscode-code-navigation.md](tooling/vscode-code-navigation.md) — VSCode拡張のコードナビゲーション（ホバー・定義ジャンプ・アウトライン）
