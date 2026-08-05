---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書（索引）

v0.17.0の設計文書は下記「レイヤー別レビュー 第4ラウンド」の未修正所見（Z5）と「第5ラウンド」の新規所見（Q1〜Q5・Q7）を除き全件の処置が完了し、実装済み文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動した（本READMEは索引として残る）。
各文書には設計方針・段階分割・実装記録・不採用判断・将来課題を記録している。
変更の要約はリリースノート（[docs/releases/v0.17.0.md](../../releases/v0.17.0.md)）を参照。

## レイヤー別レビュー 第4ラウンド（未修正の新規所見 Y1〜Y6）

全修正完了後のフロント〜コード生成レイヤー別レビュー（ユニオン・リテラル型・const伝搬・戻り値解決・配列/スライス境界・型昇格の差分検証）で検出。バグ1項目につき1文書。

- Y1〜Y3: ユニオン構築（タグ書き込み）の消費サイト欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/union-construction-sites.md)。coerce_to_union共通ヘルパをreturn・フィールド・push・リテラル要素・引数/デフォルト引数へ適用。混在変種の三項/match腕は残課題として記録）
- Y4: int×double混合二項演算が不正IR・SIGBUS — **修正済み**（[archive移動](../../archive/v0.17.0/numeric-promotion-binary-ops.md)。infer_binaryの昇格Cast挿入+MIR防衛層+CANONICAL_SPEC 10.2明文化）
- Y5: 固定長配列→スライス引数の暗黙変換欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/fixed-array-to-slice-argument.md)。coerce_fixed_array_to_sliceを引数/デフォルト引数へ適用・decay抑止・コピー意味論をチュートリアル明文化）
- Y6: スライスof固定長配列の要素格納表現が未定義 — **修正済み**（[archive移動](../../archive/v0.17.0/slice-of-fixed-array-elements.md)。N×実ストライドのインラインblobに仕様確定・dispatch/layout/codegen/letの4系統統一。jsのblob意味論は将来課題）

### 第4ラウンド追補: ユニオン・文字列要素の配列/スライス整合性（Z1〜Z3）

要素サイズが型依存（ポインタ幅・タグ付き・可変ペイロード）の配列/スライスについて、サイズ決定サイトと実行の整合性を調査した。基本レイアウト（string固定長配列の全操作・ユニオン固定長配列の読み書き・大型構造体バリアント・構造体内string配列）はnative/jit/wasmで整合を確認済み。

- Z1: 配列検索ビルトインの要素型ディスパッチ欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/array-builtin-elem-dispatch.md)。値比較系の全幅+str変種を両ランタイムへ追加・js緩い等価対応・未対応要素の診断化）
- Z2: 固定長配列→スライス変換の手書き要素サイズ残存 — **修正済み**（[archive移動](../../archive/v0.17.0/array-to-slice-elem-size.md)。要素サイズ決定をMIRのlayout API 1系統へ統一・死コード削除）
- Z3: 構造体内ユニオンフィールドのタグがwasmで読めない — **解決確認**（[archive移動](../../archive/v0.17.0/wasm-union-in-struct-tag.md)。バイセクトでY1〜Y3のタグ書き込み統一が真因と特定——nativeは偶然一致だった。マトリクス回帰を4系一致で追加）
- Z4: 型検査のエラー検出漏れ3件 — **修正済み**（[archive移動](../../archive/v0.17.0/checker-error-coverage-holes.md)。push要素型検査・非変種as拒否・ループ深度によるbreak/continue診断、エラーテスト4本追加）
- [Z5: 暗黙変換と明示キャストの設計整理](implicit-explicit-cast-design.md) — 縮小変換が全て暗黙受理され、double→int暗黙代入は変換命令欠落でゴミ値・バックエンド分裂。stdlibの`as`127箇所の一部はY4の回避策。conversion_kind一元化と段階的エラー化の方針（Critical）

## 未修正バグ調査 第5ラウンド（Q1〜Q7）

