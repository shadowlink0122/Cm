# インターフェイスの静的ディスパッチ

Cmのインターフェイスは、レシーバの具象型がコンパイル時に確定する経路ではvtableを介さず直接呼び出しへ解決される。ジェネリック境界（`<T: Ord>`等）経由の呼び出しは、型検査で制約充足を検証したうえで、単相化が呼び出し名内の型パラメータを具象型名へ書き換えることでモノモーフィックな`Type__method`直接呼び出しになる。演算子オーバーロード（`==`や`<`）はMIR loweringが`Type__op_eq`/`Type__op_lt`への関数呼び出しへ正規化し、`with Eq, Ord, Clone, Hash`等のderiveは自動実装生成器が非ジェネリック構造体には登録時に、ジェネリック構造体には単相化後の特殊化ごとにメソッド本体を合成する。

## 概要

ディスパッチの分岐点はMIRのCall終端命令が持つ`is_virtual`フラグである（`src/internal/mir/nodes.hpp:358`）。呼び出し名`TypeName__method`の`TypeName`がインターフェイス名集合に含まれる場合のみ動的ディスパッチ（[dynamic-dispatch.md](dynamic-dispatch.md)）となり（`src/internal/mir/lowering/expr_call.cpp:317-352`）、それ以外はすべて静的な直接呼び出しである。LLVM側のディスパッチ入口でも、レシーバのローカル型が具象構造体であれば`actualTypeName + "__" + method_name`の関数を直接呼ぶ（`src/internal/codegen/llvm/core/terminator/dispatch.cpp:118-123`）。

## データ構造とアルゴリズム

### ジェネリック境界の検証と単相化による解決

境界は関数登録時に`generic_function_constraints_`へ保存される（`src/internal/types/checking/decl.cpp:264`）。呼び出しの型推論後、`infer_generic_call`が各型パラメータの推論結果を制約と突き合わせる（`src/internal/types/checking/generic.cpp:134-157`）。判定本体は`check_type_constraints`（`src/internal/types/checking/utils/compat.cpp:510-518`）で、全制約について`type_implements_interface`（`compat.cpp:459-508`）を評価する。この関数はプリミティブ型の組み込み実装（数値・文字は`Ord`、それに`bool`・`string`を加えた`Eq`/`Clone`）をハードコードで認め、ユーザー型は明示的な`impl`の登録（`impl_interfaces_`）と`with`による自動実装（`has_auto_impl`）を順に確認する。

制約を満たした呼び出しの実体解決は単相化が行う。ジェネリック本体内の`t.method()`はHIR loweringでレシーバの静的型名からメソッド名を合成するため、型パラメータ`T`のレシーバは`T__method`という呼び出し名になる（`src/internal/hir/lowering/expr_member.cpp:1071`の`mangle::method_name`）。関数特殊化の生成時、`clone_terminator_with_subst`が呼び出し名内の型パラメータを具象型名へ書き換える。

```cpp
// src/internal/mir/lowering/monomorphization_utils.cpp:156-164
// パターン1: 先頭の "TypeParam__method" -> "ConcreteType__method"
std::string prefix = type_param + "__";
if (func_name.find(prefix) == 0) {
    func_name = concrete_type + func_name.substr(type_param.length());
    ...
}
// パターン2: 途中の "__TypeParam__" -> "__ConcreteType__"
```

この結果、`<T: Ord> T max_of(T a, T b)`を`int`で呼ぶと特殊化`max_of__int`の本体は`int`のネイティブ比較（あるいは構造体なら`Sq__op_lt`直接呼び出し）へモノモーフィックに解決され、vtableは一切関与しない。

### implの解決順序

implメソッドと自動実装の登録・解決はMIR loweringのパス順序（`src/internal/mir/lowering/lowering.cpp:19-68`）で決まる。

