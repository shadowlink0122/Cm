# インターフェイスの動的ディスパッチ

インターフェイス値は、実装オブジェクトへのデータポインタとvtableポインタを組にしたfat pointer `{i8* data, i8** vtable}`として表現される。vtableは`impl Type for Iface`ごとに「インターフェイス宣言のメソッド順に実装関数`Type__method`のポインタを並べた定数配列」としてLLVMグローバルに生成され、呼び出し側はfat値からvtableを取り出してメソッドインデックス位置の関数ポインタをロード・間接呼び出しする。この機構の正しさは「具象型の値がインターフェイス型のスロットへ入る全経路でfat pointer構築（coercion）が行われる」ことに懸かっており、1経路でも生値のまま格納されると、後段のディスパッチが不正なvtableを読んでnative/jitのセグフォになる。

## 概要

動的ディスパッチが選ばれる条件はMIRのCall終端命令の`is_virtual`フラグである。MIR loweringは呼び出し名を最初の`__`で分割し、前半がインターフェイス名集合に含まれる場合に`interface_name`/`method_name`を設定して`is_virtual = true`にする（`src/internal/mir/lowering/expr_call.cpp:317-352`、フラグ定義は`src/internal/mir/nodes.hpp:358`）。LLVM側の入口`generateMethodCallDispatch`（`src/internal/codegen/llvm/core/terminator/dispatch.cpp:13`）は、レシーバのローカル型がインターフェイス型なら動的ディスパッチ、具象構造体なら`Type__method`の直接呼び出し、プリミティブなら`int__abs`等のimpl呼び出しへ分岐する。

## データ構造とアルゴリズム

### fat pointer表現

インターフェイス型のLLVM型は`{ptr data, ptr vtable}`の名前付きstruct（`Iface_fat_ptr`）で、`getInterfaceFatPtrType`が生成・キャッシュする（`src/internal/codegen/llvm/core/interface.cpp:13-29`、キャッシュは`mir_to_llvm.hpp:68`の`interfaceTypes`）。型変換の全経路でこの表現が徹底される。インターフェイス名を持つStruct型はfat pointer型になり（`src/internal/codegen/llvm/core/types.cpp:87-95`）、インターフェイスへのポインタ型`Shape*`も隠し領域を介さずfat pointer値そのものとして表現される（`types.cpp:62-67`）。関数シグネチャではインターフェイス型引数をfat pointer構造体の値渡しにする（`src/internal/codegen/llvm/core/translate/signature.cpp:262-266`）。MIR側のレイアウト計算でもインターフェイス値はポインタ2個分の幅を持つ（`src/internal/mir/lowering/context.cpp:507-510`）。この幅が配列・スライスの要素strideの前提になる。

### vtable生成

MIR loweringの`generate_vtables`（`src/internal/mir/lowering/lowering.cpp:407-443`）が、`register_impl`で記録した`impl_info`（型名→インターフェイス名→実装メソッド名）から`VTable`ノードを作る。エントリはインターフェイス宣言のメソッド順で、実装関数名は`type_name + "__" + method.name`である（`lowering.cpp:436`）。LLVM側の`generateVTables`（`interface.cpp:55-103`）がこれを関数ポインタの定数配列`Type_Iface_vtable`としてグローバル化し、`vtableGlobals[type_name + "_" + interface_name]`へ登録する（`interface.cpp:101`）。呼び出しは`translate/program.cpp:330`で関数本体生成前に行われる。

### fat pointerの構築（coercion）

具象型→インターフェイス型の変換は`createInterfaceFatPtr`に一元化されている。

```cpp
// interface.cpp:33-52
llvm::Value* MIRToLLVM::createInterfaceFatPtr(llvm::Value* dataPtr,
                                              const std::string& concreteTypeName,
                                              const std::string& interfaceName) {
    auto fatPtrType = getInterfaceFatPtrType(interfaceName);
    llvm::Value* vtablePtr = nullptr;
    auto it = vtableGlobals.find(concreteTypeName + "_" + interfaceName);
    ...
    fat = builder->CreateInsertValue(fat, dataCast, {0}, "iface_fat");
    fat = builder->CreateInsertValue(fat, vtableCast, {1}, "iface_fat");
    return fat;
}
```

