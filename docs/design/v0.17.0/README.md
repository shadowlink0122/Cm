---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書（索引）

v0.17.0の設計文書は下記「第5ラウンド」の新規所見（Q5）と「全体複雑度レビュー」のリファクタリング提案8件を除き全件の処置が完了し、実装済み文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動した（本READMEは索引として残る）。
各文書には設計方針・段階分割・実装記録・不採用判断・将来課題を記録している。
変更の要約はリリースノート（[docs/releases/v0.17.0.md](../../releases/v0.17.0.md)）を参照。
あわせて、全検証ラウンドでまだバグ調査の対象になっていない構文・機能の棚卸しを後述の「構文・機能カバレッジの棚卸し」セクションに表で列挙している。

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
- Z5: 暗黙変換と明示キャストの設計整理 — **修正済み**（[archive移動](../../archive/v0.17.0/implicit-explicit-cast-design.md)。classify_numeric_conversion一元化・縮小/符号変化の警告と--strictエラー昇格・coerce_numeric_context一般化でdouble→intの全文脈修正・CANONICAL_SPEC 10.3変換表・stdlib回避策as削減。uint/usize→intはlen/sizeofイディオム維持のため現段階無診断と仕様決定）

## 未修正バグ調査 第5ラウンド（Q1〜Q7）

過去ラウンドで未検証だった領域（複雑左辺値の複合代入・for-inイテレータ・Try演算子・グローバル初期化・ポインタ演算・複数型引数ジェネリクス・インターフェース戻り値・演算子オーバーロード・match式・defer・文字列/enumメソッド・ビット演算・HashMap負荷）を6経路差分（jit O0/O2・native O0〜O3）で調査した。バグ1項目につき1文書。
健全確認済み: 複合代入/inc-decの複雑左辺値・for-in（スライス/固定長配列）・Try連鎖・グローバル依存初期化・ポインタ演算stride・inherent演算子オーバーロード・match式全値位置・defer順序/キャプチャ・文字列メソッド群・has_next形イテレータ・ビット/char演算・sizeof。

- Q2: ネストしたジェネリック型引数のstringフィールド読みが無言死 — **修正済み**（[archive移動](../../archive/v0.17.0/nested-generic-type-arg-string.md)。真因は2系統: struct_symbol_keyのsimple高速パスが生成する曖昧フラット名の誤逆算（複数引数基底×特殊化引数を$エンコードへ退避）と、内側リテラルの型注釈がフィールド宣言型のジェネリックパラメータ名で上書きされ裸のBoxのままlowerされる問題（propagate_literal_expected_typeの上書き抑止+実引数置換）。ネスト特殊化マトリクスの回帰をjit/native O0〜O3/wasm/jsで追加。フラット名逆算の全廃は[mono-flat-name-elimination.md](mono-flat-name-elimination.md)が引き続き扱う）
- Q3: インターフェース戻り値のfat pointer構築欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/interface-return-fat-pointer.md)。真因はペイロードが呼び出し先スタックを指すダングリング（O0のみ偶然動作）。upcast時のfat pointerペイロードをヒープboxing化し、jsの転送引数の再ラップ（Shape_Shape_vtable未定義参照）も修正。ペイロードのdrop対応は将来課題）
- Q7: HashMapが17要素以上で挿入済み要素を喪失 — **修正済み**（[archive移動](../../archive/v0.17.0/hashmap-resize-loses-entries.md)。真因はresize未実装で満杯後のinsertが黙って喪失。負荷率50%で2倍拡張・全エントリ再ハッシュのgrow()を実装し、境界16/17/33・200件・remove/上書きまたぎの回帰を追加。removeの探索列分断とstringキーハッシュは将来課題として記録）
- Q1: for-inイテレータプロトコルの検査穴 — **修正済み**（[archive移動](../../archive/v0.17.0/forin-iterator-protocol-checks.md)。check_for_inのiter()発見時にhas_next存在+bool戻り・next存在・非Option戻りを検査しi18n診断で停止。Option返しnextはプロトコル外と仕様決定（暗黙unwrap非対応・従来も一度も動作していないため非破壊）。エラーテスト4本+i18n E2E追加、チュートリアルへiter()プロトコル節を新設）
- Q4: 算術演算子インターフェースのimpl形が内部エラー — **修正済み**（[archive移動](../../archive/v0.17.0/arith-operator-interface-decl.md)。算術・ビット演算子インターフェース10種をEq/Ordと同形で組み込み宣言し`impl T for Add`形を受理、decl.cppのthrow4件（未宣言インターフェース・重複impl・重複メソッド）を通常診断へ置換。肯定+エラーテスト・i18n E2E追加、チュートリアルへインターフェース指定形を明記。`<T: Add>`境界の総称本体内算術は未対応の既知制約として記録）
- [Q5: enumへのinherent implメソッドが未サポート](enum-inherent-impl-methods.md) — impl宣言は黙って受理され呼び出しで「Unknown method for type 'int'」（Medium）
- Q6（文書化なし・注記のみ）: `replace()`が最初の一致のみ置換する仕様がドキュメント未記載（全置換との区別を文字列チュートリアルへ明記すべき。Low）

