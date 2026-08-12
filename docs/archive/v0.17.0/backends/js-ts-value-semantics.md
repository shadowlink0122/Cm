---
title: JS/TSバックエンドの値セマンティクスと64bit整数表現の統一
parent: v0.17.0 Design
---

# JS/TSバックエンドの値セマンティクスと64bit整数表現の統一

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H3 | 型システム | js/tsのみ構造体の関数引数が参照渡し（let束縛とreturnは`__cm_clone`するのに呼び出し引数だけクローンしない）、値セマンティクスがバックエンドで分裂 | 実装済み（実引数生成の2ループへ`structArgNeedsClone`判定を適用。implメソッドのself・仮想ディスパッチのレシーバ・インターフェイス値・外部JSオブジェクト・ランタイム組み込み（indexOf等はJSのオブジェクト同一性に依存）はクローン対象外。テスト: tests/common/functions/struct_arg_value_semantics.cm） |
| H5 | バックエンド | `long`/`ulong`がjs/tsでNumber表現のため2^53超で黙って精度喪失（64bit ID・タイムスタンプが破損） | 実装済み（long/ulong/isize/usizeの実行時表現をBigIntへ移行。リテラルは123n形式、算術/ビット演算/シフトはBigInt.asIntN/asUintN(64)でLLVM系のラップ挙動へ一致、除算はBigIntのゼロ方向切り捨てでsdiv一致。等値・順序比較は両辺を64bit幅へ符号再解釈して比較（ulongの-1リテラル==最大値等もnative一致）。Number混在（len()等の32bit系戻り値との演算）は冪等ヘルパー__cm_big/__cm_truncで吸収し、呼び出し境界（Cm関数の仮引数型・スライスpush/setのスロット幅）で明示変換を挿入。キャストはasIntN/asUintN(幅)→Numberで正確に縮小。出力はString(BigInt)で整形（console.log直渡しの10n表記を回避）、TS型注釈はbigintへ写像（tscターゲットes2020）。union実行時型検査・sort比較子・文字列添字ヘルパーもBigInt対応。2^53超の精度・64bitラップ・混在演算・キャスト境界をjit/native/wasm/js/tsの5系一致で回帰固定: tests/common/types/long_bigint_precision.cm。従来jsスキップだったulong_large_hex・bytes_endian（64bit往復）もスキップ解除で全緑） |

## 背景と根本原因

JSとTSは別実装ではなく、単一の `JSCodeGen` が `emitTypeScript` フラグ（`--target=ts`）で型注釈出力のみを切り替える完全共有実装である（`src/internal/codegen/js/codegen.hpp:25`）。
したがってH3・H5はJS/TS両方に等しく影響する。

### H3: 構造体クローンが代入経路のみで、呼び出し引数に無い

`__cm_clone` は再帰的な深いコピーとして定義される（`src/internal/codegen/js/runtime.cpp:164-175`）。
クローン適用の中心は `emitOperandWithClone`（`src/internal/codegen/js/emit_expressions.cpp:516-551`）で、適用条件は「オペランドが`Copy`（`Move`は対象外）」「ローカル型が`TypeKind::Struct`」「`structIsForeignObject`がfalse」「impl selfソースでない」を全て満たす場合のみである。

この `emitOperandWithClone` を呼ぶのは `emitRvalue` の `Use` ケースだけである（`src/internal/codegen/js/emit_expressions.cpp:47-53`）。

```cpp
// emit_expressions.cpp:47-53 — clone が効くのは Use Rvalue のみ
case mir::MirRvalue::Use: {
    const auto& data = std::get<mir::MirRvalue::UseData>(rvalue.data);
    return emitOperandWithClone(*data.operand, func);  // ← ここだけ
}
```

`Use` Rvalueを通るのは代入・let束縛（`src/internal/codegen/js/emit_statements.cpp:139`, `:145`）である。
一方、関数呼び出しの実引数生成ループは2箇所（buffered/非buffered重複実装）あり、どちらも素の `emitOperand` を使い構造体cloneの分岐を持たない（`src/internal/codegen/js/emit_statements.cpp:277`, `:500`）。

```cpp
// emit_statements.cpp:277 — 実引数は素の emitOperand（clone なし）
std::string argStr = emitOperand(*arg, func);   // Interfaceキャスト・char変換のみ
```