1. Pass 1: 構造体・インターフェイス等の宣言登録。implは`register_impl`（`lowering.cpp:446-468`）が`impl_info[type][interface] = Type__method`へ記録する。
2. Pass 1.5: 非ジェネリック構造体の`with`自動実装を生成する（`generate_auto_impls`）。
3. Pass 2-3: 自由関数とimplメソッド本体のlowering。メソッド定義名はHIR呼び出し側と同じ`mangle::method_name`規則で生成される（`src/internal/mir/lowering/impl.cpp:75`）。
4. Pass 4: 単相化。ジェネリックimplメソッド（`Vector<T>__push`）が特殊化名（`Vector__int__push`）へ展開される。
5. Pass 5: 単相化で生成された特殊化構造体に対する自動実装の生成（`generate_monomorphized_auto_impls`）。
6. Pass 6: 構造体比較演算子の関数呼び出しへの書き換え。

メソッド呼び出しの名前解決自体はオーバーロードや複数implの探索を持たず、レシーバ型名とメソッド名から一意なマングル名が決まる（同名縮退は型検査の衝突検出が弾く、[../generics/mangling.md](../generics/mangling.md)参照）。したがって「解決順序」の実体はこのパス順序であり、自動実装が参照する`impl_info`は必ず明示implの登録後に更新される。

### 演算子オーバーロードの正規化

構造体への`==`/`!=`は、MIR loweringの二項演算処理が`impl_info`に`Eq`実装（または`__op_eq`を含む実装関数名）が登録されているかを確認し、`Type__op_eq(lhs, rhs)`の通常関数呼び出しへ変換する（`src/internal/mir/lowering/expr/binary.cpp:509-572`）。`!=`は結果の論理否定で表現する。順序比較は`op_lt`1本へ正規化され、`a > b`は`op_lt(b, a)`、`a <= b`は`!op_lt(b, a)`、`a >= b`は`!op_lt(a, b)`として引数交換と否定で合成する（`binary.cpp:574-624`）。ユーザーが`impl`で書いた演算子は`lower_operator`が`HirOperatorKind`から`op_eq`/`op_lt`/`op_add`等のサフィックスへ写像して同じ名前空間の関数になる（`src/internal/mir/lowering/impl.cpp:17-75`）。いずれの呼び出しも`is_virtual = false`の直接呼び出しである（`binary.cpp:552`）。

### deriveの自動実装生成

`with Eq, Ord, Clone, Hash, Debug, Display, Css`は`AutoImplGenerator`（`src/internal/mir/lowering/auto_impl.hpp:18`）がメソッド本体をMIRとして合成する。非ジェネリック構造体は`generate_for_struct`（`src/internal/mir/lowering/auto_impl/generator.cpp:10-50`）がトレイト名ごとに生成器（`Eq`→`generate_builtin_eq_operator`等）を呼び、ジェネリック構造体は`register_generic_auto_impls`で`with`リストを保存するだけに留める（`generator.cpp:15-18`）。単相化後、`generate_monomorphized_auto_impls`（`generator.cpp:55-95`）が生成済みの全`MirStruct`を走査し、特殊化名の最初の`__`より前を基底名として保存済みリストを引き、特殊化ごとの実装（`Pair__int__string__op_eq`等）を合成する。生成された実装は`impl_info`へ登録されるため、後続のPass 6や式loweringの演算子正規化から明示implと同様に見える。各トレイトの本体合成はフィールドを順に比較・複製・ハッシュ結合するループ展開であり、`src/internal/mir/lowering/auto_impl_compare.cpp`（Eq/Ord）と`src/internal/mir/lowering/auto_impl/clone_hash.cpp`に実装がある。

### 解決の動作例

```cm
interface Greet {
    fn hello() -> int;
}
struct En { int n; }
impl En for Greet {
    fn hello() -> int { return self.n; }
}

<T: Greet> int call_hello(T t) {
    return t.hello();       // HIRでは T__hello という呼び出し名になる
}

int main() {
    En e = En{n: 7};
    println("{call_hello(e)}");   // 静的解決: En__hello の直接呼び出し
    return 0;
}
```