## 構文・機能カバレッジの棚卸し（未調査項目一覧・第6ラウンド候補）

CANONICAL_SPEC・cm_grammar.md・レクサ/パーサ実装・libs・tests全域を突き合わせ、B〜Qの全検証ラウンドと57所見監査のいずれでもバグ調査（バックエンド差分プローブ）の対象になっていない構文・機能を棚卸しした。
診断状況の凡例: **未調査** = どのラウンドでもプローブ未実施、**一部調査** = 特定側面のみ調査済みで残りが未対象。
「統合テスト」はtests/配下の.cmテストの有無（機能テストの存在はバグ調査済みを意味しない——HashMapはテストが存在したままQ7の要素喪失を見逃していた）。
調査済みで未修正・将来課題として文書化済みの項目（Q5 enumのinherent impl・Q6 replace文書化・混在変種ユニオンの三項/match腕・jsのblob意味論・`<T: Add>`境界の総称本体内算術・インターフェースペイロードのdrop・HashMap removeの探索列分断など）はこの表に含めない（各ラウンド文書を参照）。

### A. 属性・ディレクティブ・プリプロセッサ

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| A1 | `#[test]`/`#bench`/`#deprecated`/`#inline`/`#optimize`関数ディレクティブ | 未調査 | 一部あり（`#[test]`はcmtest/libsランナーの正常系のみ） | deprecated警告・inline効果などディレクティブの意味解釈の検証なし |
| A2 | `#[derive(...)]`/`with`自動実装（Eq/Ord/Clone/Hash/Debug/Display/Css） | 一部調査 | あり（with_eq/with_ord/derive_basic） | C3（with Ordのstringアドレス比較）は修正済み。ジェネリック構造体deriveのSlice/Unionフィールド無言誤動作が既知（[auto-impl-generic-gaps-and-cleanup.md](auto-impl-generic-gaps-and-cleanup.md)） |
| A3 | `#[target(...)]`/`#[cfg(...)]`条件付きコンパイル | 未調査 | 見つからず | 未知ターゲット名・否定形`!js`・誤指定時の診断有無が未検証（誤除去・黙殺リスク） |
| A4 | 未知属性・タイポ属性の黙認 | 未調査 | なし | 属性パーサは任意識別子の`#[名前]`を構文受理するため、`#[tset]`等が無診断で無視される疑い |
| A5 | プリプロセッサ`#define` | 未調査 | なし | cm_grammar.mdに文法定義があるが実装はディレクティブ非対応（conditional.cppのdefine()はCLI/組み込みシンボル用）。仕様文書と実装の不一致 |
| A6 | プリプロセッサ`#endif` | 未調査 | なし | 実装は`#end`のみ認識し`#endif`は通常行として黙って素通り（conditional.cpp parse_directive）。cm_grammar.md・VSCode文法は`#endif`を記載しており不一致 |
| A7 | `#ifdef`/`#ifndef`/`#else`/`#end`のネスト・異常系 | 一部調査 | 基本テストあり（preprocessor/ifdef_basic等） | 閉じ忘れ・過剰`#end`・ネスト境界の診断が未検証 |
| A8 | `//! platform:`・`//! test:`・`//! sv:`ディレクティブの異常系 | 未調査 | 正常系はSVスイートで常用 | 誤記・未知プラットフォーム名指定時の診断が未検証 |

