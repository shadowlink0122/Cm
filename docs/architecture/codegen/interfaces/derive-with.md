# with / derive自動実装のコード生成

構造体宣言の`with Eq, Ord, Debug`や`#[derive(...)]`属性は、コンパイラがメソッド本体を自動合成する指示であり、LLVM IRでは手書きimplと同じ`型名__メソッド名`規則の通常関数として現れる。
`Eq`は`型名__op_eq`、`Ord`は`型名__op_lt`、`Debug`は`型名__debug`（ほかに`Display`は`型名__toString`、`Clone`/`Hash`も同様の接尾辞）を生成し、`==`や`<`などの演算子はこれらへの関数呼び出しに正規化される。
本体はフィールドを宣言順に逐次処理する形（Eqは全フィールド比較のAND、Ordは辞書式比較、Debugはフィールド名付きの再帰的文字列化）に展開される。
本文書のIRは、次のプログラムを`CM_DUMP_IR=1 ./cm compile -O3`（最適化前IR）および`CM_DUMP_IR=2`（O3最適化後IR）でダンプした抜粋である。

```cm
import std::io::println;

struct Inner with Debug {
    int v;
}

struct Point with Eq, Ord, Debug {
    int x;
    int y;
}

struct Wrap with Debug {
    Inner core;
    int tag;
}

int main() {
    Point a = Point{x: 1, y: 2};
    Point b = Point{x: 1, y: 3};
    if (a == b) { println("eq"); } else { println("ne"); }
    if (a < b) { println("lt"); }
    Wrap w = Wrap{core: Inner{v: 9}, tag: 4};
    println("{w.debug()}");
    return 0;
}
```

### `with Eq` — `Point__op_eq`（フィールド逐次比較）

`with Eq`は全フィールドの等値比較を論理ANDで結合する関数を合成し、`a == b`はこの関数への直接呼び出しへ、`a != b`は結果の否定へ正規化される。

```llvm
define i8 @Point__op_eq(%Point %arg0, %Point %arg1) local_unnamed_addr #0 {
entry:
  %arg0.fca.0.extract = extractvalue %Point %arg0, 0
  %arg0.fca.1.extract = extractvalue %Point %arg0, 1
  %arg1.fca.0.extract = extractvalue %Point %arg1, 0
  %arg1.fca.1.extract = extractvalue %Point %arg1, 1
  %eq = icmp eq i32 %arg0.fca.0.extract, %arg1.fca.0.extract
  %eq10 = icmp eq i32 %arg0.fca.1.extract, %arg1.fca.1.extract
  %logical_and = and i1 %eq, %eq10
  %and_ext = zext i1 %logical_and to i8
  ret i8 %and_ext
}
```

`x`と`y`の`icmp eq`を`and`でまとめるだけの純粋関数で、O3では`memory(none)`属性が付き、このプログラムの`a == b`のように引数が定数なら呼び出しごと畳み込まれて`main`には分岐すら残らない。
演算子から`op_eq`への正規化規則は[../../interface/static-dispatch.md](../../interface/static-dispatch.md)を参照。

### `with Ord` — `Point__op_lt`（辞書式比較）

`with Ord`は「先頭フィールドが小さいか、等しければ次のフィールドで比較」という辞書式順序の`op_lt`を合成し、`>`・`<=`・`>=`は引数交換と否定で`op_lt`1本に還元される。
最適化前IRでは各フィールドの比較が分岐で連なる形だが、O3では分岐が`select`へ畳み込まれて直線コードになる。

```llvm
define i8 @Point__op_lt(%Point %arg0, %Point %arg1) local_unnamed_addr #1 {
entry:
  %arg0.fca.0.extract = extractvalue %Point %arg0, 0
  %arg0.fca.1.extract = extractvalue %Point %arg0, 1
  %arg1.fca.0.extract = extractvalue %Point %arg1, 0
  %arg1.fca.1.extract = extractvalue %Point %arg1, 1
  %lt = icmp slt i32 %arg0.fca.0.extract, %arg1.fca.0.extract
  %gt = icmp sle i32 %arg0.fca.0.extract, %arg1.fca.0.extract
  %lt11 = icmp slt i32 %arg0.fca.1.extract, %arg1.fca.1.extract
  %or.cond = select i1 %gt, i1 %lt11, i1 false
  %narrow = select i1 %lt, i1 true, i1 %or.cond
  %common.ret.op = zext i1 %narrow to i8
  ret i8 %common.ret.op
}
```