さらに `return` も clone を適用せず戻り値の参照を共有する（`emit_statements.cpp:171-187`）。
結果、構造体を値渡しで関数へ渡してもJSでは参照が渡り、呼び出し先での変更が呼び出し元に漏れる（値セマンティクス違反）。
LLVM系バックエンドは値渡しで隔離されるため、バックエンド間で意味論が分裂する。

補足: `structIsForeignObject`（関数型フィールドを持つ構造体を外部JSオブジェクト扱い）はclone対象外（`src/internal/codegen/js/utilities.cpp:117-129`）、impl selfソース（`impl_self_sources_`）もselfの変更伝搬のためclone対象外（`src/internal/codegen/js/emit_function.cpp:138-156`）である。引数cloneを追加する際はこれらの除外条件を踏襲する。

### H5: long/ulongがNumber表現で53bit精度に制限される

long/ulongは全てJSの `Number`（64bit浮動小数点）で表現される。
数値リテラルは `std::to_string` で出力され `n` サフィックスが付かない（`src/internal/codegen/js/emit_expressions.cpp:682-683`）。
算術（Add/Sub/Mul）は64bit判定 `wide64` でも `switch` の default を素通りし、最終的に素のNumber算術になる（`src/internal/codegen/js/emit_expressions.cpp:190`）。ラップアラウンドもオーバーフロー補正もない。
BigIntを使うのはビット演算（Shr/Shl/BitAnd/BitOr/BitXor）だけで、しかも `Number(...)` で包んで戻すためlong値そのものはBigIntで保持されない（`src/internal/codegen/js/emit_expressions.cpp:139-157`）。
出力も `console.log` / `String()` でNumberを文字列化する（`src/internal/codegen/js/builtins.cpp:219-226`, `:261-278`）。
TSの型注釈もlong/ulongを `number` に写像し `bigint` にはしない（`src/internal/codegen/js/utilities.cpp:18-88`、特に:30/:34/:42）。

結果、2^53を超える64bit ID・タイムスタンプは黙って精度を失う。

## 設計方針

### H3: 呼び出し実引数にも構造体cloneを適用

関数呼び出しの実引数生成（`emit_statements.cpp:277`, `:500`）で、既存の `emitOperandWithClone` と同じ判定（`Copy` かつ `Struct` かつ 非foreign かつ 非impl-self）を適用し、構造体引数を値渡しでクローンする。
2箇所の重複ループを共通ヘルパへ寄せて判定の同期漏れを防ぐ（監査テーマ「手書きテーブルの重複」に整合）。
`return` にcloneを追加するかは別途検討する（LLVM系のreturn値隔離と揃えるべきだが、既存の参照返し前提コードへの影響が大きいため、まずは引数のみでバックエンド分裂を解消する）。

### H5: long/ulongをBigIntで表現

long/ulong の実行時表現を `Number` から `BigInt` へ移行し、2^53超の精度を保持する。
波及範囲が広い（リテラル・演算子・出力・型注釈・FFI境界）ため段階分割する。

1. 値表現とリテラル
   - long/ulongリテラルを `123n` 形式のBigIntリテラルで出力（`emit_expressions.cpp:682-683`）。
   - 変数の初期化・代入がBigInt値を保持するようにする。

2. 演算子
   - Add/Sub/Mul/Div/Mod/ビット演算をBigIntで実行し、`Number(...)`巻き戻しを撤廃（`emit_expressions.cpp:139-190`）。
   - ulongは64bitラップ（`BigInt.asUintN(64, ...)`）、longは `BigInt.asIntN(64, ...)` でオーバーフロー挙動をLLVM系に揃える。
   - int32等の32bit整数との混在演算で型不一致（`BigInt`と`Number`の混合はTypeError）を避けるため、境界で明示変換を挿入する。

3. 出力
   - `cm_println_long`/`cm_long_to_string`/`cm_format_long` 等を BigInt→文字列（`String(x)` はBigIntでも正しく最短表現）に整合させる（`builtins.cpp:219-226`, `:261-278`）。

4. 型注釈（TS）
   - `tsType` でlong/ulong/isize/usizeを `bigint` へ写像（`utilities.cpp:30-42`）。structのinterface宣言・引数・戻り値注釈へ波及。

