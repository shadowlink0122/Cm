---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書（索引）

v0.17.0の設計文書は未修正バグ調査（Q1〜Q7）まで全件の処置が完了し（実装済み文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動）、未処置は「全体複雑度レビュー」のリファクタリング提案7件（8件中、配列HOFランタイム共通ソース化は実施済み）と、下記**追加調査（R1〜R25）で新規に検出したバグ**のうち未修正分（構文網羅バグ調査はR1・R3・R4・R5・R6・R7・R8修正済み・archive移動で7件、バックエンド網羅バグ調査はR15・R17修正済みで残4件、ライブラリ・自動実装調査はR21修正済みで残4件）である（本READMEは索引として残る）。
各文書には設計方針・段階分割・実装記録・不採用判断・将来課題を記録している。
変更の要約はリリースノート（[docs/releases/v0.17.0.md](../../releases/v0.17.0.md)）を参照。
構文網羅バグ調査はそれ以前に未調査だった構文・機能（棚卸し表のA〜D）、バックエンド網羅バグ調査はバックエンド・ターゲット（E）、ライブラリ・自動実装調査は残った一部調査項目（A2 derive・D3/D5/D6/D7/D8ライブラリ）を実機プローブしたもの。これで棚卸し表の全項目の調査を完了した。

## レイヤー別レビュー（未修正の新規所見 Y1〜Y6）

全修正完了後のフロント〜コード生成レイヤー別レビュー（ユニオン・リテラル型・const伝搬・戻り値解決・配列/スライス境界・型昇格の差分検証）で検出。バグ1項目につき1文書。

- Y1〜Y3: ユニオン構築（タグ書き込み）の消費サイト欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/union-construction-sites.md)。coerce_to_union共通ヘルパをreturn・フィールド・push・リテラル要素・引数/デフォルト引数へ適用。混在変種の三項/match腕は残課題として記録）
- Y4: int×double混合二項演算が不正IR・SIGBUS — **修正済み**（[archive移動](../../archive/v0.17.0/numeric-promotion-binary-ops.md)。infer_binaryの昇格Cast挿入+MIR防衛層+CANONICAL_SPEC 10.2明文化）
- Y5: 固定長配列→スライス引数の暗黙変換欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/fixed-array-to-slice-argument.md)。coerce_fixed_array_to_sliceを引数/デフォルト引数へ適用・decay抑止・コピー意味論をチュートリアル明文化）
- Y6: スライスof固定長配列の要素格納表現が未定義 — **修正済み**（[archive移動](../../archive/v0.17.0/slice-of-fixed-array-elements.md)。N×実ストライドのインラインblobに仕様確定・dispatch/layout/codegen/letの4系統統一。jsのblob意味論は将来課題）

### レイヤー別レビュー追補: ユニオン・文字列要素の配列/スライス整合性（Z1〜Z3）

要素サイズが型依存（ポインタ幅・タグ付き・可変ペイロード）の配列/スライスについて、サイズ決定サイトと実行の整合性を調査した。基本レイアウト（string固定長配列の全操作・ユニオン固定長配列の読み書き・大型構造体バリアント・構造体内string配列）はnative/jit/wasmで整合を確認済み。

- Z1: 配列検索ビルトインの要素型ディスパッチ欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/array-builtin-elem-dispatch.md)。値比較系の全幅+str変種を両ランタイムへ追加・js緩い等価対応・未対応要素の診断化）
- Z2: 固定長配列→スライス変換の手書き要素サイズ残存 — **修正済み**（[archive移動](../../archive/v0.17.0/array-to-slice-elem-size.md)。要素サイズ決定をMIRのlayout API 1系統へ統一・死コード削除）
- Z3: 構造体内ユニオンフィールドのタグがwasmで読めない — **解決確認**（[archive移動](../../archive/v0.17.0/wasm-union-in-struct-tag.md)。バイセクトでY1〜Y3のタグ書き込み統一が真因と特定——nativeは偶然一致だった。マトリクス回帰を4系一致で追加）
- Z4: 型検査のエラー検出漏れ3件 — **修正済み**（[archive移動](../../archive/v0.17.0/checker-error-coverage-holes.md)。push要素型検査・非変種as拒否・ループ深度によるbreak/continue診断、エラーテスト4本追加）
- Z5: 暗黙変換と明示キャストの設計整理 — **修正済み**（[archive移動](../../archive/v0.17.0/implicit-explicit-cast-design.md)。classify_numeric_conversion一元化・縮小/符号変化の警告と--strictエラー昇格・coerce_numeric_context一般化でdouble→intの全文脈修正・CANONICAL_SPEC 10.3変換表・stdlib回避策as削減。uint/usize→intはlen/sizeofイディオム維持のため現段階無診断と仕様決定）

## 未修正バグ調査（Q1〜Q7）

過去の調査で未検証だった領域（複雑左辺値の複合代入・for-inイテレータ・Try演算子・グローバル初期化・ポインタ演算・複数型引数ジェネリクス・インターフェース戻り値・演算子オーバーロード・match式・defer・文字列/enumメソッド・ビット演算・HashMap負荷）を6経路差分（jit O0/O2・native O0〜O3）で調査した。バグ1項目につき1文書。
健全確認済み: 複合代入/inc-decの複雑左辺値・for-in（スライス/固定長配列）・Try連鎖・グローバル依存初期化・ポインタ演算stride・inherent演算子オーバーロード・match式全値位置・defer順序/キャプチャ・文字列メソッド群・has_next形イテレータ・ビット/char演算・sizeof。