`x`同士の比較が決着すればその結果、等しければ`y`同士の比較結果を返すという合成規則がそのまま`select`の入れ子に対応している。
比較演算子の正規化と自動実装生成の内部実装は[../../interface/static-dispatch.md](../../interface/static-dispatch.md)を参照。

### `with Debug` — `Wrap__debug`（再帰的文字列化）

`with Debug`は`型名 { フィールド名: 値, ... }`形式の文字列を組み立てる`debug`メソッドを合成し、構造体フィールドは各フィールド型の`debug`を呼ぶことで再帰的に文字列化される。

```llvm
define ptr @Wrap__debug(%Wrap %arg0) local_unnamed_addr #2 {
entry:
  %arg0.fca.0.0.extract = extractvalue %Wrap %arg0, 0, 0
  %arg0.fca.1.extract = extractvalue %Wrap %arg0, 1
  %0 = tail call ptr @cm_string_concat(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [8 x i8] }>, ptr @strh.3, i64 0, i32 4, i64 0), ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [7 x i8] }>, ptr @strh.4, i64 0, i32 4, i64 0))
  %1 = tail call ptr @cm_string_concat(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [9 x i8] }>, ptr @strh, i64 0, i32 4, i64 0), ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [4 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
  %2 = tail call ptr @cm_format_int(i32 %arg0.fca.0.0.extract)
  %3 = tail call ptr @cm_string_concat(ptr %1, ptr %2)
  ...
  %8 = tail call ptr @cm_format_int(i32 %arg0.fca.1.extract)
  %9 = tail call ptr @cm_string_concat(ptr %7, ptr %8)
  %10 = tail call ptr @cm_string_concat(ptr %9, ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [3 x i8] }>, ptr @strh.7, i64 0, i32 4, i64 0))
  ret ptr %10
}
```

本体は文字列定数（`"Wrap { "`・`"core: "`など）と`cm_format_int`の結果を`cm_string_concat`で順に連結する直線コードで、実行すると`Wrap { core: Inner { v: 9 }, tag: 4 }`が得られる。
ネストした`Inner`フィールドの処理は最適化前IRでは`Inner__debug`への呼び出しだが、O3では`Wrap__debug`の中へインライン展開されており（`%1`〜`%3`が`Inner { v: ... }`部分の組み立て）、`Inner__debug`自体も独立した関数として残っている。

### `#[derive(...)]`属性

`#[derive(Ord)]`のような属性記法は`with`と等価な指示で、両方を書いた場合はリストがマージされる。

```cm
#[derive(Ord)]
struct Item with Eq {
    int priority;
    int id;
}
```

このコードからは`with Eq, Ord`と同一の`Item__op_eq`と`Item__op_lt`が生成され、IR上で属性経由と`with`経由の区別は存在しない。

```llvm
define i8 @Item__op_eq(%Item %arg0, %Item %arg1) local_unnamed_addr #0 { ... }
define i8 @Item__op_lt(%Item %arg0, %Item %arg1) local_unnamed_addr #1 { ... }
```

ジェネリック構造体では自動実装は単相化後の特殊化ごとに合成される（`Pair__int__string__op_eq`のような特殊化名になる）。
生成タイミングとパス順序の内部実装は[../../interface/static-dispatch.md](../../interface/static-dispatch.md)を参照。

## 関連資料

- [impl.md](impl.md) — 手書き`impl`が生成するシンボル群との対応
- [dispatch.md](dispatch.md) — 自動実装メソッドが呼ばれる際のディスパッチIR
- [../../interface/static-dispatch.md](../../interface/static-dispatch.md) — 演算子正規化とderive自動実装生成の内部実装
- [../../generics/monomorphization.md](../../generics/monomorphization.md) — ジェネリック構造体の特殊化と自動実装の合成順序