過去ラウンドで未検証だった領域（複雑左辺値の複合代入・for-inイテレータ・Try演算子・グローバル初期化・ポインタ演算・複数型引数ジェネリクス・インターフェース戻り値・演算子オーバーロード・match式・defer・文字列/enumメソッド・ビット演算・HashMap負荷）を6経路差分（jit O0/O2・native O0〜O3）で調査した。バグ1項目につき1文書。
健全確認済み: 複合代入/inc-decの複雑左辺値・for-in（スライス/固定長配列）・Try連鎖・グローバル依存初期化・ポインタ演算stride・inherent演算子オーバーロード・match式全値位置・defer順序/キャプチャ・文字列メソッド群・has_next形イテレータ・ビット/char演算・sizeof。

- [Q2: ネストしたジェネリック型引数のstringフィールド読みが無言死](nested-generic-type-arg-string.md) — `Pair<Box<int>, Box<string>>`経由のstring読みでrc=0のまま無言終了（全バックエンド・checker無診断。Critical）
- [Q3: インターフェース戻り値のfat pointer構築欠落](interface-return-fat-pointer.md) — 戻り値経由のメソッド呼び出しがjit=ゴミ値/native=誤値（ローカルupcast・引数は正常。returnサイト欠落ファミリ。Critical）
- [Q7: HashMapが17要素以上で挿入済み要素を喪失](hashmap-resize-loses-entries.md) — 容量16境界のリサイズで要素喪失、getがNone（stdlib。Critical）
- [Q1: for-inイテレータプロトコルの検査穴](forin-iterator-protocol-checks.md) — has_next欠如が未解決シンボルまで無診断・Option返しnextの要素型未unwrap（Medium）
- [Q4: 算術演算子インターフェースのimpl形が内部エラー](arith-operator-interface-decl.md) — `impl T for Add`が内部エラー（Add系未宣言+例外漏れ。inherent形は正常。Medium）
- [Q5: enumへのinherent implメソッドが未サポート](enum-inherent-impl-methods.md) — impl宣言は黙って受理され呼び出しで「Unknown method for type 'int'」（Medium）
- Q6（文書化なし・注記のみ）: `replace()`が最初の一致のみ置換する仕様がドキュメント未記載（全置換との区別を文字列チュートリアルへ明記すべき。Low）

## コンパイラ基盤の構造的リファクタリング

大規模開発ボトルネック監査と修正履歴の原因分析に基づき、同族バグの再発を構造的に防ぐ再編を実施した。

- [compiler-architecture-restructure.md](../../archive/v0.17.0/compiler-architecture-restructure.md) — 12層のinclude依存規律のlint/CI強制・run_frontend共有化・optionsテーブル化（物理分離とLint分離は実測に基づく不採用判断）
- [module-system-structural-imports.md](../../archive/v0.17.0/module-system-structural-imports.md) — importのテキストインライン展開を廃止し、モジュールグラフ+AST駆動の選択的包含へ全面移行（テキスト展開系約2,800行を削除）
- [type-resolution-simplification.md](../../archive/v0.17.0/type-resolution-simplification.md) — 場所解決のlower_place一本化・スライスビルトイン表引き化・期待型伝播の正式API化・補間のパース時脱糖とミニパイプライン完全削除
- [typed-hir-single-source.md](../../archive/v0.17.0/typed-hir-single-source.md) — 「型検査後のHIRは全式が型付き」不変条件の機械的検証と違反6クラスの上流修正（物理的単一walk化は不採用判断）
- [monomorphization-typed-instantiation.md](../../archive/v0.17.0/monomorphization-typed-instantiation.md) — 特殊化の同定・書き換えの型ノード駆動化と名前マングリング逆算の廃止・無置換特殊化の常時検査
- [diagnostics-engine-unification.md](../../archive/v0.17.0/diagnostics-engine-unification.md) — DiagnosticEmitter表示一元化・MIRエラーの診断昇格・`__error__`成果物検査・診断207呼び出しの完全i18n化
- [runtime-builtin-registry.md](../../archive/v0.17.0/runtime-builtin-registry.md) — ビルトイン188件のレジストリ表・シグネチャ乖離のlint/CI検査・slice系ランタイムの共通ソース化
- [derive-as-source-expansion.md](../../archive/v0.17.0/derive-as-source-expansion.md) — with/derive自動実装のCmソース合成化と死んだ生成器約1,700行の削除
- [layout-query-unification.md](../../archive/v0.17.0/layout-query-unification.md) — 型→要素ストライドの2意味論API集約（elem_size手書きスイッチ10箇所の置換）
- [optimizer-shared-analysis.md](../../archive/v0.17.0/optimizer-shared-analysis.md) — 最適化8パスの効果モデル（effects.hpp）一元化
- [type-identity-recursive-keys.md](../../archive/v0.17.0/type-identity-recursive-keys.md) — ジェネリック特殊化の型同一性の構造化と可逆型キーtypekey（C7/C8/C9）

