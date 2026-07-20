---
title: 集約へのインターフェイス値格納（fat pointer構築の伝播）
parent: v0.17.0 Design
---

# 集約へのインターフェイス値格納（fat pointer構築の伝播）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H1 | 型システム | インターフェイス値を配列・スライスに入れると`__error__len`/`__error__area`への解決でリンク失敗（全バックエンド、ポリモーフィックコレクション不可） | 未着手 |
| H2 | 型システム | インターフェイス型の構造体フィールドへの代入でfat pointerが構築されずnativeセグフォ・wasmトラップ・jsでvtable undefined | 未着手 |

## 背景と根本原因

インターフェイス値は fat pointer（データポインタ+vtableポインタ）として表現される。
fat pointer構築（具象型→インターフェイス型のcoercion）は「射影なしのローカルへの代入」経路にしか存在せず、集約（配列・スライス要素・構造体フィールド）へ格納する経路では構築がスキップされる。
その結果、H2では生ポインタがそのまま格納されて後段の動的ディスパッチが不正なvtableを読み、H1では配列要素の型解決が失敗して`__error__*`という未定義シンボルへ解決される。

### fat pointerの表現

インターフェイス値のLLVM型は `{i8* data, i8** vtable}` の匿名structである（`src/internal/codegen/llvm/core/interface.cpp:13-29` `getInterfaceFatPtrType`）。
具象型からfat値を構築するのは `createInterfaceFatPtr(dataPtr, concreteTypeName, interfaceName)`（`interface.cpp:33-52`）で、`{0}=data`、`{1}=vtable` を `InsertValue` で詰める（vtableは `vtableGlobals[concrete + "_" + interface]`、未発見なら null）。
Struct型のcodegenは `isInterfaceType(type->name)` ならfat pointer型を返す（`src/internal/codegen/llvm/core/types.cpp:85-95`）。
JSでは fat object `{data, vtable}` として表現される（`src/internal/codegen/js/types.hpp:132-133`）。
vtableは `impl X for Iface` から生成され、各エントリの実装関数名は `type_name + "__" + method.name` である（`src/internal/mir/lowering/lowering.cpp:400-437`、特に:430）。
動的ディスパッチはreceiver（fat値）から `ExtractValue 0=data / 1=vtable` を取り出し、vtableをGEP→ロード→呼び出す（`src/internal/codegen/llvm/core/terminator/dispatch.cpp:15-118`、特に:47-50）。

### fat pointer構築が「射影なし代入」限定である（H2の根本）

LLVMのinterface coercionは、代入先placeが射影を持たない場合のみ実行される（`src/internal/codegen/llvm/core/statement/assign.cpp:29`）。

```cpp
// assign.cpp:29 — この前提ガードが全 coercion の入口
if (assign.place.projections.empty() && ...) {
    // Case A: interface値へ具象structを代入 → createInterfaceFatPtr(...)（:49）
    // Case B: interfaceポインタへ具象アドレスを代入（:65-120）
}
```

`b.sh = x`（射影 `.sh` あり）や `arr[i] = x`（射影 `[i]` あり）はこの分岐に入らず、fat pointer構築がスキップされて右辺の生ポインタ/生値がそのままstoreされる。
後段の動的ディスパッチが `ExtractValue 1`（vtable）で不正値を読むため native SIGSEGV / wasm trap になる。
JSも同一のガードで同一の欠陥を持ち、射影付きplaceは `{data, vtable}` 構築を通らず生値が入るため `receiver.vtable.method` が undefined になる（`src/internal/codegen/js/emit_statements.cpp:77-104`）。

再現（検証済み）: `Box{sh: a}`（リテラル初期化）や `b.sh = a2`（interface値の代入）は正常に動くが、`b.sh = sq2`（具象Sqを射影付きplaceへ代入）で native/jit ともに異常終了する。
壊れるのは「射影付きplaceへの具象構造体代入」に限られる。