### B. 構文・式

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| B1 | async/await | 未調査 | jsの基本テストのみ（tests/js/async） | native/wasm/ts経路の挙動・エラーパス・await式の型検査が未プローブ |
| B2 | macro宣言（定数マクロ・関数マクロ） | 未調査 | 基本テストあり（common/macro） | 6経路差分プローブ未実施でバックエンド分裂リスク未確認 |
| B3 | ラムダの参照キャプチャ`[&x]` | 未調査 | なし | 値キャプチャはC6/V5〜V7で調査済み。参照キャプチャは文法定義がありテストゼロ |
| B4 | raw string（`r"..."`/`r#"..."#`） | 未調査 | なし | バッククォート文字列はテストあり。raw stringのエスケープ無効化・`${}`補間保持が未検証 |
| B5 | エスケープ識別子（バッククォート`` `名前` ``） | 未調査 | なし | マングリング・モジュール解決との相互作用が未検証 |
| B6 | エスケープシーケンス`\xHH`/`\uHHHH`/`\UHHHHHHHH` | 未調査 | なし | UTF-8基盤刷新（H9）後のコードポイント整合が未検証 |
| B7 | 数値リテラルの桁区切り`_`・型サフィックス（u/l/f/d等） | 未調査 | なし | 16進/2進/8進リテラルはテストあり。サフィックスと型推論・縮小警告（Z5）の相互作用が未検証 |
| B8 | `${...}`形式の文字列補間 | 未調査 | なし | `{...}`形式はV/W/L2で徹底調査済み。lexerは`${`も受理する |
| B9 | タプル型・タプル式 | 未調査 | 専用テストなし | 文法定義はあるが実装到達度自体が不明 |
| B10 | 参照型`T&` | 未調査 | なし | 文法定義はあるが実装到達度不明（設計はポインタ代替が正準） |
| B11 | 演算子オーバーロードの`[]`/`()` | 未調査 | なし | 算術・比較・複合代入はQ4と既存テストで調査済み |
| B12 | `overload`メソッド（コンストラクタ以外） | 未調査 | コンストラクタのみ（basic/constructor_overload） | メソッドの同名オーバーロードはテスト・プローブなし |
| B13 | インターフェースのデフォルト実装（本体付き宣言） | 未調査 | なし | 文法定義はあるが実装状況不明 |
| B14 | ユーザー定義関数の可変長引数`...` | 未調査 | FFIのprintf経由のみ | ユーザー定義関数での受理・実行の検証なし |
| B15 | matchの範囲パターン`a..b` | 未調査 | なし（masked_patternはあり） | パーサはmake_rangeを持つがテストゼロ |
| B16 | ジェネリクスのデフォルト型引数`<T = int>` | 未調査 | なし | 文法定義あり |
| B17 | const genericパラメータの境界・演算 | 一部調査 | 基本テストあり（generics/const_generics） | 差分プローブ・エラーパスが未実施 |

### C. 修飾子・宣言

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| C1 | `constexpr` | 未調査 | なし | チュートリアルに記載があるがテストゼロ |
| C2 | `inline`修飾子/`#inline` | 未調査 | なし | 効果・診断とも未検証 |
| C3 | `volatile` | 未調査 | asm最適化テストでの付随使用のみ | 単体の意味論が未検証 |
| C4 | 語彙のみのキーワード（`mutable`/`namespace`/`template`/`typename`/`pub`/`from`） | 未調査 | なし | トークン定義はあるが実装利用が不明。使用時に黙殺されるか診断されるか未確認 |
| C5 | `ufloat`/`udouble` | 未調査 | なし | 語彙はあるが実装・意味論が不明 |
| C6 | `extern`宣言（native一般） | 未調査 | SVのextern_instance等のみ | |
| C7 | デフォルト引数 | 一部調査 | あり（functions/default_args等） | Y1〜Y3/Y5で変換適用サイトとして検証済み。評価順・複雑な既定式などの異常系は未対象 |

### D. 標準ライブラリ・ランタイム

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| D1 | TreeMap | 未調査 | 機能テストあり | Q7（HashMap要素喪失）と同型の負荷・境界・removeプローブ未実施（アリーナのノード再利用など） |
| D2 | std::json | 未調査 | libテストあり（json/mod_test.cm） | 異常系JSON・深いネスト・大入力の差分プローブ未実施 |
| D3 | std::env/process/path/bytes/fs | 一部調査 | selfhost素振りのCI検証（S1〜S9） | エラーパス・プラットフォーム差の個別プローブ未実施 |
| D4 | std::ioの対話入力（input/input_int等） | 未調査 | なし（自動化困難） | パース失敗時のOption返し（M17）の入力系適用が未検証 |
| D5 | native::sync/thread | 一部調査 | 機能テストあり（tests/llvm/sync・thread） | L7（native専用・int64のみ）の所見のみで、実並行実行・競合の検証なし |
| D6 | native::net/http | 未調査 | 機能テストあり | ラウンド未対象 |
| D7 | native::gpu（Metal） | 未調査 | 機能テストあり | ラウンド未対象 |
| D8 | web::html・js::fetch/timer | 未調査 | libテスト・jsスイートあり | 差分プローブの枠外 |
| D9 | std::iterのadapters（map/filter等） | 未調査 | — | mod.cmに「将来実装」と記載があり実装到達度不明 |
| D10 | アロケータ差し替え（set_allocator_fns） | 一部調査 | M14実装時の検証のみ | 差し替え後の全確保経路・マルチバックエンドの検証は未実施 |

