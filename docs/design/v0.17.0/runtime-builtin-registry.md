---
title: ランタイムビルトインのレジストリ化（文字列名分散と多重宣言の解消）
parent: v0.17.0 Design
---

# ランタイムビルトインのレジストリ化（文字列名分散と多重宣言の解消）

## 概要

ランタイム関数（cm_slice_*・cm_string_*・__builtin_*等）の名前とシグネチャが、宣言・写像・実装の各所に文字列として独立に列挙されており、1関数の追加・変更に最大7箇所の同期が必要になっている。
単一のビルトインレジストリ（名前・シグネチャ・引数規約・各バックエンド写像を持つ表）を導入し、宣言と写像を表から導出する。

## 現状の実測と問題

同期が必要な列挙箇所:

1. LLVM宣言: declareExternalFunction のelse-if連鎖117分岐（core/runtime/builtins.cpp）。
2. jsビルトイン判定: kBuiltinNames相当の文字列集合322件+emitBuiltinCallのif連鎖（js/builtins.cpp）。
3. MIRの既知HOF表: lowering.cppのクロージャ変換対象リスト。
4. nativeランタイム実装: runtime_format.c 2,935行+runtime_slice.c 1,165行。
5. wasmランタイム実装: 上記とほぼ同内容の二重実装（runtime_format.c 2,756行+runtime_slice.c 1,232行、計8,088行が対）。
6. FFI用のCm側宣言（libs/std配下のuse libc）。
7. インタプリタ/JITのシンボル解決前提（非inline実体の要求）。

同期漏れの実績: N4はacc64版reduce追加時に1〜5を個別に足す必要があった実例で、H5では println系の写像先（js）だけを変えて型再解釈を入れる必要があった。
宣言が実装と独立なため、シグネチャ不一致はwasmのcall_indirect検査（N4）やABI残留値（V5）として実行時にしか現れない。

## 簡素化方針

1. ビルトインレジストリを1ファイルで定義する: `{名前, 戻り型, 引数型列, 引数規約(値/アドレス/スライスヘッダ), 分類(slice/string/print/hof), js写像テンプレート}` の定数表（X-macroまたはconstexpr表）。
2. declareExternalFunctionは表引き1関数に置換する（117分岐の削除）。jsのビルトイン判定・基本写像も表から導出し、特殊な整形（BigInt境界等）だけをフックとして残す。
3. MIRのHOFリスト・クロージャ変換対象は表の分類タグから導出する。
4. ランタイムCの二重実装解消: native/wasmで挙動が同一の関数（slice操作・format大半）は共通ソース（codegen/common/）へ一本化し、プラットフォーム差（アロケータ・出力・ページゲート）のみをフックで分ける。H9第4段のSDSゲート差・wasm32のint64_t幅規約（M14）のような既知の差分は明示フックとして表現する。
5. 表からCヘッダ（シグネチャ宣言）を生成してランタイム実装がincludeし、実装と宣言の不一致をコンパイル時に検出する。

## 段階分割

1. 第1段: レジストリ表の新設と、declareExternalFunctionの表引き化（挙動不変。表に無い名前は既存フォールバックを一時併用）。
2. 第2段: js側の判定・基本写像を表から導出し、名前集合322件の手書き列挙を削除する。
3. 第3段: 表からのCヘッダ生成をランタイムへ適用し、native/wasmのシグネチャ乖離をビルドエラー化する。
4. 第4段: ランタイムCの共通ソース化（slice系→format系の順。wasm固有のゲート・アロケータ差はフック化）。

## テスト計画

- 各段で全12スイート+O0検証を完走させる（挙動不変）。
- レジストリと実装Cの関数集合が一致することのビルド時検査（宣言生成ヘッダの適用）をCIに追加する。
- N4/V5系の回帰（シグネチャ不一致がビルド時に落ちること）を意図的な不一致ケースのnegativeテストで固定する。

## 進捗

