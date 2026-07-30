# Cm構文→LLVM IR対訳リファレンス

Cmの全構文について、O3でLLVM IRへ変換される過程と実際の出力を構文ごとに対訳形式で記載した文書群。全IR抜粋はビルド済み`cm`の`CM_DUMP_IR=1/2`による実測で、O0との対比・O3で消滅する場合の畳み込み結果まで示す。設計の深掘りは各文書から[アーキテクチャドキュメント](../index.md)の該当文書へリンクする。なおimport・moduleはプリプロセッサ展開でIRに現れないため[モジュール解決](../modules/import-resolution.md)を、`#ifdef`は[条件付きコンパイル](../pipeline/conditional-compilation.md)を参照。

## declarations — 宣言と代入

| ドキュメント | 対象構文 |
|---|---|
| [変数宣言](declarations/var-decl.md) | `int a;`（allocaのみ・読み出しundef）、`int a = 1 + 1;`（O3で`ret i32 2`へ畳み込み）、const・auto、集約ローカルのalloca+memset |
| [代入と複合代入](declarations/assignment.md) | `=`、複合代入10種の命令対応（`/=`のゼロ除算ガード含む）、`a++`/`--a`の`add ..., 1`化と式値の差 |
| [グローバル宣言](declarations/global-decl.md) | internal/外部linkage、`constant`とO3即値化、非定数初期化子のmain先頭store方式 |
| [typedefとマクロ](declarations/typedef-macro.md) | typedefのIR完全透過、定数マクロのインライン置換、関数マクロの実関数化 |

## literals — リテラル

| ドキュメント | 対象構文 |
|---|---|
| [数値リテラル](literals/numeric-literals.md) | 10進・0x/0b/0o、文脈型決定、浮動小数、bool（i8）、char、負数の定数化 |
| [文字列リテラル](literals/string-literals.md) | SDSヘッダ付き`@strh`定数と+16オフセットGEP、エスケープ、空文字列、定数共有 |
| [集約リテラル](literals/aggregate-literals.md) | 配列`[1,2,3]`のstore列、スライスの`cm_slice_new`+push構築、構造体リテラルのSROA収束 |

## operators — 演算子

| ドキュメント | 対象構文 |
|---|---|
| [算術演算](operators/arithmetic.md) | `+ - * / %`の命令対応、ラップ意味論（nsw/nuw無し）、ゼロ除算検査、式文`1 * 2;`の消滅 |
| [比較と論理](operators/comparison-logical.md) | icmp/fcmp述語対応表、文字列のcm_strcmp化、`&&`の短絡分岐とO3 select平坦化 |
| [ビット演算](operators/bitwise.md) | and/or/xor、`~`のxor -1化、ashr/lshr符号分岐、シフト量の拡幅規則 |
| [三項演算子](operators/ternary.md) | O0分岐+合流とO3 select/smax縮約、構造体腕のフィールド単位select分解 |
| [asキャストとis](operators/cast-is.md) | sext/zext/trunc/sitofp/fptosi.sat/ptrtoint等の実IR、`is`のタグ比較 |
| [演算子オーバーロード](operators/operator-overload.md) | `Type__op_add`等へのcall化、`>`/`!=`の`<`/`==`からの導出、O3インライン消滅 |

## control-flow — 制御フロー

| ドキュメント | 対象構文 |
|---|---|
| [if-else](control-flow/if-else.md) | 分岐ブロックとbr、O3でのselect化・ブランチレス化と分岐が残る条件 |
| [ループ](control-flow/loops.md) | while/for/while(true)のヘッダ・ラッチ・エグジット構造、break/continue、閉形式評価 |
| [for-in](control-flow/for-in.md) | スライス・固定長配列（完全アンロール）・イテレータの3形態の展開 |
| [switch](control-flow/switch.md) | LLVM switch命令への写像、OR/範囲パターン、ルックアップテーブル化 |
| [match](control-flow/match.md) | enumタグ分岐のswitch再構成、ペイロード抽出、ガード・束縛・ワイルドカード |
| [return・defer・must](control-flow/return-defer-must.md) | ret・common.ret統合、deferの逆順複製、mustのno_optフラグとasmバリア |

## functions — 関数

| ドキュメント | 対象構文 |
|---|---|
| [関数宣言と呼び出し](functions/function-decl-call.md) | mainのargc/argv、スカラ値渡し・16バイト超ポインタ渡し・sret、デフォルト引数の呼び出し側補完 |
| [ジェネリック関数](functions/generic-functions.md) | `<T>`定義から`f__int`等への単相化、明示特殊化、制約付き呼び出し |
| [ラムダ](functions/lambdas.md) | `(int x) => {...}`の`__lambda_N`独立関数化、キャプチャ前置引数、間接呼び出しの脱仮想化 |
| [メソッド呼び出し](functions/method-calls.md) | `Type__method(self,...)`脱糖、チェーンの一時実体化、`return self`ビルダー |

## structs — 構造体

| ドキュメント | 対象構文 |
|---|---|
| [宣言とアクセス](structs/struct-decl-access.md) | `%struct名`型、GEPパターンとSROA、`self()`コンストラクタ、特殊化型名`%Pair__int__string` |
| [コピーとRAII](structs/struct-copy-raii.md) | 小型load/store・大型memcpyの2系統、sret、`__dtor`の挿入位置、move |

## enums-unions — enum・ユニオン型

| ドキュメント | 対象構文 |
|---|---|
| [enum](enums-unions/enum.md) | 単純enumのi32表現、データ付きvariantの`{i32 tag, [N x i8]}`構築、matchのタグ分岐 |
| [ユニオン型](enums-unions/union-types.md) | `int \| string`の構築store列、`is`の1命令化、`as`のタグ検査付き取り出し |
| [Result・Option](enums-unions/result-option.md) | Ok/Err/Some/Noneの構築、`?`のタグ抽出→早期return、unwrapのpanic分岐 |

## interfaces — インターフェイス

| ドキュメント | 対象構文 |
|---|---|
| [interfaceとimpl](interfaces/impl.md) | `impl A for B`が生成するシンボル群とvtable定数、implブロックがIRに現れない事実 |
| [ディスパッチ](interfaces/dispatch.md) | 境界経由の直接call+インライン化と、fat pointerのvtableロード+間接callの対比、O3脱仮想化 |
| [deriveとwith](interfaces/derive-with.md) | `with Eq, Ord, Debug`が合成する`__op_eq`/`__op_lt`/`__debug`関数の実IR |

## strings-slices — 文字列とスライス

| ドキュメント | 対象構文 |
|---|---|
| [文字列操作](strings-slices/string-ops.md) | 連結のconcat/concat3/4、比較、補間の`cm_format_*`連鎖、メソッドの`__builtin_string_*`、println |
| [スライス操作](strings-slices/slice-ops.md) | `cm_slice_new`+push、添字読み書きの非対称、サブスライス、二次元、ランタイム境界がO3でも残る事実 |

## pointers-ffi — ポインタとFFI

| ドキュメント | 対象構文 |
|---|---|
| [ポインタ操作](pointers-ffi/pointers.md) | `&`/`*`/`->`のGEP+load/store、ポインタ演算のスケーリング、null比較、関数ポインタ間接call |
| [FFI呼び出し](pointers-ffi/ffi-calls.md) | `use libc`宣言のdeclare化、string→char*渡し、malloc/freeの連結リスト、可変長引数 |