構築が必要な経路は代入・引数渡し・集約格納の全てに及ぶ。代入文の処理（`src/internal/codegen/llvm/core/statement/assign.cpp`）には3つのケースがある。射影なしのインターフェイス値への具象構造体代入（Case A、`assign.cpp:33-62`）、インターフェイスポインタへの具象アドレス代入（Case B、`assign.cpp:64-121`）、そして射影付きplace（構造体フィールド・配列/スライス要素）のインターフェイス型スロットへの代入（Case A2、`assign.cpp:149-213`）である。Case A2は射影列を辿って格納先の静的型を解決し（Fieldは構造体定義、Index/Derefは要素型）、格納先がインターフェイス型なら具象値の実体アドレスからfat値を構築して`store`する。関数引数への具象→インターフェイスcoercionは呼び出し生成側が行い、期待パラメータ型から対象インターフェイスを特定してfat値を構築する（`src/internal/codegen/llvm/core/terminator/invoke.cpp:367-404`）。スライスリテラルやpushでは、MIR loweringが要素をいったんインターフェイス型の一時へ代入してfat pointerを構築してからblob格納する（`src/internal/mir/lowering/expr_slice.cpp:105-121`）。

### ディスパッチのコード生成

動的ディスパッチ（`dispatch.cpp:15-116`）はfat値から`ExtractValue 0`でdataポインタ、`ExtractValue 1`でvtableポインタを取り出す（`dispatch.cpp:47-49`、fat値がポインタで渡る経路はGEP+ロード）。メソッドインデックスはMIRのインターフェイス宣言をメソッド名で検索して求め（`dispatch.cpp:53-68`）、`methodIndex * ポインタサイズ`のバイトオフセットで関数ポインタをロードする（`dispatch.cpp:70-78`）。関数型はインターフェイス宣言のシグネチャ（戻り値型と引数型）から構成し、第1引数にdataポインタを渡して間接呼び出しする（`dispatch.cpp:80-100`）。

### 動作例

```cm
interface Shape {
    fn area() -> int;
}
struct Sq { int side; }
impl Sq for Shape {
    fn area() -> int { return self.side * self.side; }
}
struct Holder { Shape sh; }

int main() {
    Sq sq = Sq{side: 4};
    Shape[] shapes = [sq];            // スライス要素へのfat構築（一時経由）
    println("{shapes[0].area()}");    // 16: vtable経由の間接呼び出し
    Holder h = Holder{sh: sq};
    Sq sq2 = Sq{side: 5};
    h.sh = sq2;                       // 射影付きplaceへのfat構築（Case A2）
    println("{h.sh.area()}");         // 25
    return 0;
}
```