### 第1段（レジストリ新設とLLVM宣言の表引き化）: 実装済み

- `src/internal/codegen/common/builtin_registry.hpp` にビルトインレジストリ（`BuiltinSig{name, symbol, ret, args, vararg}` の名前昇順constexpr表・188件・二分探索の`find_builtin_sig`）を新設した。表は旧else-if連鎖からの機械抽出で生成し、名前集合の新旧完全一致（欠落・過剰ゼロ）と代表エントリ（printf別名・vararg・bool型）の突き合わせで等価性を検証した。
- `declareBuiltinRuntimeFunction`（core/runtime/builtins.cpp）を表引き+型タグ→LLVM型写像の1関数へ置換した（720行→約60行、117分岐の削除）。シンボル別名（`__println__`→printf）はレジストリのsymbol列で表現する。
- 表に無い名前は従来どおりnullptrを返し呼び出し元の後続解決へ委ねる（挙動不変）。

### 第2段（js側の導出）: 名前集合のみ実装済み

- jsの`isBuiltinFunction`をレジストリ照合+js固有42件（i64系HOF・StringBuilder・アロケータ・format_N等、LLVM側は別経路宣言）の小集合へ書き換え、154件の重複列挙を削除した。
- `emitBuiltinCall`の写像if連鎖の表駆動化と、js固有42件のレジストリ収容（シグネチャ定義）は未実施。

### 第3段（シグネチャ乖離の自動検査）: 実装済み

- `scripts/check_builtin_signatures.py` を新設し、レジストリ表とnative/wasmランタイムC実装の関数シグネチャ（戻り型・引数の幅と個数・可変長）を突き合わせる検査を `make lint` とCIのLintジョブへ組み込んだ（レジストリ照合297件・native/wasm二重実装の相互照合191件、同名の重複定義も全件検査）。
- 原案の「Cヘッダ生成をランタイムへ適用」はポインタのpointee型（char*/CmSlice*等）が実装ごとに異なりC言語の再宣言互換で誤検出になるため、幅正規化（ポインタ→Ptr・スカラ→幅タグ）でのlint時突き合わせへ設計変更した。検出対象の本質（N4/V5/M14族の幅・個数・可変長乖離）は同等に検出でき、native/wasm二重実装同士の乖離検査も追加で得られる。boolのI1/I8表現差は互換として扱い、wasmエクスポートでのsize_t等の可変幅型は警告する。
- 検査で実乖離2件を検出し修正した: (1) `__builtin_array_findIndex_i64` のnative実装がint64_t戻り（LLVM宣言・closure変種・wasm実装はi32。ABIの偶然で動作していた）→i32へ統一 (2) wasmの `cm_format_string` スタブがargc引数を欠いていた→レジストリ宣言（fmt, argc, ...)へ整合。
- negative検証: 意図的な不一致（cm_slice_lenのi32戻り定義）でレジストリ照合・相互照合の両方が検出することを確認した。

### 第2段後半（jsの写像表駆動化）: 不採用（設計判断）

- `emitBuiltinCall`の実装を精査した結果、各caseはBigInt再解釈（asIntN/asUintN境界）・process.exitフォールバック・fmt_double正規化などjs固有の意味論を持ち、「基本写像を表から導出し特殊整形をフックに残す」と大半がフック側に残るため、表駆動化は複雑さの移動にしかならないと判断し不採用とした。名前集合の導出（isBuiltinFunctionのレジストリ照合化）までを第2段の成果とする。
- js固有42件のレジストリ収容も、それらのLLVM宣言が別経路（system.cpp・MIRシグネチャ）にあり、収容には各宣言経路との優先順位整理が必要なため見送った（シグネチャ検査の対象拡大として将来検討）。

### 残り（第4段）: 未実装

ランタイムCの共通ソース化（native/wasm 8,088行の一本化。slice系→format系の順、wasm固有のゲート・アロケータ差はフック化）。
