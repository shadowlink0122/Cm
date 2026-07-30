# インターフェイスメソッド呼び出しのディスパッチIR対比

インターフェイスのメソッド呼び出しは、レシーバの型がコンパイル時に確定するかどうかでIRが2系統に分かれる。
ジェネリック境界`<T: A>`経由の呼び出しは単相化により具象型ごとの特殊化関数が生成され、本体は`型名__メソッド名`への直接呼び出し（O3ではさらにインライン化）になる。
一方、インターフェイス型の変数・引数経由の呼び出しは、fat pointerからvtableポインタを取り出して関数ポインタをロードする間接呼び出しになる。
本文書のIRは、次のプログラムを`CM_DUMP_IR=1 ./cm compile -O3`（最適化前IR）および`CM_DUMP_IR=2`（O3最適化後IR）でダンプした抜粋である。

```cm
import std::io::println;

interface Shape {
    int area();
    void describe();
}

struct Sq {
    int s;
}
impl Sq for Shape {
    int area() { return self.s * self.s; }
    void describe() { println("sq {self.s}"); }
}

struct Ci {
    int r;
}
impl Ci for Shape {
    int area() { return 3 * self.r * self.r; }
    void describe() { println("ci {self.r}"); }
}

// ジェネリック境界: 単相化で具象型ごとの直接呼び出しに解決される
<T: Shape> void describe_static(T t) {
    t.describe();
}

// インターフェイス型引数: fat pointerのvtable経由で間接呼び出しされる
int area_dynamic(Shape sh) {
    return sh.area();
}

int main() {
    Sq sq = Sq{s: 5};
    Ci ci = Ci{r: 2};
    describe_static(sq);
    describe_static(ci);
    const int a = area_dynamic(sq);
    const int b = area_dynamic(ci);
    println("{a} {b}");
    return 0;
}
```

### ジェネリック境界`<T: Shape>`経由の呼び出し（静的ディスパッチ）

`describe_static`は呼び出された具象型ごとに`describe_static__Sq`と`describe_static__Ci`へ単相化され、本体の`t.describe()`は最適化前IRの時点で`Sq__describe`/`Ci__describe`への直接呼び出しになっている。

```llvm
define void @describe_static__Sq(%Sq %arg0) {
  ...
  call void @Sq__describe(ptr %load)
```

vtableは一切関与せず、O3では`Sq__describe`の本体が特殊化関数へインライン展開されて、フィールド抽出と書式化ランタイム呼び出しだけが残る。

```llvm
define void @describe_static__Sq(%Sq %arg0) local_unnamed_addr {
entry:
  %arg0.fca.0.extract = extractvalue %Sq %arg0, 0
  %0 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
  %1 = tail call ptr @cm_format_replace_int(ptr %0, i32 %arg0.fca.0.extract)
  tail call void @cm_println_string(ptr %1)
  ret void
}
```

単相化が呼び出し名内の型パラメータを具象型名へ書き換える仕組みは[../../interface/static-dispatch.md](../../interface/static-dispatch.md)を、特殊化名の規則は[../../generics/monomorphization.md](../../generics/monomorphization.md)を参照。

### インターフェイス型変数経由の呼び出し（動的ディスパッチ）

`area_dynamic`はfat pointer構造体`%Shape_fat_ptr = type { ptr, ptr }`を値渡しで受け取り、呼び出し側でどの具象型が来るか分からないため、O3でも間接呼び出しが残る。

```llvm
define i32 @area_dynamic(%Shape_fat_ptr %arg0) local_unnamed_addr {
entry:
  %arg0.fca.0.extract = extractvalue %Shape_fat_ptr %arg0, 0
  %arg0.fca.1.extract = extractvalue %Shape_fat_ptr %arg0, 1
  %func_ptr = load ptr, ptr %arg0.fca.1.extract, align 8
  %0 = tail call i32 %func_ptr(ptr %arg0.fca.0.extract)
  ret i32 %0
}
```

要素0がデータポインタ、要素1がvtableポインタで、`area`はインターフェイス宣言の0番目のメソッドなのでvtable先頭の関数ポインタをそのままロードして呼んでいる（2番目のメソッドならオフセット8のロードになる）。
最適化前IRでは同じ処理が`getelementptr`によるフィールドアクセスと`load`の列として現れ、fat pointerの構築側では実体アドレスと`@Sq_Shape_vtable`定数のアドレスが詰められる。

```llvm
@Ci_Shape_vtable = private constant [2 x ptr] [ptr @Ci__area, ptr @Ci__describe]
@Sq_Shape_vtable = private constant [2 x ptr] [ptr @Sq__area, ptr @Sq__describe]

  %vtable_field = getelementptr inbounds %Shape_fat_ptr, ptr %fat_ptr, i32 0, i32 1
  store ptr @Sq_Shape_vtable, ptr %vtable_field, align 8
  %2 = call i32 @area_dynamic(%Shape_fat_ptr %fat_ptr_value)
```

fat pointerの構築経路とvtable生成の内部実装は[../../interface/dynamic-dispatch.md](../../interface/dynamic-dispatch.md)を参照。

### 呼び出し側で具象型が見える場合のO3脱仮想化

`main`から見ると`area_dynamic(sq)`のfat pointerは`{&sq, @Sq_Shape_vtable}`の定数なので、O3はvtableロードを定数畳み込みして`Sq__area`へ脱仮想化し、さらにインライン化して結果を定数にする。

```llvm
  %6 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0))
  %7 = tail call ptr @cm_format_replace_int(ptr %6, i32 25)
  %8 = tail call ptr @cm_format_replace_int(ptr %7, i32 12)
  tail call void @cm_println_string(ptr %8)
```

`{a} {b}`の穴埋めが`i32 25`と`i32 12`の定数になっており、`main`内には間接呼び出しもvtable参照も残らない。
つまり「インターフェイス型を経由したら必ず実行時ディスパッチになる」のではなく、間接呼び出しが残るのは具象型を静的に追跡できない境界（インターフェイス型の関数引数・戻り値、実行時に要素が決まる配列やスライスなど）だけである。

## 関連資料

- [impl.md](impl.md) — `impl`が生成するシンボル群とvtableグローバルの形
- [derive-with.md](derive-with.md) — `with`/`#[derive]`による自動実装関数のIR
- [../../interface/static-dispatch.md](../../interface/static-dispatch.md) — 直接呼び出しへ解決される条件と単相化の内部実装
- [../../interface/dynamic-dispatch.md](../../interface/dynamic-dispatch.md) — fat pointer・vtable・間接呼び出し生成の内部実装
- [../../generics/monomorphization.md](../../generics/monomorphization.md) — 特殊化関数の生成過程
