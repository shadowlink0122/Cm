---
title: 暗黙変換の統一ドライバ化（変換挿入サイト散在の構造的解消）
parent: v0.17.0 Design
---

# 暗黙変換の統一ドライバ化（変換挿入サイト散在の構造的解消）

## 概要

暗黙変換のMIR挿入がヘルパ3種（coerce_numeric_context・coerce_to_union・coerce_fixed_array_to_slice）を消費サイトごとに手組みで連鎖する構造になっており、全種を連鎖しているのは引数・デフォルト引数の2サイトだけである。
B2・Y1〜Y3・Y5・Z5・Q3と続いた「受理されるのに変換が挿入されないサイトがある」バグ族は、この散在構造の直接の帰結であり、サイト個別修正では次の変換種追加時に必ず再発する。
消費サイトを実測したところ11サイトあり、ユニオン変換はヘルパ経由とインラインCast直書きの2方式が併存し、配列→スライスはヘルパを呼ぶのが2サイトのみで3サイトはインライン再実装、インターフェースupcastに至っては変換系に存在しない。

## 現状マトリクス（サイト×変換種の実測）

| サイト | numeric | union | array→slice | interface |
|---|---|---|---|---|
| let初期化（スカラ） stmt/let.cpp:658 | ✅ | ⚠️インラインCast | ✗ | ✗ |
| letスライスリテラル要素 stmt/let.cpp:464 | ✗（別インライン） | ✅ | ✗ | ⚠️インライン一時 |
| 単純代入 stmt/assign.cpp:121 | ✅ | ⚠️インラインCast | ✗ | ✗ |
| メンバ/添字/deref代入 stmt/assign.cpp:141 | ✅ | ✗ | ✗ | ✗ |
| return stmt/control.cpp:100 | ✅ | ✅ | ⚠️インライン別実装:48-98 | ✗ ←Q3 |
| 構造体リテラルフィールド expr/construct.cpp:217 | ✅ | ✅ | ✗ | ✗ |
| push expr_slice.cpp:148 | ✗ | ✅ | ⚠️インライン | ⚠️インライン一時 |
| 呼び出し引数 expr_call.cpp:271 | ✅ | ✅ | ✅ | ✗ |
| デフォルト引数 expr_call.cpp:352 | ✅ | ✅ | ✅ | ✗ |
| 複合代入RHS expr/binary.cpp:419 | ✅ | ⚠️インラインCast | ✗ | ✗ |

インターフェースupcastはMIRの変換ではなく、(a) H1のインライン一時変数パターン（stmt/let.cpp:472-486とexpr_slice.cpp:177-191に逐語複製）と、(b) 各バックエンドのassign認識（LLVM statement/assign.cpp:79,142,232のcreateInterfaceFatPtr・JS emit_statements.cpp:112-134）に分裂している。
さらに戻り値fat pointerのheap-boxing（スタックローカルへのfat pointerがdangleする問題の対処）はLLVMのassign.cpp:58-77にしか存在せず、Q3（インターフェース戻り値）がバックエンドごとに別の壊れ方をする真因になっている。
checker側の受理判定（utils/compat.cpp・utils/conversion.cpp）とMIR側の挿入も独立実装であり、conversion.cpp冒頭コメントが2実装分離を明言している——受理と挿入が乖離できる構造そのものが無言ミスコンパイルの温床である。

## リファクタリング方針

1. **統一ドライバ**: `LocalId LoweringContext::coerce_to_expected(LocalId value, const hir::TypePtr& expected)` を新設し、全11サイトを「値を作る→coerce_to_expected→格納」の1形へ集約する。
2. **再帰的ディスパッチ**: フラットなnumeric→union→sliceの固定順ではなく、宛先がユニオンならまず変種を解決して値を変種型へ再帰的にcoerceしてからwrapする（ユニオンofスライス・ユニオン内インターフェース・変種内numeric正規化が固定順では壊れるため）。
3. **インターフェースupcastのMIR化**: fat pointer構築をMIRの構築物（Rvalueまたは専用ヘルパ）へ昇格し、return時のheap-boxingはフラグ1つで表現して、LLVM/JS/SV/interpreterのassign認識コードを撤去する。これがQ3の恒久修正を包含する。
4. **受理と挿入の同表化**: checkerが受理した変換種をHIRノードへ注釈し、loweringは注釈を読んで挿入するだけにする（再導出の廃止）。Z5のclassify_numeric_conversionを変換種全体（numeric/union/slice/interface）へ拡張した`conversion_kind`表を単一の真実とする。

## 段階分割

- 第1段（低リスク・高効果）: coerce_to_expected導入と11サイトの置換。インラインCast方式のユニオン3サイト・インライン再実装のarray→slice 3サイトをヘルパへ吸収する。挙動変更なしの純リファクタリング。
- 第2段（Q3修正を包含）: インターフェースupcastのMIR構築物化とreturnのbox統一。4バックエンドのassign認識を削除し、interface-return-fat-pointer.mdのバグをこの機構で修正する。
- 第3段: checker注釈駆動化（受理した変換のHIR記録）。types_compatibleの構造変換分岐を注釈生成へ置き換え、受理・挿入の2実装を1表に畳む。