`call_hello(e)`の単相化で特殊化`call_hello__En`が生成され、本体の`T__hello`は文字列パターン書き換えで`En__hello`になる。レシーバの具象型が確定しているため`is_virtual`は立たず、vtableは生成されても参照されない。同じ`e`を`Greet`型の変数や配列要素へ入れた場合のみ動的ディスパッチに切り替わる。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/types/checking/generic.cpp` | インスタンス化時の制約充足検査 |
| `src/internal/types/checking/utils/compat.cpp` | `type_implements_interface` / `check_type_constraints` |
| `src/internal/hir/lowering/expr_member.cpp` | メソッド呼び出し名の合成（`T__method`を含む） |
| `src/internal/mir/lowering/monomorphization_utils.cpp` | 特殊化時の呼び出し名の型パラメータ書き換え |
| `src/internal/mir/lowering/expr/binary.cpp` | 演算子の`Type__op_xx`呼び出しへの正規化 |
| `src/internal/mir/lowering/impl.cpp` | implメソッド・演算子定義のlowering |
| `src/internal/mir/lowering/auto_impl/`・`auto_impl*.cpp` | deriveの自動実装生成（Eq/Ord/Clone/Hash/Debug/Display/Css） |
| `src/internal/mir/lowering/lowering.cpp` | パス順序と`impl_info`登録 |
| `src/internal/codegen/llvm/core/terminator/dispatch.cpp` | 静的ディスパッチ分岐のコード生成 |

## 落とし穴とケア

- 防ぐバグのクラス: 境界検査の欠落によるリンク失敗。制約検査を通らない型で単相化が走ると、`T__method`の書き換え先`Concrete__method`が存在せず未定義シンボルになる。制約検査（型検査）と実装存在（`impl_info`）の両方が揃って初めて静的解決が成立する。
- 防ぐバグのクラス: 呼び出し側と定義側のマングル不一致。`T__method`書き換えは文字列パターンに依存するため、メソッド名生成を`mangle::method_name`以外で行うと書き換えが外れて特殊化前の名前が残る。
- 維持すべき不変条件: 単相化（Pass 4）より前にジェネリック構造体の自動実装を生成しないこと。特殊化ごとにフィールド型が異なるため、基底構造体に対する合成はレイアウト不一致の比較・複製コードになる。逆に、単相化後の自動実装生成は特殊化名から基底名を最初の`__`で切り出すため、特殊化命名規則（[../generics/mangling.md](../generics/mangling.md)）との整合が前提である。
- 維持すべき不変条件: `op_lt`1本への正規化（引数交換と否定）を保つこと。`op_gt`等を別実装として導入すると、deriveされたOrdと明示implの間で比較の全順序性が壊れうる。
- 回帰テスト: `tests/common/generics/interface_bounds.cm`・`generic_with_interface.cm`・`generic_constraints.cm`（境界経由の呼び出し）、`tests/common/interface/operator_add.cm`・`operator_compare.cm`・`operator_ord.cm`（演算子オーバーロード）、`tests/common/interface/derive_basic.cm`・`derive_generic.cm`・`derive_mixed_with.cm`・`derive_ord_string.cm`（derive）。

## 関連資料

- [dynamic-dispatch.md](dynamic-dispatch.md) — レシーバがインターフェイス値である場合のvtable経由呼び出し
- [../generics/monomorphization.md](../generics/monomorphization.md) — 特殊化生成と呼び出し書き換えの本体
- [../generics/instantiation-diagnostics.md](../generics/instantiation-diagnostics.md) — 境界未宣言の演算子使用の前倒し検査
- [ジェネリックインスタンス化の診断](../../archive/v0.17.0/diagnostics/generic-instantiation-diagnostics.md) — 境界検査の設計文書