- Q2: ネストしたジェネリック型引数のstringフィールド読みが無言死 — **修正済み**（[archive移動](../../archive/v0.17.0/nested-generic-type-arg-string.md)。真因は2系統: struct_symbol_keyのsimple高速パスが生成する曖昧フラット名の誤逆算（複数引数基底×特殊化引数を$エンコードへ退避）と、内側リテラルの型注釈がフィールド宣言型のジェネリックパラメータ名で上書きされ裸のBoxのままlowerされる問題（propagate_literal_expected_typeの上書き抑止+実引数置換）。ネスト特殊化マトリクスの回帰をjit/native O0〜O3/wasm/jsで追加。フラット名逆算の全廃は[mono-flat-name-elimination.md](mono-flat-name-elimination.md)が引き続き扱う）
- Q3: インターフェース戻り値のfat pointer構築欠落 — **修正済み**（[archive移動](../../archive/v0.17.0/interface-return-fat-pointer.md)。真因はペイロードが呼び出し先スタックを指すダングリング（O0のみ偶然動作）。upcast時のfat pointerペイロードをヒープboxing化し、jsの転送引数の再ラップ（Shape_Shape_vtable未定義参照）も修正。ペイロードのdrop対応は将来課題）
- Q7: HashMapが17要素以上で挿入済み要素を喪失 — **修正済み**（[archive移動](../../archive/v0.17.0/hashmap-resize-loses-entries.md)。真因はresize未実装で満杯後のinsertが黙って喪失。負荷率50%で2倍拡張・全エントリ再ハッシュのgrow()を実装し、境界16/17/33・200件・remove/上書きまたぎの回帰を追加。removeの探索列分断とstringキーハッシュは将来課題として記録）
- Q1: for-inイテレータプロトコルの検査穴 — **修正済み**（[archive移動](../../archive/v0.17.0/forin-iterator-protocol-checks.md)。check_for_inのiter()発見時にhas_next存在+bool戻り・next存在・非Option戻りを検査しi18n診断で停止。Option返しnextはプロトコル外と仕様決定（暗黙unwrap非対応・従来も一度も動作していないため非破壊）。エラーテスト4本+i18n E2E追加、チュートリアルへiter()プロトコル節を新設）
- Q4: 算術演算子インターフェースのimpl形が内部エラー — **修正済み**（[archive移動](../../archive/v0.17.0/arith-operator-interface-decl.md)。算術・ビット演算子インターフェース10種をEq/Ordと同形で組み込み宣言し`impl T for Add`形を受理、decl.cppのthrow4件（未宣言インターフェース・重複impl・重複メソッド）を通常診断へ置換。肯定+エラーテスト・i18n E2E追加、チュートリアルへインターフェース指定形を明記。`<T: Add>`境界の総称本体内算術は未対応の既知制約として記録）
- Q5: enumへのinherent implメソッドが未サポート — **修正済み・(a)サポートを採用**（[archive移動](../../archive/v0.17.0/enum-inherent-impl-methods.md)。enum正規化の全面遅延でなく「int解決時にenum名をnameへ保持」する名前ピギーバック方式で、checker解決・HIRマングル・MIRのself渡し規約（値enum=値渡し・タグ付き=ポインタ渡し）・__tag恒等化・ペイロード射影derefの5箇所を修正。タグ付きenumのメソッドも動作。値enumメソッド内のself再代入はコピーに閉じる値意味論と仕様決定）
- Q6（文書化なし・注記のみ）: `replace()`が最初の一致のみ置換する仕様がドキュメント未記載（全置換との区別を文字列チュートリアルへ明記すべき。Low）

## 構文網羅バグ調査（R1〜R14）

下記「構文・機能カバレッジの棚卸し」で列挙した未調査項目のうち、属性・ディレクティブ（A）・構文/式（B）・修飾子/宣言（C）・標準ライブラリ（D）の全項目を6並列で実機プローブ（jit O0/O2・native O0〜O3・wasm・js/ts）した。過去の調査と異なり、未実装構文の受理/診断/黙殺の別・仕様書との乖離まで対象にした。バグ1項目（同根は束ねて）につき1文書を起票。Critical/Highは自分で最小再現を実機で裏取り済み。
健全確認済み: async/await（js/ts動作+他経路の明示拒否）・macro正常系（6経路一致）・`${}`補間本体（`{}`と同値）・`...`範囲パターン（両端含む・先勝ち・網羅性強制、9実行一致）・TreeMap（1000件/10万件の挿入喪失なし・自動拡張）・対話入力のOption返し（M17適用）・`namespace`（実装済み動作）・extern（native/jit動作・未解決シンボル診断）・予約語誤用診断（X5の空表示バグ再発なし）・数値リテラルの拒否は安全側（誤値化なし）。