## テスト計画

- 変換種（numeric/union/slice/interface）×サイト（let/代入/メンバ代入/return/フィールド/push/引数/デフォルト引数/複合代入）のマトリクス回帰を6経路（jit O0/O2・native O0〜O3）+wasm/jsで追加する。
- 再帰ケース: ユニオンofスライス変種への固定長配列代入・ユニオン内インターフェース変種・変種への縮小numericを明示的に検証する。

## 検出経緯

全体複雑度レビュー（2026-08-05）で実測。バグ族の系譜はB2→Y1〜Y3→Y5→Z5→Q3で、いずれも「新しい変換種や新しいサイトが手動連鎖から漏れる」同型である。
## 実装記録（第1段・2026-08-08）

第1段（coerce_to_expected導入と11サイト置換）を実施した。挙動変更なしの純リファクタリングとして全13スイートPASSを確認済み。

- `LoweringContext::coerce_to_expected(LocalId value, const hir::TypePtr& expected)`をcontext.cppへ新設し、11サイト全てを「値を作る→coerce_to_expected→格納」の1形へ置き換えた。インラインCast方式のユニオン3サイト（let/単純代入/複合代入。宛先place直接Castだったが意味論同一のためヘルパ経由の一時+copyへ統一）を吸収し、メンバ/添字/deref代入サイトはnumericのみ→全変換種対応になった。
- 再帰的ディスパッチ（方針2）を保守的に実装した: 宛先がユニオンで値の型に一致する変種がある場合は従来通り事前変換なしでwrap（既存挙動維持）。一致変種が無い場合のみ、固定長配列→唯一のスライス変種の実体化・唯一の数値変種への正規化を事前に行ってからwrapする。
- returnの配列→スライスインライン実装（control.cpp:48-97、return_local直接ターゲット）とpushの多次元リテラル専用経路（expr_slice.cpp）は専用の後段/前段として残置した（ドライバの汎用経路と役割が重ならないことを確認済み）。let要素・構造体フィールドの配列リテラル専用経路も同様に残置（棚卸しの11サイト外）。

**第1段で発見した既存バグ（第2段の入力。いずれも本リファクタリング前のバイナリで同一再現・jsは正値でLLVMバックエンドのユニオン実装に帰属）:**
1. 複数のユニオン関数を含むファイルでのユニオンreturnペイロード誤読: `IntOrStr ret_union(int x) { return x; }`を他のユニオン引数関数と同居させて呼ぶと`r as int`が1を返す（単独ファイルでは正値5。MIRは`as <union>`の正しい構築列を出力しており、LLVM側のユニオンローカル/型の取り回しが真因）。tests/common/types/union_coerce_sites.cmはこのためreturnはタグのみ検証している。
2. 数値変種の事前正規化がバックエンドで無効化される: `(double|string) d = 1;`はドライバ後のMIRで`copy as double`→`as <union>`の正しい列になるが、native/jitの実行結果はペイロードが生intビット（5e-324）のまま（jsは1.0で正値）。LLVMのユニオン構築Castが変換済み一時でなく元の値を読んでいる可能性が高い。
3. `is`のスライス変種検査がbool以外を出力する: `s is int[]`が-1342177280等の生値を返す（タグ比較のlowering欠落）。

**checker側の受理乖離（第3段の入力）:** ユニオン引数への変種リテラル直接渡し（`take_union(9)`）は`Argument type mismatch`で拒否（`as`付きは通る）、typedef経由の変種as（`s as IntSlice`）は「not a variant」拒否。MIR側は対応済みのため、受理表の同表化（方針4）で解消する領分。

残り: 第2段（インターフェースupcastのMIR化+上記LLVMユニオンバグ群の修正）・第3段（checker注釈駆動化）。
## 実装記録（第2段の一部・2026-08-08）

第1段で記録した「LLVMユニオンの既存バグ3件」を修正した。**真因の訂正: 3件ともLLVMバックエンドのユニオン実装ではなく、MIR最適化とパーサの欠陥だった**（第1段の帰属仮説は誤りで、IRを直接検分して確定した）。