### 配列要素の型がerror化して`__error__*`になる（H1の根本）

配列リテラルの型推論は先頭要素の型のみを配列要素型に採用し、要素をインターフェイス型へcoercionしない（`src/internal/types/checking/expr/primary.cpp:316-328`）。
let宣言は宣言型と初期化型が非互換でも `error(...)` を出すのみで継続するため（`src/internal/types/checking/stmt.cpp:182-214`）、配列要素へのアクセスがerror型になり得る。

`__error__len` / `__error__area` はソースに存在するリテラルではなく、次の3段で合成される。

1. 受信側がerror型になると文字列表現が `"<error>"` になる（`src/internal/syntax/ast/types.cpp:174-175` `case TypeKind::Error: return "<error>";`）。
2. メソッド名マングリングが `"<error>"` を `Base<Arg>` と誤認し、`base_name=""`, `type_args=["error"]` から `"" + "__error" = "__error"` を生成する（`src/internal/hir/lowering/expr_member.cpp:912-960`）。
3. 最終呼び出し名が `method_type_name + "__" + member` で連結され `"__error" + "__" + "area" = "__error__area"` になる（`src/internal/hir/lowering/expr_member.cpp:1032`）。

どのバックエンドもこの解決失敗を診断せず未定義シンボルとして発行するため、全バックエンドでリンク失敗する（監査テーマ「黙って壊れる」の典型）。
なお文字列補間 `println("{arr[0].area()}")` の場合は受信が`<error>`型になり `__error__area` へ、補間外の直接 `arr[0].area()` の場合は名前は正しく `Shape__area` になるが（fat pointer未格納で）SIGSEGVになる、という二形態がある。

### 正しく動く既存経路（設計の参考）

- 射影なし代入 `Shape a = sq;` はfat構築される（`assign.cpp:34-62`、JS `emit_statements.cpp:77-106`）。
- 関数引数への具象→interface coercionは動く（`src/internal/codegen/llvm/core/terminator/invoke.cpp:161-204`）。
- 関数戻り値のinterface型はシグネチャでfat pointer型へ変換される（`src/internal/codegen/llvm/core/translate/signature.cpp:262`）。
- cast式経由のcoercionも動く（JS `emit_expressions.cpp:430-436`）。

## 設計方針

fat pointer構築を「射影なし代入」から解放し、代入先placeがインターフェイス型で右辺が具象型のときは射影の有無に関わらずcoercionを行う。
配列・スライス・構造体フィールドという集約の3経路すべてでfat値を構築・伝播させる。
併せて、集約要素型としてインターフェイス型を採用する型検査（配列リテラル・宣言型優先）を整え、error型への退行と`__error__*`黙殺を診断付きエラーに置き換える。

1. coercionを射影付きplaceへ拡張
   - `assign.cpp:29` のガードを、代入先placeの静的型がインターフェイス型かどうかで判定するよう変更する（射影の有無ではなく、格納先の型で決める）。
   - 射影付きplace（構造体フィールド・配列/スライス要素）へfat値を`store`する経路を追加する。JSも同様（`emit_statements.cpp:77`）。

2. 集約構築時のcoercion
   - 配列リテラル・スライスpushで、要素型がインターフェイス型のとき各要素をfat値へcoercionしてから格納する。
   - 配列要素型の推論を「宣言型が優先されればそれをインターフェイス要素型として採用」する方向へ整える（`primary.cpp:316-328` / `stmt.cpp:182-214`）。

3. 集約要素の読み出しでのfat維持
   - `arr[i].method()` のreceiver解決で、要素がfat値であることを保ち、動的ディスパッチ（`dispatch.cpp:15-118`）へ正しく渡す。

4. 黙殺の排除
   - 型解決失敗で`<error>`型が生じた場合、`__error__*`名を発行する前に診断付きハードエラーにする（`expr_member.cpp:1032` 到達前に検出）。
   - fat pointer未構築のままディスパッチへ渡る経路を、コンパイル時に検出可能な不変条件として扱う。