- R1: std::jsonパーサの堅牢性 — **修正済み**（[archive移動](../../archive/v0.17.0/json-parser-robustness.md)。アリーナをTreeMap同方式のパラレル動的スライスへ置換し無限ループの発生源を構造的に消滅（ノード上限撤廃）、末尾ゴミ/複数値/数字なしマイナスを拒否、\uXXXXをサロゲートペア込みでUTF-8実デコード・不正エスケープを診断。併発発見のcm testモードのグローバル非定数初期化子欠落（#[test]エントリへ未注入でスライスグローバルがnull）もMIR lowering側で修正。jsの非ASCII表示はR2の文字列モデル分裂に帰属する既知制約）
- [R2: 文字列APIのコードポイント/バイト単位不一致](string-codepoint-byte-api-split.md) — **High**: `len()`=コードポイント数（H9）と`charAt()`=生バイトの添字単位不一致で、非ASCII文字列を走査するコードが破綻。std::jsonの非ASCIIパースがnative/jit失敗・js成功のバックエンド分岐に
- R3: ジェネリック`T*`引数の型パラメータ束縛失敗 — **修正済み**（[archive移動](../../archive/v0.17.0/generic-pointer-param-inference.md)。真因2系統: checkerのinfer_generic_callが`T*`/`T[]`/`T**`を推論できず3ケース個別実装だったのを構造再帰unifyへ置換、LLVM codegenのselfコピー最適化が特殊化名`deref__int`のT*引数をプリミティブimplメソッドのselfと誤認し要素型で確保していたのをシード条件へ「第0引数名=self」を追加。swap/deref/T[]/T**/ジェネリック構造体の回帰をjit/native O0〜O3/wasm/jsで追加）
- R4: クロージャの外側変数書き込み黙殺・構造体キャプチャのjs分岐 — **修正済み・方針1（診断拒否）を採用**（[archive移動](../../archive/v0.17.0/closure-mutation-semantics.md)。値キャプチャは読み取り専用と明確化し、キャプチャ変数への代入・複合代入・inc/dec（メンバ・添字書き込み含む）をi18n診断で拒否。ポインタキャプチャ経由の間接書き込み`*px = v`は伝播するため許可し公式の回避策としてチュートリアルへ明文化。構造体キャプチャのjs分岐は診断拒否により消滅。識別子収集のfor-in/ブロック/switch/defer/must走査漏れも同時修正。参照キャプチャ`[&x]`はR13/R15の未実装構文として残置）
- R5: 文字列エスケープの黙殺・raw文字列のエスケープ解釈・補間エスケープ不能 — **修正済み**（[archive移動](../../archive/v0.17.0/string-escape-and-raw-semantics.md)。`\xHH`/`\uHHHH`/`\UHHHHHHHH`のデコード（UTF-8エンコード・仕様書は元々約束済みで実装欠落だった）とC標準`\b`/`\f`/`\v`/`\a`を実装、未知エスケープはレクサ発行のErrorトークン→パーサ診断で位置つき拒否。std::jsonの`"\b"`が文字bへ化ける潜在stdlibバグも同時解消。charリテラルはエスケープ表を共用し複数バイトは専用診断。raw文字列は`` \` ``（デリミタ）以外エスケープ無効のリテラル保持へ。`\${x}`でリテラル`${x}`が書けるようMIRの`${{`時の`$`喪失も修正。VSCode拡張は対応/不正エスケープの2色化）
- R6: 条件付きコンパイルディレクティブの堅牢性 — **修正済み**（[archive移動](../../archive/v0.17.0/preprocessor-conditional-robustness.md)。#endifを#endの別名として認識、閉じ忘れ（開始行・シンボル付き）・対応ブロックのない#end/#endif/#else・#define使用をi18n診断化（-D/組み込みシンボル案内）、cm_grammar.mdを実態へ追従。パーサ段の行番号・imported module誤表記はR14へ委譲）
- R7: 属性の検証レジストリ — **修正済み**（[archive移動](../../archive/v0.17.0/attribute-validation-registry.md)。checkerの検証パスで3値分類を実装: 未知・タイポ属性=警告/--strictエラー、既知未実装（bench/optimize/inline/cfg）=専用診断、#[deprecated]=呼び出しサイト警告として実装昇格。#[target]未知名の意味反転を許可リスト検証で診断化、#[inline]の予約語パース特例、AttributeNode.spanで診断位置を属性自身に。cfg条件評価の実装はR13の領分として残置）
- R8: デフォルト引数での前引数参照が無診断でゼロ値 — **修正済み・方針1（診断拒否）を採用**（[archive移動](../../archive/v0.17.0/default-arg-prev-param-zero.md)。デフォルト引数はパラメータ束縛前に呼び出し側で評価される仕様のためC++同様にパラメータ参照を拒否。check_default_param_refsで宣言時に式を再帰走査し関数・implメソッド両経路で診断（自己参照・後方参照も検出）。エラーテスト3本+i18n E2E+正常系回帰を追加、チュートリアルへ制約を明記）
- [R9: stdlibの出荷不良](stdlib-shipping-defects.md) — **High/Medium**: `std::iter`モジュール自体が`range`多重定義と型エラーでコンパイル不能（`import std::iter::*`常時失敗）。Vector/HashMap/Queueが生malloc直呼びでアロケータ差し替えを素通し。`std::io`の入力API再exportが選択import/`*`とも解決不能
- [R10: 型検査の黙殺穴](checker-silent-holes.md) — **Medium**: 未定義型の変数宣言が無診断で実行まで通る・型不一致マクロ（`macro int X = "str";`）がcheck素通りでLLVM内部エラー/js黙殺の三分裂・const generic宣言が無警告受理されるが実体化手段が存在しない（半黙殺）
- [R11: 修飾子の未実装・黙殺](modifier-implementation-gaps.md) — **Medium/Low**: `constexpr`変数がパーサTODOのnullptr返しで壊れた診断・`inline`は無警告黙殺（IR不変）・`volatile`はパーサ未対応・`ufloat`/`udouble`のunsigned語義が未実装で負値も無診断
- [R12: matchの負数パターン不可・網羅matchのreturn漏れ誤検知](match-pattern-and-flow-gaps.md) — **Medium**: matchパターンに負数リテラル（`-1 =>`・`-5...-1`）が書けない。全arm returnの網羅matchに「falls off the end」を誤検知し--strictでビルド阻害
- [R13: 文法書・仕様書に定義があるが未実装の構文](unimplemented-documented-syntax.md) — **Low**: タプル・参照型`T&`・演算子`[]`/`()`・overloadメソッド（仕様書は実装済みと例示するがパース不能+黙殺）・IFデフォルト実装・可変長引数・デフォルト型引数・エスケープ識別子・数値サフィックスが未実装（診断ありだが仕様書と乖離）。実装かドキュメント追従かの判断待ち一覧
- [R14: 構文・プリプロセッサ診断の品質](syntax-error-diagnostic-quality.md) — **Medium（横断）**: パーサ/プリプロセッサ経由の構文エラーに行番号・桁がなく自ファイルを「imported module」と誤表記、誤誘導メッセージ（`main not found`・`assign`を型名扱い等）。X5で意味解析側は改善済みだがパーサ段が取り残されている

## バックエンド網羅バグ調査（R15〜R20）

棚卸し表のバックエンド・ターゲット（E）を3並列で実機プローブした（SVはiverilog+vvpシミュレーション、baremetal-arm/x86・UEFIはコンパイル成否と制約強制、TSは生成物の型注釈妥当性とts/js/jit値差分）。バグ1項目（同根は束ねて）につき1文書を起票。Critical/Highは自分で最小再現を実機で裏取り済み。
**教訓**: 調査中にユーザーがcmをリビルドした（HOF共通化コミット、cmバイナリmtime 19:15）ため、TSエージェントの旧バイナリ由来の所見3件（long戻り値のnumber混入・ulong定数の符号喪失・`as char`縮小欠落）は現行バイナリでは非再現だった。**バイナリが変わりうる環境では最終判定を現行バイナリで取り直すこと**——下記は全て現行バイナリでの再確認済みのみを記載する。
健全確認済み: native↔SVの数値意味論一致・SV固有構文の正常系（`bit[N]`・幅付きリテラル・マスクmatch・ビットスライス・always系）・baremetal-x86とUEFIの正常系コンパイル・malloc/println直呼びとラッパー経由の正しい拒否・TSのプリミティブ/struct interface/配列/クロージャ/ジェネリック/async→Promise/macro/エラー診断。

- R15: SVテスト検証の健全性 — **修正済み**（[archive移動](../../archive/v0.17.0/sv-test-verification-soundness.md)。`//! test:`期待値ごとに`!==`比較+`$fatal`をテストベンチへ生成し（4値比較でx/zも不一致扱い・既存`$display`行は維持し.expect突合は不変）、`#[test]`のassertを`if (!(cond))`から`if ((cond) !== 1'b1)`のx安全形へ変更。ディレクティブ検出の行中誤認識（コメント中の`//! test:`言及をテストケース化）も行頭限定で修正。negative check 4本（不一致/x出力/assert対象xがrc=1でFAIL）をcm test E2Eへ、アサート生成の固定をregressionへ追加。tests/sv 125件は無修正で全PASS＝既存期待値の正しさも確認）
- [R16: SVコード生成が不正な構文を無診断で受理](sv-codegen-silent-invalid.md) — **Medium**: `#[sv::pinn]`タイポでピン制約が静かに欠落・`#[input]`ポートへの代入が不正SV（iverilog l-valueエラー）・エッジ無し`always_ff`が`always_comb`へ黙殺変換・`bit[0]`が不正SV（`0'd0`）。桁あふれ`4'd99`・非文字列pin引数・native流入診断の低品質（Low）
- R17: baremetal-armターゲットが起動コードのmemcpy型不一致で全滅 — **修正済み**（[archive移動](../../archive/v0.17.0/baremetal-arm-startup-broken.md)。memcpy/memsetのsize引数をDataLayoutのポインタ幅型（arm=i32）にしCreatePtrDiff結果をIntCastで整合。実装中に起動コード自体の意味バグ2件も発見・同時修正: リンカシンボルのload誤り（アドレス自体が境界なのに値をloadしていた）とptrdiff要素型ポインタによるサイズ1/4化。テストランナーのllvm-baremetalドライバへx86成功時のarm二段コンパイルゲートを追加し、x86のみ実行の盲点を封止。逆アセンブルで正しい初期化列を確認）
- [R18: フリースタンディング制約の強制漏れ](freestanding-nostd-enforcement-gaps.md) — **High**: 文字列連結（`cm_string_concat`＝malloc依存）がbaremetal/UEFIで無診断コンパイル（ブロックリストに`cm_string_*`漏れ）・`&putchar`の関数ポインタ間接呼び出しでブロックリスト回避。floatがbaremetal-x86失敗/UEFI成功の非対称（Medium）
- [R19: TS出力がlong/ulongフィールドへnumberリテラルを代入しtscを通らない](ts-bigint-number-generation.md) — **Medium**: structフィールド代入が`_t.v = 5`（`number`を`bigint`フィールドへ）でTS2322。「生成TSがtscを通る」第一級保証に違反（実行値は正）。戻り値・let初期化サイトは修正済みでフィールド代入サイト固有
- [R20: 文字列補間・書式指定子のバックエンド分岐](interpolation-format-backend-divergence.md) — **Medium**: 単項`~`を含む補間（`{~a}`）がjs/tsで全プレースホルダをundefined破壊・jit/nativeで文字列素通し。書式の幅/科学記法（`:6`/`.2e`）をnative/jitが無視しjs/tsが適用（3経路不一致）

## ライブラリ・自動実装 深掘り調査（R21〜R25）

棚卸し表で「一部調査」「未調査」のまま残っていたderive/with自動実装（A2）とライブラリモジュール（D3 OS連携・D5 sync/thread・D6 net/http・D7 gpu・D8 web）を4並列で実機プローブした。バグ1項目（同根は束ねて）につき1文書を起票。Critical/Highは現行バイナリで裏取り済み。
**教訓（バックエンド網羅バグ調査に続き再発）**: 調査中にユーザーがcmを複数回リビルドした（19:18→19:39→19:44）ため、旧バイナリ由来の所見（A2スライス型引数EqのJIT-O2 SIGSEGVはクラッシュが消え誤値のみ残存等）は現行バイナリで取り直した。下記は全て現行バイナリでの再確認済み。
健全確認済み: derive非ジェネリック全トレイト（6経路一致）・Ord全順序性/C3回帰・std::env/process/path/bytes/fs（バイナリ安全性・不在/空の区別・エラーパス非クラッシュ）・native::sync/threadのランタイム挙動（実並行・Mutex排他・macOS RwLock SIGILL回避・O2/O3無破壊）・net/http・gpu（実Metal実行）・web::htmlエスケープ（XSS遮断）。

- R21: derive/with自動実装のジェネリック型引数・フィールド型ギャップ — **修正済み**（[archive移動](../../archive/v0.17.0/derive-generic-and-field-gaps.md)。特殊化時の置換後フィールド型検証（validate_derive_instantiation）でスライス/ユニオン型引数の無言誤値・リンク失敗を使用箇所診断へ。ジェネリックのClone/Hash/Debug/Displayはchecker側のG<T>キー登録漏れが真因でメソッド解決を配線し全バックエンド動作化。値enumフィールドはint意味論でEq/Hash/Debug/Display対応・タグ付きenumは明示診断。derive診断の位置もname_span化でstdlib誤指しを解消。スライス/ユニオンを動作させる構造的解消は[auto-impl-generic-gaps-and-cleanup.md](auto-impl-generic-gaps-and-cleanup.md)第3段へ委譲）
- [R22: implメソッドの`export`修飾子がパースエラー](export-on-impl-method-parse-error.md) — **High**: `impl Pt { export int getx() {...} }`が`Expected type`で停止。`native::sync`の高レベルOOP API（Mutex<T>/RwLock<T>/Channel）が全メソッドexportで全滅し一度もimportできない死んだコード。`native::io`も連鎖。メンテナが回避策のfree-functionサブモジュールを用意済み
- [R23: クロスターゲットFFIの能力ガード欠如](cross-target-ffi-capability-gaps.md) — **Medium**: native専用モジュール（env/process/fs/net/gpu/sync/thread）が`--target=wasm`で無診断コンパイル成功し実行時に難解な`unknown import`で破綻（jsは`void*`禁止で明確に拒否）。js::timerのコールバックがint型宣言で使用不能・node例外（Low〜Medium）
- [R24: 文字列補間が実行時値の波括弧で破壊される](interpolation-brace-from-runtime-value.md) — **Medium**: `{a.debug()}`の戻り値`P { x: 1 }`が含む波括弧を補間エンジンが再スキャンし、その値と後続プレースホルダが総崩れ（`dbg=P (1, 2) disp={}`）。L2のリテラル波括弧とは別経路（評価済み値の再スキャン）
- [R25: 並行処理の最適化・戻り値の穴](concurrency-optimizer-and-join-gaps.md) — **Medium**: プレーン共有フラグのspin-waitがO1+で「Infinite loop risk」でコンパイル中断（atomic版は全レベル動作）。join()が64bitスレッド戻り値をint32に切り詰め（Low〜Medium）

## 構文・機能カバレッジの棚卸し（全項目の調査完了）

CANONICAL_SPEC・cm_grammar.md・レクサ/パーサ実装・libs・tests全域を突き合わせ、B〜Qの全検証と57所見監査のいずれでもバグ調査（バックエンド差分プローブ）の対象になっていなかった構文・機能を棚卸しした。属性・ディレクティブ（A）・構文/式（B）・修飾子/宣言（C）・標準ライブラリ（D）は構文網羅バグ調査（R1〜R14）、バックエンド・ターゲット（E）はバックエンド網羅バグ調査（R15〜R20）、残った一部調査項目（A2 derive・D3/D5/D6/D7/D8ライブラリ）はライブラリ・自動実装調査（R21〜R25）で**全項目の調査を完了**し、各項目の診断状況欄に対応文書を記した。
診断状況の凡例: **調査済み** = R1〜R25の調査でプローブ完了（→対応文書 or 健全）。
「統合テスト」はtests/配下の.cmテストの有無（機能テストの存在はバグ調査済みを意味しない——HashMapはテストが存在したままQ7の要素喪失を見逃していた）。

### A. 属性・ディレクティブ・プリプロセッサ

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| A1 | `#[test]`/`#bench`/`#deprecated`/`#inline`/`#optimize`関数ディレクティブ | 調査済み → [R7](../../archive/v0.17.0/attribute-validation-registry.md)（修正済み）・[R11](modifier-implementation-gaps.md) | 一部あり | `#[deprecated]`等は無警告黙殺（High）、`#inline`は記述不能。`#[test]`正常系は健全 |
| A2 | `#[derive(...)]`/`with`自動実装（Eq/Ord/Clone/Hash/Debug/Display/Css） | 調査済み → [R21](../../archive/v0.17.0/derive-generic-and-field-gaps.md)（修正済み） | あり（with_eq/with_ord/derive_basic・generic_derive_methods・derive_enum_field） | 非ジェネリックは全トレイト健全。ジェネリックのメソッド解決・特殊化時検証・値enumフィールド対応はR21で修正済み |
| A3 | `#[target(...)]`/`#[cfg(...)]`条件付きコンパイル | 調査済み → [R7](../../archive/v0.17.0/attribute-validation-registry.md)（修正済み） | 見つからず | `#[cfg]`完全不活性・未知ターゲット名の無診断Native縮退（High）。否定形`!js`は健全 |
| A4 | 未知属性・タイポ属性の黙認 | 調査済み → [R7](../../archive/v0.17.0/attribute-validation-registry.md)（修正済み） | なし | `#[tset]`等でテスト黙殺（High）を実証 |
| A5 | プリプロセッサ`#define` | 調査済み → [R6](../../archive/v0.17.0/preprocessor-conditional-robustness.md)（修正済み） | なし | 未実装の専用診断化（-D/組み込みシンボル案内）・文法書を実態へ追従 |
| A6 | プリプロセッサ`#endif` | 調査済み → [R6](../../archive/v0.17.0/preprocessor-conditional-robustness.md)（修正済み） | あり（preprocessor/endif_alias） | #endの別名として認識・後続コード保存を回帰固定 |
| A7 | `#ifdef`/`#ifndef`/`#else`/`#end`のネスト・異常系 | 調査済み → [R6](../../archive/v0.17.0/preprocessor-conditional-robustness.md)（修正済み） | エラーテスト3本追加 | 閉じ忘れ・過剰`#end`/`#else`を診断化 |
| A8 | `//! platform:`・`//! test:`・`//! sv:`ディレクティブの異常系 | 調査済み → [R7](../../archive/v0.17.0/attribute-validation-registry.md)（修正済み）・[R14](syntax-error-diagnostic-quality.md) | 正常系はSVスイートで常用 | 不正`//! test:`が常時PASSの空テストベンチに化ける、platformタイポの診断品質難 |

### B. 構文・式

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| B1 | async/await | 調査済み → 健全 | jsの基本テストのみ | js/ts動作・他経路は明示拒否。await式の型も正しい（軽微: check非対称・全角括弧混入は[R14](syntax-error-diagnostic-quality.md)） |
| B2 | macro宣言（定数マクロ・関数マクロ） | 調査済み → [R10](checker-silent-holes.md) | 基本テストあり | 正常系は6経路一致で健全。型不一致マクロがcheck素通り（Medium） |
| B3 | ラムダの参照キャプチャ`[&x]` | 調査済み → [R4](../../archive/v0.17.0/closure-mutation-semantics.md)（修正済み）・[R13](unimplemented-documented-syntax.md) | なし | `[&x]`構文は未実装。クロージャ書き込みは診断拒否へ修正済み |
| B4 | raw string（`r"..."`/`r#"..."#`） | 調査済み → [R5](../../archive/v0.17.0/string-escape-and-raw-semantics.md)（修正済み） | あり（escape_sequences） | `r"..."`構文は不採用（仕様書から削除）。バッククォートraw文字列は非エスケープ化済み（`` \` ``のみ例外） |
| B5 | エスケープ識別子（バッククォート`` `名前` ``） | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（バッククォートはraw文字列に割当・診断あり） |
| B6 | エスケープシーケンス`\xHH`/`\uHHHH`/`\UHHHHHHHH` | 調査済み → [R5](../../archive/v0.17.0/string-escape-and-raw-semantics.md)（修正済み） | あり（escape_sequences） | デコード実装済み・未知エスケープは診断拒否 |
| B7 | 数値リテラルの桁区切り`_`・型サフィックス（u/l/f/d等） | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（拒否は安全側・誤値化なし）。基数リテラル値は健全 |
| B8 | `${...}`形式の文字列補間 | 調査済み → [R5](../../archive/v0.17.0/string-escape-and-raw-semantics.md)（修正済み） | あり（escape_sequences） | 補間本体は`{}`と同値で健全。`\${x}`のリテラル出力対応済み |
| B9 | タプル型・タプル式 | 調査済み → [R13](unimplemented-documented-syntax.md) | 専用テストなし | 未実装（文法書と乖離・診断あり・黙殺なし） |
| B10 | 参照型`T&` | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（CANONICAL_SPEC §3.2で明記済み・診断あり） |
| B11 | 演算子オーバーロードの`[]`/`()` | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（operator_symbolに`[]`/`()`なし・診断あり） |
| B12 | `overload`メソッド（コンストラクタ以外） | 調査済み → [R13](unimplemented-documented-syntax.md) | コンストラクタのみ | 仕様書§4が実装済みと例示するがパース不能+inherentで黙殺（Medium） |
| B13 | インターフェースのデフォルト実装（本体付き宣言） | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（診断あり） |
| B14 | ユーザー定義関数の可変長引数`...` | 調査済み → [R13](unimplemented-documented-syntax.md) | FFIのprintf経由のみ | 未実装（文法書と乖離・FFI専用・診断あり） |
| B15 | matchの範囲パターン`a...b` | 調査済み → [R12](match-pattern-and-flow-gaps.md) | なし（masked_patternはあり） | `...`範囲は健全（両端含む・9実行一致）。負数パターン不可（Medium） |
| B16 | ジェネリクスのデフォルト型引数`<T = int>` | 調査済み → [R13](unimplemented-documented-syntax.md) | なし | 未実装（診断あり） |
| B17 | const genericパラメータの境界・演算 | 調査済み → [R10](checker-silent-holes.md) | 基本テストあり | 宣言のみ無警告受理・実体化手段なしの半黙殺（Medium） |

### C. 修飾子・宣言

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| C1 | `constexpr` | 調査済み → [R11](modifier-implementation-gaps.md) | なし | 変数はパーサTODOのnullptr返しで壊れた診断・関数は無警告で通常関数化（Medium） |
| C2 | `inline`修飾子/`#inline` | 調査済み → [R11](modifier-implementation-gaps.md) | なし | キーワードは無警告黙殺（IR不変）、`#inline`は仕様記載ありなのに拒否 |
| C3 | `volatile` | 調査済み → [R11](modifier-implementation-gaps.md) | asm最適化テストの付随のみ | パーサ未対応で全位置構文エラー（最適化検証不能） |
| C4 | 語彙のみのキーワード（`mutable`/`namespace`/`template`/`typename`/`pub`/`from`） | 調査済み → 概ね健全 | なし | 識別子誤用は6語とも正しく診断（X5空表示バグ再発なし）。`namespace`は実装済み動作。他は診断で拒否 |
| C5 | `ufloat`/`udouble` | 調査済み → [R11](modifier-implementation-gaps.md) | なし | 受理・6経路値一致だがunsigned語義未実装（負値も無診断、Medium） |
| C6 | `extern`宣言（native一般） | 調査済み → 健全 | SVのextern_instance等のみ | native/jitで動作・未解決シンボル診断も明確。js経路のみ実行時エラー（Low） |
| C7 | デフォルト引数 | 調査済み → [R8](../../archive/v0.17.0/default-arg-prev-param-zero.md)（修正済み） | あり（functions/default_args等+R8回帰） | 部分省略・関数呼び出し既定式・メソッド既定は健全。前引数参照は診断拒否へ修正済み |

### D. 標準ライブラリ・ランタイム

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| D1 | TreeMap | 調査済み → 健全 | 機能テストあり | 1000件/10万件の挿入喪失なし・自動拡張が効く（6経路一致）。remove/走査APIは診断ありの未実装 |
| D2 | std::json | 調査済み → [R1](../../archive/v0.17.0/json-parser-robustness.md)（修正済み）・[R2](string-codepoint-byte-api-split.md) | libテストあり（json/mod_test.cm） | 無限ループ・甘い受理・\u破壊はR1で修正済み。非ASCII失敗（High）はR2未修正 |
| D3 | std::env/process/path/bytes/fs | 調査済み → 健全（+[R23](cross-target-ffi-capability-gaps.md)） | selfhost素振りのCI検証（S1〜S9） | env/process/path/bytes/fsは健全（バイナリ安全・不在/空区別・エラーパス非クラッシュ）。wasmで能力ガード欠如。配列添字OOB無検査（Low〜Medium・言語共通） |
| D4 | std::ioの対話入力（input/input_int等） | 調査済み → 健全（+[R9](stdlib-shipping-defects.md)） | なし | パイプ入力で正常・非数値でNone（M17適用）・クラッシュなし。ただし`std::io`窓口の入力再exportが解決不能 |
| D5 | native::sync/thread | 調査済み → [R22](export-on-impl-method-parse-error.md)・[R25](concurrency-optimizer-and-join-gaps.md) | 機能テストあり（tests/llvm/sync・thread） | ランタイム挙動は健全（実並行・Mutex排他・RwLock SIGILL回避・O2/O3無破壊）。高レベルAPIがexport-implパースエラーで全損（High）・spin-waitがO1+でコンパイル不能 |
| D6 | native::net/http | 調査済み → 健全 | 機能テストあり | 実行・接続拒否/DNS失敗のエラーパスとも健全（クラッシュ/ハングなし） |
| D7 | native::gpu（Metal） | 調査済み → 健全 | 機能テストあり | 実Metal実行成功・不正シェーダ/関数名の誤用診断も良好 |
| D8 | web::html・js::fetch/timer | 調査済み → 健全（+[R23](cross-target-ffi-capability-gaps.md)） | libテスト・jsスイートあり | web::htmlエスケープはXSS遮断（native/js一致）。js::timerのコールバックがint型で使用不能 |
| D9 | std::iterのadapters（map/filter等） | 調査済み → [R9](stdlib-shipping-defects.md)・[R3](../../archive/v0.17.0/generic-pointer-param-inference.md)（R3修正済み） | — | mod.cm自体がコンパイル不能（High・R9未修正）・swapのSIGSEGVはR3で修正済み。組み込みmap/filterは健全 |
| D10 | アロケータ差し替え（set_allocator_fns） | 調査済み → [R9](stdlib-shipping-defects.md) | M14実装時の検証のみ | 文字列/スライス/直接確保は経由・reset復帰も健全。Vector/HashMap/Queueが素通し（Medium） |

### E. バックエンド・ターゲット

| # | 調査項目 | 診断状況 | 統合テスト | 備考 |
|---|---------|---------|-----------|------|
| E1 | SVバックエンドの網羅バグ調査 | 調査済み → [R15](../../archive/v0.17.0/sv-test-verification-soundness.md)（修正済み） | 専用スイートあり（tests/sv） | `//! test:`期待値の非検証・assertのx楽観性は修正済み。数値意味論はnative↔SV一致で健全 |
| E2 | SV固有構文の異常系（`bit<N>`・always系・assign・`+:`・幅付きリテラル・`#[sv::*]`属性群） | 調査済み → [R16](sv-codegen-silent-invalid.md) | 正常系はSVスイートにあり | 不正構文の無診断受理・属性タイポでピン制約欠落（Medium）。正常系は健全 |
| E3 | UEFIターゲット | 調査済み → [R18](freestanding-nostd-enforcement-gaps.md) | 機能テストあり（tests/uefi） | 文字列連結のヒープ確保・間接呼び出しが制約をすり抜け（High）。正常系コンパイルは健全 |
| E4 | baremetal-arm/x86 | 調査済み → [R17](../../archive/v0.17.0/baremetal-arm-startup-broken.md)（修正済み）・[R18](freestanding-nostd-enforcement-gaps.md) | 機能テストあり（tests/baremetal、arm/x86二段ゲート） | armの起動コード型不一致は修正済み。x86正常系は健全 |
| E5 | TSバックエンド固有経路 | 調査済み → [R19](ts-bigint-number-generation.md)・[R20](interpolation-format-backend-divergence.md) | tests/tsと`tsc --noEmit`ゲート | long/ulongフィールド代入がTS2322・補間/書式のバックエンド分岐（Medium）。型注釈の大半は健全 |

## 全体複雑度レビュー（未実装のリファクタリング提案）

修正履歴の同族バグ分析（変換サイト欠落族・メソッド解決分裂族・名前逆算族）と全ソースの実測（サイト×変換種マトリクス・キー計算箇所の棚卸し・ランタイムdiff・関数長スキャン）に基づき、複雑すぎる実装をシンプルかつバグが再発しない構造へ変えるための提案。優先度順。

- [暗黙変換の統一ドライバ化](coercion-driver-unification.md) — 変換挿入が11サイトに手組み散在（全種連鎖は2サイトのみ・ユニオンは2方式併存・インターフェースupcastは変換系外で各バックエンド個別）。coerce_to_expected一本化＋upcastのMIR化＋checker受理との同表化で、B2→Y1〜Y3→Y5→Z5→Q3と続いた「受理したのに未挿入」バグ族を構造的に封止する（Critical）
- [モノモーフ化のフラット名逆算の完全廃止](mono-flat-name-elimination.md) — 可逆$エンコーダ（typekey）があるのにstruct_symbol_keyのsimple高速パスがネスト特殊化で曖昧フラット名を生成し、parse_flat_type_argsの誤逆算がQ2の真因。逆算器の削除とtypekey全面化でQ2族を表現不能にする（Critical）
- [メソッド解決の一元化](method-resolution-unification.md) — メソッド表キー計算が12箇所別実装・解決機構4系統・types/の全throw4件が内部エラー漏れ。正準キー関数＋resolve_method APIでQ1/Q4/Q5族を封止する（High）
- [型検査の解決結果をHIRへ引き渡す](checker-to-hir-resolution-handoff.md) — MemberExprが解決結果を捨てるためlower_member（単一関数1100行・全ソース最大）がcheckerの解決を全再導出。解決注釈の導入で再導出コードを削除する（Medium）
- [モジュールグラフのテキスト手術脱却](module-graph-ast-emission.md) — 構造化import後も包含判定は正規表現識別子スキャン・出力はスパン消し込み+改名テキスト複製のまま。判定のAST化→出力のAST化の2段で座標ズレ（X5同根）と誤包含を解消する（Medium）
- [型サイズ照会の一本化](layout-size-single-source.md) — サイズ実装が4系統（HIRの暫定256バイト・MIRのフィールド数×8見積もり・monoのフラット名依存・真実のlayout系）でsizeofが見積もりを答えうる。全照会をlayout API 1系統へ（Medium）
- [derive自動実装の残存MIR生成の整理](auto-impl-generic-gaps-and-cleanup.md) — 源展開移行後も約2,970行のMIR直生成が残存（非ジェネリック分は死に体）。ジェネリックパスはSlice/Unionフィールド未対応で無言誤動作。削除→ギャップ封鎖→単一ソース化完遂の3段（Medium）
- 配列HOFランタイムの共通ソース化 — **実施済み**（[archive移動](../../archive/v0.17.0/runtime-hof-common-source.md)。__builtin_array_*全54関数+ヘルパーをcommon/runtime_hof_core.inc（826行）へ抽出しCM_RT_*フックで両target包含（native 3,058→2,255行・wasm 2,888→2,129行）。wasm sort系のalloc失敗リーク解消・ソートのcm_qsort統一を含む。check_builtin_signatures.pyへ共通コア重複定義検査（111件）を追加し再発を機械防止。文字列フォーマット系はwasm SDS化まで非対象の既存判断を維持）

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
- [syntax-audit-bugfixes.md](../../archive/v0.17.0/syntax-audit-bugfixes.md) — 構文網羅検証（B1〜B9の総括）
- [move-closure-interp-audit.md](../../archive/v0.17.0/move-closure-interp-audit.md) — move・クロージャ・補間式添字の検証（V1〜V8の総括）
- 5バックエンド差分プローブ（N1〜N8）の個別文書: [interp-nested-slice-index.md](../../archive/v0.17.0/interp-nested-slice-index.md)・[generic-slice-element-garbage.md](../../archive/v0.17.0/generic-slice-element-garbage.md)・[string-switch-miscompile.md](../../archive/v0.17.0/string-switch-miscompile.md)・[wasm-reduce-closure-trap.md](../../archive/v0.17.0/wasm-reduce-closure-trap.md)・[generic-struct-literal.md](../../archive/v0.17.0/generic-struct-literal.md)・[enum-multi-payload-match.md](../../archive/v0.17.0/enum-multi-payload-match.md)・[negative-radix-format.md](../../archive/v0.17.0/negative-radix-format.md)・[js-string-index-bigint.md](../../archive/v0.17.0/js-string-index-bigint.md)
- native/jit網羅検証（W1〜W5・X1〜X6）の個別文書: [nested-anonymous-struct-literal-loss.md](../../archive/v0.17.0/nested-anonymous-struct-literal-loss.md)・[nested-slice-element-write-crash.md](../../archive/v0.17.0/nested-slice-element-write-crash.md)・[slice-struct-pop-value-crash.md](../../archive/v0.17.0/slice-struct-pop-value-crash.md)・[licm-global-clobber-miscompile.md](../../archive/v0.17.0/licm-global-clobber-miscompile.md)・[interp-chain-lowering-failures.md](../../archive/v0.17.0/interp-chain-lowering-failures.md)・[static-block-scope-init-loss.md](../../archive/v0.17.0/static-block-scope-init-loss.md)・[private-field-access-unchecked.md](../../archive/v0.17.0/private-field-access-unchecked.md)・[slice-push-array-literal-corruption.md](../../archive/v0.17.0/slice-push-array-literal-corruption.md)・[slice-push-anonymous-struct-literal.md](../../archive/v0.17.0/slice-push-anonymous-struct-literal.md)・[syntax-error-position-and-token-display.md](../../archive/v0.17.0/syntax-error-position-and-token-display.md)・[private-method-cross-impl-visibility.md](../../archive/v0.17.0/private-method-cross-impl-visibility.md)
- 個別バグ文書（B系ほか）: [must-block-field-assignment.md](../../archive/v0.17.0/must-block-field-assignment.md)・[int-literal-to-float-conversion.md](../../archive/v0.17.0/int-literal-to-float-conversion.md)・[nested-member-slice-chain.md](../../archive/v0.17.0/nested-member-slice-chain.md)・[cast-null-pointer-comparison.md](../../archive/v0.17.0/cast-null-pointer-comparison.md)・[interface-bound-method-return-type.md](../../archive/v0.17.0/interface-bound-method-return-type.md)・[interface-method-interpolation-type.md](../../archive/v0.17.0/interface-method-interpolation-type.md)・[typedef-struct-literal-resolution.md](../../archive/v0.17.0/typedef-struct-literal-resolution.md)・[defer-implicit-function-end.md](../../archive/v0.17.0/defer-implicit-function-end.md)・[const-global-aggregate-init.md](../../archive/v0.17.0/const-global-aggregate-init.md)・[uninitialized-struct-fields.md](../../archive/v0.17.0/uninitialized-struct-fields.md)