- **バグ1・2の真因はGVNのCSEキー欠陥**: `GVN::stringify_rvalue`のCastキーが（オペランド, 宛先型）のみでcheck_onlyフラグを含まず、`r is int`（check_only=true・bool結果）と`r as int`（ペイロード抽出）が同一値番号に統合されていた。isの真偽値（true=1）がasの結果として再利用され、returnペイロードが1になり（バグ1）、数値変種の`as double`はbit値1のdouble解釈=5e-324になっていた（バグ2）。キーへ`,is`を追加して修正。ConstantFoldingにも同種の混同（check_only Castの定数畳み込みが値変換として評価される）が潜在していたためガードを追加した。
- **バグ3の真因は二層**: (1)パーサの`is`/`as`型右辺がスライス型`int[]`を受けず構文エラーだった——空ブラケット`[]`のみ型サフィックスとして消費する（`[i]`は従来通り`(expr as T)[i]`の添字式）。(2)補間プレースホルダのパース失敗が無診断で`<error>`型ローカル（未初期化値出力）に落ちていた——横断的な黙殺穴で、`{x +}`等のタイポも同様だった。MIRフォールバックを「`{内容}`のリテラル文字列出力」へ変更し、checkerが脱糖時に警告（--strictエラー。脱糖はmatch_hoistプリパスが先行するためリテラルへ失敗を記録して報告）を出す。
- 回帰: union/coerce_sites.cmへreturnペイロード（is/as併用）・数値変種正規化・isスライス変種の検証を復元し、native/js出力一致を確認。全13スイートPASS。

第1段記録の「LLVMユニオンStructType生成3系統（8バイト固定fallback含む）・サイトごとのタグ再導出」は静的には依然存在するが、今回の3バグの真因ではなかった。実害の証拠が出た時点で対処する（現時点で全スイート・全マトリクスは正値）。

残り: 第2段の本丸（インターフェースupcastのMIR構築物化・returnのheap-boxingフラグ統一・4バックエンドのassign認識撤去）・第3段（checker注釈駆動化。受理乖離2件=ユニオン引数リテラル直渡し・typedef経由の変種asもここで解消）。
## 実装記録（受理乖離2件の解消・2026-08-08）

第3段（受理と挿入の同表化）の先行断片として、第1段で記録したchecker受理乖離2件（MIR側は対応済みなのにcheckerが誤拒否する偽陽性）を解消した。

- **ユニオン型引数への変種リテラル直接渡し**（take_union(9)がArgument type mismatch）: 真因はtypes_compatibleがtypedefエイリアスを解決せず、letサイトは呼び出し側で個別にresolve_typedefしてから比較する一方、引数検査は未解決のまま渡すため`typedef IntOrStr = int | string`型パラメータのユニオン受理分岐に到達しなかったこと。types_compatible先頭でのtypedef実体解決へ一元化し、呼び出し側の個別resolveへの依存を廃した。
- **typedef別名経由の変種as/is**（s as IntSliceがnot a variant）: is/asの変種照合が未解決のtypedef名の文字列比較だった。対象型・変種の両側を実体解決してから照合するようにした（IntSlice = int[]がint[]変種と一致する。負例のas_nonvariant等のエラーテストは全て維持）。
- 回帰: union/coerce_sites.cmを変種リテラル直接渡し・typedef別名経由のas抽出込みへ更新（native/js一致・全13スイートPASS）。

残り: 第2段の本丸（インターフェースupcastのMIR化・4バックエンドassign認識撤去・return heap-boxing統一）・第3段本体（checkerが受理した変換種のHIR注釈化とconversion_kind表の単一真実化）。

## 実装記録（ユニオン変種の実体化・照合の是正・2026-08-11）

ユニオン等値比較の修正時に実害化した2件（第1段記録の周辺欠陥）を処置した。

- **スライス変種の実体化スキップ**: `coerce_to_expected` の完全一致判定がkindのみの比較だったため、固定長配列（`int[3]`）がスライス変種（`int[]`）に完全一致扱いされ、第1段で導入した「固定長配列→唯一のスライス変種の実体化」が一度も発火していなかった（生の配列データがペイロードへbitcast格納され、native/jitの抽出 `u as int[]` がゴミ値のスライスヘッダになる）。完全一致判定へ `array_size.has_value()` の一致（動的/固定長の別）を追加した。あわせて明示 `as` キャストのユニオン向け経路（`lower_cast`）を統一ドライバ `coerce_to_expected` へ委譲し、let/代入/asの3経路が同一の実体化＋wrapを通るようにした。LLVMのユニオン変種サイズ計算（types.cpp）へArrayケース（動的=ポインタ8バイト・固定長=実サイズ）も追加した。
- **null変種の誤タグ**: nullリテラルの型はcheckerで`void`のため、構築時の変種照合（kind一致）がNull変種にヒットせずタグ0へフォールバックしていた。変種照合の3箇所——MIR統一ドライバ（`coerce_to_expected`の完全一致判定）・LLVM構築（rvalue.cppのタグ照合）・JS構築（`computeUnionTag`）——へvoid→Null写像を追加した（void型の値式はnullリテラル以外に存在しないため安全。checkerのnullリテラル型をNull化する全面変更はポインタnull互換への波及があるため採らなかった）。
- 回帰テスト: `tests/common/types/union/slice_variant.cm`（as/let/動的スライスからの構築・判別・抽出）・`null_variant.cm`（null/int変種の構築とis判別・再代入）・`equality.cm`（スライス変種の内容比較を含む14ケース）。いずれもinterpreter/llvm/jsの3バックエンド一致。
- 残: 第2段の本丸（インターフェースupcastのMIR構築物化・4バックエンドassign認識撤去・return heap-boxing統一）・第3段（checker注釈駆動化）は未着手のまま。