## 構文例・出力例

```cm
interface Shape {
    fn area() -> int;
}
struct Sq { int side; }
impl Sq for Shape {
    fn area() -> int { return self.side * self.side; }
}
struct Box { Shape sh; }

fn main() {
    Sq sq = Sq{side: 4};
    // H1: インターフェイス値の配列
    Shape[] shapes = [sq];
    println("{shapes[0].area()}");   // 期待: 16
    // H2: 構造体フィールド（射影付きplace）への具象代入
    Box b = Box{sh: sq};
    Sq sq2 = Sq{side: 5};
    b.sh = sq2;                       // 現状: native SIGSEGV / wasm trap / js vtable undefined
    println("{b.sh.area()}");        // 期待: 25
}
```

現状:

- H1配列 + 補間: 全バックエンドで `__error__area` へのリンク失敗。
- H1配列 + 補間外: 名前は `Shape__area` だが fat未格納で SIGSEGV。
- H2フィールド代入: native/jit異常終了、wasm trap、js vtable undefined。

## 実装の段階分割

1. H2先行: `assign.cpp:29` のガードを格納先型ベースへ変更し、構造体フィールド（射影付きplace）へのfat構築を通す。JS側 `emit_statements.cpp:77` 同期。
2. H1配列・スライス: 配列リテラル/スライスpushでの要素coercion、配列要素型推論の整備（`primary.cpp`/`stmt.cpp`）。
3. receiver読み出しのfat維持と動的ディスパッチ整合（`dispatch.cpp`）。
4. 黙殺排除: `<error>`型からのメソッド名合成（`expr_member.cpp:912-1032`）を診断付きエラー化。
5. wasm/svを含む全バックエンド一致確認（svは動的ディスパッチ非対応のため明示エラー継続: `src/internal/codegen/sv/self_param.cpp:62`）。

## テスト計画（tests/common/配下）

- `tests/common/interface/` にポリモーフィックコレクションのケースを追加（`Shape[]`・スライス・混在具象型、補間あり/なしの両形態）。
- `tests/common/interface/` に構造体フィールドへの具象代入・interface値代入・リテラル初期化の3系統を追加（H2）。
- 全バックエンド一致（jit/native/wasm/js/ts）を期待、svは合成不能の明示エラーを期待。
- 回帰: 既存の射影なし代入・関数引数・戻り値・castのinterface経路が非退行。
- ネガティブ: 型不整合な集約構築で診断付きエラー（`__error__*`を発行しない）。

## リスクと非互換性

- `assign.cpp:29` のガード変更は全代入経路に影響するため、非interface型の射影付き代入（通常の構造体フィールド代入）を退行させないよう、interface型判定を厳密に行う。
- MIRコピー伝播が `*Shape`/`*Sq` を同一視してcoercionを消す既知の退行がある（`src/internal/mir/passes/scalar/propagation.cpp:82` にコメント）。集約経路のfat構築が伝播で消えないことを確認する。
- 配列要素型をインターフェイス型として扱うと、要素のstrideがfat pointer幅（ポインタ2個）になる。スライスのelem_size計算（監査C4関連）と整合させる必要がある。
- 言語構文の変更はなく後方互換。既存の「偶然動く」`Box{sh: a}`初期化は維持する。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（H1、H2、テーマ5「集約を値として動かす経路の未成熟」）
- 関連所見: C4（スライスのelem_size/stride）— fatポインタ幅の要素stride、C16（`Struct__method`マングリング）— error型からの名前合成と同根
- 主要ファイル: `src/internal/codegen/llvm/core/interface.cpp`, `src/internal/codegen/llvm/core/statement/assign.cpp`, `src/internal/codegen/llvm/core/terminator/dispatch.cpp`, `src/internal/hir/lowering/expr_member.cpp`, `src/internal/types/checking/expr/primary.cpp`, `src/internal/codegen/js/emit_statements.cpp`