### E. バックエンド・ターゲット

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| E1 | SVバックエンドの網羅バグ調査 | 一部調査 | 専用スイートあり（tests/sv） | 検証ラウンドはnative/jit/wasm/js中心でSVはM18のみ。SVテストのx楽観性（不定値のassert素通り）リスクが残る |
| E2 | SV固有構文の異常系（`bit<N>`・always系・assign・`+:`・幅付きリテラル・`#[sv::*]`属性群） | 未調査 | 正常系はSVスイートにあり | 誤用時の診断・非SVターゲットでの使用時の挙動が未検証 |
| E3 | UEFIターゲット | 未調査 | 機能テストあり（tests/uefi） | ラウンド未対象 |
| E4 | baremetal-arm | 未調査 | 機能テストあり（tests/baremetal） | ラウンド未対象 |
| E5 | TSバックエンド固有経路 | 一部調査 | tests/tsと`tsc --noEmit`ゲート | js統合分の調査のみで、TS固有の型注釈生成の意味論プローブは未実施 |

## 全体複雑度レビュー（未実装のリファクタリング提案）

修正履歴の同族バグ分析（変換サイト欠落族・メソッド解決分裂族・名前逆算族）と全ソースの実測（サイト×変換種マトリクス・キー計算箇所の棚卸し・ランタイムdiff・関数長スキャン）に基づき、複雑すぎる実装をシンプルかつバグが再発しない構造へ変えるための提案。優先度順。

- [暗黙変換の統一ドライバ化](coercion-driver-unification.md) — 変換挿入が11サイトに手組み散在（全種連鎖は2サイトのみ・ユニオンは2方式併存・インターフェースupcastは変換系外で各バックエンド個別）。coerce_to_expected一本化＋upcastのMIR化＋checker受理との同表化で、B2→Y1〜Y3→Y5→Z5→Q3と続いた「受理したのに未挿入」バグ族を構造的に封止する（Critical）
- [モノモーフ化のフラット名逆算の完全廃止](mono-flat-name-elimination.md) — 可逆$エンコーダ（typekey）があるのにstruct_symbol_keyのsimple高速パスがネスト特殊化で曖昧フラット名を生成し、parse_flat_type_argsの誤逆算がQ2の真因。逆算器の削除とtypekey全面化でQ2族を表現不能にする（Critical）
- [メソッド解決の一元化](method-resolution-unification.md) — メソッド表キー計算が12箇所別実装・解決機構4系統・types/の全throw4件が内部エラー漏れ。正準キー関数＋resolve_method APIでQ1/Q4/Q5族を封止する（High）
- [型検査の解決結果をHIRへ引き渡す](checker-to-hir-resolution-handoff.md) — MemberExprが解決結果を捨てるためlower_member（単一関数1100行・全ソース最大）がcheckerの解決を全再導出。解決注釈の導入で再導出コードを削除する（Medium）
- [モジュールグラフのテキスト手術脱却](module-graph-ast-emission.md) — 構造化import後も包含判定は正規表現識別子スキャン・出力はスパン消し込み+改名テキスト複製のまま。判定のAST化→出力のAST化の2段で座標ズレ（X5同根）と誤包含を解消する（Medium）
- [型サイズ照会の一本化](layout-size-single-source.md) — サイズ実装が4系統（HIRの暫定256バイト・MIRのフィールド数×8見積もり・monoのフラット名依存・真実のlayout系）でsizeofが見積もりを答えうる。全照会をlayout API 1系統へ（Medium）
- [derive自動実装の残存MIR生成の整理](auto-impl-generic-gaps-and-cleanup.md) — 源展開移行後も約2,970行のMIR直生成が残存（非ジェネリック分は死に体）。ジェネリックパスはSlice/Unionフィールド未対応で無言誤動作。削除→ギャップ封鎖→単一ソース化完遂の3段（Medium）
- [配列HOFランタイムの共通ソース化](runtime-hof-common-source.md) — native/wasmのruntime_format.cは名前79%共有・本体35〜40%一致で二重実装が常態。slice方式（共通.inc+5フック）をHOF/検索ビルトイン群へ適用。文字列フォーマット系はwasm SDS化まで非対象の既存判断を維持（Low）

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