5. FFI境界
   - extern/npm FFIでlongをやり取りする境界で、JS側APIがNumberを期待する場合の変換（`Number(x)` / `BigInt(x)`）を明示する。現状FFIにlong専用変換は無い（`emit_statements.cpp:257` の `mapExternJsName` は名前解決のみ）ため、境界変換を新設する。

## 構文例・出力例

H3:

```cm
struct Point { int x; int y; }
fn mutate(Point p) { p.x = 999; }   // 値渡し: 呼び出し元に影響しないべき

fn main() {
    Point pt = Point{x: 1, y: 2};
    mutate(pt);
    println("{pt.x}");   // 期待: 1（LLVM系は1、現状のjs/tsは999）
}
```

現状の生成（引数cloneなし、参照が漏れる）:

```js
mutate(pt);            // pt をそのまま渡す → mutate 内の p.x=999 が pt に反映
```

設計適用後:

```js
mutate(__cm_clone(pt));   // 構造体引数をクローンして値渡し
```

H5:

```cm
fn main() {
    long id = 9007199254740993;   // 2^53 + 1
    println("{id}");              // 期待: 9007199254740993
}
```

現状（Number）: `9007199254740992`（末尾桁が精度喪失）。
設計適用後（BigInt）:

```js
const id = 9007199254740993n;
console.log(String(id));   // 9007199254740993
```

## 実装の段階分割

1. H3: 呼び出し実引数への構造体clone適用（`emit_statements.cpp:277`/`:500` を共通ヘルパ化し `emitOperandWithClone` 判定を適用）。除外条件（foreign/impl-self）踏襲。
2. H5-1: long/ulongリテラルのBigInt化（`emit_expressions.cpp:682-683`）と変数保持。
3. H5-2: 演算子のBigInt化とラップ挙動整合（`emit_expressions.cpp:139-190`）、int32境界の明示変換。
4. H5-3: 出力系のBigInt整合（`builtins.cpp:219-278`）。
5. H5-4: TS型注釈の `bigint` 化（`utilities.cpp:30-42`）。
6. H5-5: FFI境界のlong変換新設。

## テスト計画（tests/common/配下）

- H3: `tests/common/functions/` または `tests/common/impl/` に「構造体を値渡しし呼び出し先で変更→呼び出し元不変」を全バックエンド一致で追加。impl selfメソッド（変更が伝搬すべきケース）が非退行であることも確認。
- H5: `tests/common/casting/` または `tests/common/formatting/` に2^53超のlong/ulong（ID・タイムスタンプ・ビット演算・加減乗除）の正確性テストを追加し、jit/native/wasm/js/ts一致を期待。
- 回帰: 既存のlet束縛/return構造体clone、既存のint32演算、既存のTS型注釈出力が非退行。
- ネガティブ: BigIntとNumber混在によるTypeErrorが生成コードで発生しないこと（境界変換の網羅）。

## リスクと非互換性

- BigInt化は最大の波及点である。int32との混在演算でJSは `BigInt + Number` を実行時TypeErrorにするため、境界変換を漏れなく挿入しないと新たな実行時エラーを生む。
- 既存のJS/TS生成物（long=Number前提の外部連携コード）とのFFI互換が変わる。npm FFI境界での変換方針を明示する必要がある。
- H3の引数cloneはimpl selfメソッド（`impl_self_sources_`）や関数型フィールド構造体（`structIsForeignObject`）を誤ってクローンするとselfの変更伝搬・外部オブジェクト参照を壊す。既存除外条件を厳密に踏襲する。
- 言語構文の変更はなく後方互換。実行時意味論（値セマンティクス・64bit精度）がLLVM系に近づく方向の変更である。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（H3、H5、推奨対応ロードマップ第3段2）
- 関連メモ: `cm-js-ts-web-dev.md`（FFIオブジェクト参照コピー・透過import）— FFI境界のlong変換と整合
- 主要ファイル: `src/internal/codegen/js/emit_expressions.cpp`, `src/internal/codegen/js/emit_statements.cpp`, `src/internal/codegen/js/builtins.cpp`, `src/internal/codegen/js/utilities.cpp`, `src/internal/codegen/js/runtime.cpp`