## 機能テーマ別の設計文書

- [strings-utf8-and-stringbuilder.md](../../archive/v0.17.0/strings-utf8-and-stringbuilder.md) — 文字列基盤の刷新全5段（StringBuilder・UTF-8 len・SDSヘッダ・連結チェーン、H9）
- [memory-drop-and-lifetime.md](../../archive/v0.17.0/memory-drop-and-lifetime.md) — 一時オブジェクトのdropパス全系統とループRAII（C12/C13/M15/H12）
- [allocator-and-temp-pool.md](../../archive/v0.17.0/allocator-and-temp-pool.md) — wasmフリーリストアロケータとアロケータ差し替えの到達可能化（H11/M14）
- [aggregate-copy-lowering.md](../../archive/v0.17.0/aggregate-copy-lowering.md) — 大構造体のmemcpy化・sret化と値渡し隔離（C14全Phase）
- [incremental-build-and-parallel-codegen.md](../../archive/v0.17.0/incremental-build-and-parallel-codegen.md) — コード生成のfork分離・モジュール別キャッシュ・並列化・同一コード折り畳み（M6/H14/M10）
- [module-visibility-and-import-dedup.md](../../archive/v0.17.0/module-visibility-and-import-dedup.md) — モジュール可視性の段階的強制とimport重複排除（H7/M2/M7）
- [collections-option-api-and-errors.md](../../archive/v0.17.0/collections-option-api-and-errors.md) — マップのOption返しAPIとエラー型の統合（H8/M17）
- [const-aggregate-enforcement.md](../../archive/v0.17.0/const-aggregate-enforcement.md) — const集約の段階的強制（M3、エラー化は将来バージョン）
- [definite-assignment-and-correctness-lints.md](../../archive/v0.17.0/definite-assignment-and-correctness-lints.md) — 確定代入・return網羅の検査と--strict昇格（H6/L4）
- [bounds-checking-policy.md](../../archive/v0.17.0/bounds-checking-policy.md) — スライス境界チェックの全バックエンド統一（M1）
- [numeric-output-and-cast-consistency.md](../../archive/v0.17.0/numeric-output-and-cast-consistency.md) — double出力round-trip化とキャスト飽和の統一（M8/M9）
- [closures-multi-capture.md](../../archive/v0.17.0/closures-multi-capture.md) — 複数キャプチャクロージャの環境ポインタ化と高階関数対応（C6）
- [interface-values-in-aggregates.md](../../archive/v0.17.0/interface-values-in-aggregates.md) — 集約に入るインターフェイス値のfat pointer構築（H1/H2）
- [js-ts-value-semantics.md](../../archive/v0.17.0/js-ts-value-semantics.md) — js/tsの値セマンティクス統一とlong/ulongのBigInt化（H3/H5）
- [chain-receiver-resolution.md](../../archive/v0.17.0/chain-receiver-resolution.md) — チェーンレシーバ解決の共通化と添字レシーバ対応（H10全5段）
- [self-hosting-preparation.md](../../archive/v0.17.0/self-hosting-preparation.md) — OS連携API・argv・セルフホスト素振りの全4段（S1〜S9。セルフホスト本体は1.0以降に別文書で扱う）
- [mangling-collision-detection.md](../../archive/v0.17.0/mangling-collision-detection.md) — マングリング名衝突のハードエラー化（C16）
- [generic-instantiation-diagnostics.md](../../archive/v0.17.0/generic-instantiation-diagnostics.md) — ジェネリックインスタンス化の診断（H15/L8）
- [misc-diagnostics-and-low-priority.md](../../archive/v0.17.0/misc-diagnostics-and-low-priority.md) — 補間ネスト・var・assert_eq・SV黙殺解消・fmt演算子空白ほか（M18/L1〜L6）
- [01_js_npm_interop.md](../../archive/v0.17.0/01_js_npm_interop.md) — npmパッケージ連携の実装設計（ロードマップは`docs/design/js_interop_roadmap.md`）
- [multidim-partial-array-extraction.md](../../archive/v0.17.0/multidim-partial-array-extraction.md) — 多次元配列の低次元部分取り出し（ユーザー型多次元宣言パース・要素数不一致の診断化・スライスof固定長配列の要素コピー・js固定長配列の値セマンティクス）