このプログラムでは、`Sq_Shape_vtable = [Sq__area]`という1エントリの定数配列が生成され、`shapes[0]`・`h.sh`のスロットには`{&sq実体, &Sq_Shape_vtable}`のfat値が格納される。`area()`呼び出しはfat値の`vtable[0]`をロードし、dataポインタを第1引数として間接呼び出しする。上記のどの格納経路でもfat構築が欠けると、格納されるのは生の`Sq`値であり、`vtable`スロットに`side`の値等が重なって間接呼び出しが不正アドレスへ飛ぶ。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/interface.cpp` | fat pointer型・`createInterfaceFatPtr`・vtableグローバル生成 |
| `src/internal/codegen/llvm/core/terminator/dispatch.cpp` | 動的/静的/プリミティブのディスパッチ分岐と間接呼び出し生成 |
| `src/internal/codegen/llvm/core/statement/assign.cpp` | 代入経路のcoercion（射影なし・ポインタ・射影付き集約） |
| `src/internal/codegen/llvm/core/terminator/invoke.cpp` | 関数引数への具象→インターフェイスcoercion |
| `src/internal/codegen/llvm/core/types.cpp` | インターフェイス型・インターフェイスポインタ型のfat表現 |
| `src/internal/codegen/llvm/core/translate/signature.cpp` | インターフェイス引数のfat pointer値渡しシグネチャ |
| `src/internal/mir/lowering/lowering.cpp` | MIRレベルのvtableノード生成（`generate_vtables`） |
| `src/internal/mir/lowering/expr_call.cpp` | `is_virtual`判定（呼び出し名の`__`分割とインターフェイス名照合） |
| `src/internal/mir/lowering/expr_slice.cpp` | スライス要素格納時のインターフェイス一時経由fat構築 |
| `src/internal/mir/lowering/context.cpp` | インターフェイス値のレイアウト幅（ポインタ2個分） |

## 落とし穴とケア

- 防ぐバグのクラス（セグフォ）: fat pointer未構築のままインターフェイススロットへ生値が入ると、ディスパッチの`ExtractValue 1`がゴミをvtableとして読み、native/jitはSIGSEGV、wasmはtrapになる。coercionの要否は「代入先の静的型がインターフェイス型か」で判定するのが不変条件であり、「射影が無いか」で判定してはならない（射影付きの`b.sh = sq`や`arr[i] = sq`が素通りする）。集約への新しい格納経路（新コンテナ・新リテラル形式等）を追加する際は、要素型がインターフェイスの場合のfat構築を必ず通すこと。
- 防ぐバグのクラス（リンク失敗）: 型解決の失敗でレシーバが`<error>`型になると、メソッド名合成が`__error__len`/`__error__area`のような未定義シンボルを生成し、診断なしのリンク失敗になる。添字結果の型が導出できない経路には要素型フォールバックがあり（`src/internal/hir/lowering/expr.cpp:976-984`）、これを外すと文字列補間内の`arr[0].method()`が再びこの形で壊れる。
- 防ぐバグのクラス（stride破壊）: インターフェイス値の配列・スライスの要素strideはポインタ2個分である。要素サイズ計算がfat幅（`context.cpp:507-510`）を見ずに具象構造体サイズやポインタ1個分で計算すると、隣接要素の読み書きが互いのvtableを破壊する。
- 静かな劣化への注意: `createInterfaceFatPtr`はvtableグローバルが見つからない場合にnullを埋める（`interface.cpp:42-43`）。implが欠けた型のcoercionはコンパイルを通過し、最初のメソッド呼び出しでnull経由のクラッシュになる。vtableキーは`concrete + "_" + interface`の文字列連結であり、生成側（`interface.cpp:101`）と参照側（`interface.cpp:39`・`invoke.cpp:390`）の書式を揃え続けること。
- 維持すべき不変条件: vtableのエントリ順はインターフェイス宣言のメソッド順であり、生成側（`lowering.cpp:433-438`）とインデックス解決側（`dispatch.cpp:53-68`）が同じ順序を参照する。宣言順に依存しないソート等を片側に入れると全メソッドが入れ違いに呼ばれる。
- 回帰テスト: `tests/common/interface/interface_in_aggregates.cm`（配列・スライス・構造体フィールドへの格納と呼び出し）、`tests/common/interface/basic.cm`・`basic_impl.cm`・`as_param.cm`（値・引数渡しのcoercion）、`tests/common/interface/multiple_methods.cm`（メソッドインデックスの整合）。

## 関連資料

- [集約へのインターフェイス値格納（fat pointer構築の伝播）](../../archive/v0.17.0/interfaces-derive/interface-values-in-aggregates.md) — 集約経路のcoercion設計と壊れ方の詳細な分析
- [static-dispatch.md](static-dispatch.md) — 具象型が確定する経路の直接呼び出しへの解決
- [../generics/mangling.md](../generics/mangling.md) — vtableエントリ名と`is_virtual`判定が依存するマングル規約