## 監査・網羅検証

- [large-scale-bottleneck-audit.md](../../archive/v0.17.0/large-scale-bottleneck-audit.md) — 大規模開発ボトルネック監査の全57所見（C/H/M/L系、全件対応完了）
- [syntax-audit-bugfixes.md](../../archive/v0.17.0/syntax-audit-bugfixes.md) — 構文網羅検証 第1ラウンド（B1〜B9の総括）
- [move-closure-interp-audit.md](../../archive/v0.17.0/move-closure-interp-audit.md) — move・クロージャ・補間式添字の検証（V1〜V8の総括）
- 5バックエンド差分プローブ（N1〜N8）の個別文書: [interp-nested-slice-index.md](../../archive/v0.17.0/interp-nested-slice-index.md)・[generic-slice-element-garbage.md](../../archive/v0.17.0/generic-slice-element-garbage.md)・[string-switch-miscompile.md](../../archive/v0.17.0/string-switch-miscompile.md)・[wasm-reduce-closure-trap.md](../../archive/v0.17.0/wasm-reduce-closure-trap.md)・[generic-struct-literal.md](../../archive/v0.17.0/generic-struct-literal.md)・[enum-multi-payload-match.md](../../archive/v0.17.0/enum-multi-payload-match.md)・[negative-radix-format.md](../../archive/v0.17.0/negative-radix-format.md)・[js-string-index-bigint.md](../../archive/v0.17.0/js-string-index-bigint.md)
- native/jit網羅検証 第2・第3ラウンド（W1〜W5・X1〜X6）の個別文書: [nested-anonymous-struct-literal-loss.md](../../archive/v0.17.0/nested-anonymous-struct-literal-loss.md)・[nested-slice-element-write-crash.md](../../archive/v0.17.0/nested-slice-element-write-crash.md)・[slice-struct-pop-value-crash.md](../../archive/v0.17.0/slice-struct-pop-value-crash.md)・[licm-global-clobber-miscompile.md](../../archive/v0.17.0/licm-global-clobber-miscompile.md)・[interp-chain-lowering-failures.md](../../archive/v0.17.0/interp-chain-lowering-failures.md)・[static-block-scope-init-loss.md](../../archive/v0.17.0/static-block-scope-init-loss.md)・[private-field-access-unchecked.md](../../archive/v0.17.0/private-field-access-unchecked.md)・[slice-push-array-literal-corruption.md](../../archive/v0.17.0/slice-push-array-literal-corruption.md)・[slice-push-anonymous-struct-literal.md](../../archive/v0.17.0/slice-push-anonymous-struct-literal.md)・[syntax-error-position-and-token-display.md](../../archive/v0.17.0/syntax-error-position-and-token-display.md)・[private-method-cross-impl-visibility.md](../../archive/v0.17.0/private-method-cross-impl-visibility.md)
- 個別バグ文書（B系ほか）: [must-block-field-assignment.md](../../archive/v0.17.0/must-block-field-assignment.md)・[int-literal-to-float-conversion.md](../../archive/v0.17.0/int-literal-to-float-conversion.md)・[nested-member-slice-chain.md](../../archive/v0.17.0/nested-member-slice-chain.md)・[cast-null-pointer-comparison.md](../../archive/v0.17.0/cast-null-pointer-comparison.md)・[interface-bound-method-return-type.md](../../archive/v0.17.0/interface-bound-method-return-type.md)・[interface-method-interpolation-type.md](../../archive/v0.17.0/interface-method-interpolation-type.md)・[typedef-struct-literal-resolution.md](../../archive/v0.17.0/typedef-struct-literal-resolution.md)・[defer-implicit-function-end.md](../../archive/v0.17.0/defer-implicit-function-end.md)・[const-global-aggregate-init.md](../../archive/v0.17.0/const-global-aggregate-init.md)・[uninitialized-struct-fields.md](../../archive/v0.17.0/uninitialized-struct-fields.md)
