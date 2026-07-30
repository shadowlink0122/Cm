# interface / implのコード生成

`interface A { ... }`と`impl B for A { ... }`のペアは、LLVM IR上では「実装メソッドごとの通常関数`B__メソッド名`」と「インターフェイス宣言のメソッド順に実装関数ポインタを並べたvtableグローバル定数」の2種類のシンボルへ展開される。
固有メソッドの`impl B { ... }`も同じ`B__メソッド名`の命名規則で通常関数になるが、vtableには一切登録されない。
implブロックそのものに対応するIRの実体は存在せず、ブロックは関数群の定義とvtableへの登録情報を与えるだけの構文である。
本文書のIRは、次のプログラムを`CM_DUMP_IR=1 ./cm compile -O3`（最適化前IR）および`CM_DUMP_IR=2`（O3最適化後IR）でダンプした抜粋である。

```cm
import std::io::println;

interface Area {
    int area();
    string name();
}

struct Rect {
    int w;
    int h;
}

// インターフェイス実装
impl Rect for Area {
    int area() { return self.w * self.h; }
    string name() { return "rect"; }
}

// 固有メソッド
impl Rect {
    Rect scaled(int k) {
        return Rect{w: self.w * k, h: self.h * k};
    }
}

int main() {
    Rect r = Rect{w: 3, h: 4};
    Rect big = r.scaled(2);
    Area a = big;
    const int v = a.area();
    const string s = a.name();
    println("{v} {s}");
    return 0;
}
```

### `interface A { ... }`（インターフェイス宣言）

インターフェイス宣言そのものからは関数もグローバルも生成されず、宣言が決めるのは「インターフェイス値のLLVM型」と「vtable内のメソッド順序」の2点である。
インターフェイス型の値はデータポインタとvtableポインタを組にしたfat pointerで、宣言ごとに名前付きstruct型が作られる。

```llvm
%Area_fat_ptr = type { ptr, ptr }
```

`Area`型の変数・引数・フィールドはすべてこの2ポインタ構造体として扱われ、宣言内のメソッド並び順（`area`が0番、`name`が1番）が後述のvtableの添字になる。
fat pointer表現の詳細は[../../interface/dynamic-dispatch.md](../../interface/dynamic-dispatch.md)を参照。

### `impl B for A { ... }`（インターフェイス実装）

implブロック内の各メソッドは`型名__メソッド名`のシンボルを持つ通常関数として定義され、第1引数に`self`のポインタを受け取る。
あわせて、宣言のメソッド順に実装関数のアドレスを並べた`型名_インターフェイス名_vtable`という定数配列がグローバルに生成される（最適化前IR）。

```llvm
@Rect_Area_vtable = private constant [2 x ptr] [ptr @Rect__area, ptr @Rect__name]

define i32 @Rect__area(ptr %arg0) { ... }
define ptr @Rect__name(ptr %arg0) { ... }
```

O3最適化後も実装関数はそのまま残り、本体はフィールドロードと演算に畳み込まれる。

```llvm
define i32 @Rect__area(ptr nocapture readonly %arg0) local_unnamed_addr #1 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  %field_ptr1 = getelementptr %Rect, ptr %arg0, i64 0, i32 1
  %field_load2 = load i32, ptr %field_ptr1, align 4
  %mul = mul i32 %field_load2, %field_load
  ret i32 %mul
}
```

implブロック自体は「これらの関数を定義し、vtableへ登録せよ」という指示にすぎず、ブロックに対応する型やオブジェクトはIRに現れない。
vtableの生成過程と参照経路は[../../interface/dynamic-dispatch.md](../../interface/dynamic-dispatch.md)を参照。

### `impl B { ... }`（固有メソッド）

インターフェイスを伴わないimplブロックのメソッドも、まったく同じ`型名__メソッド名`規則の通常関数になる。
違いはvtableに登録されない点だけで、呼び出しは常に直接呼び出しであり、動的ディスパッチの対象にならない。

```llvm
define %Rect @Rect__scaled(ptr nocapture readonly %arg0, i32 %arg1) local_unnamed_addr #1 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  %mul = mul i32 %field_load, %arg1
  %field_ptr4 = getelementptr %Rect, ptr %arg0, i64 0, i32 1
  %field_load5 = load i32, ptr %field_ptr4, align 4
  %mul8 = mul i32 %field_load5, %arg1
  %struct_load.fca.0.insert = insertvalue %Rect poison, i32 %mul, 0
  %struct_load.fca.1.insert = insertvalue %Rect %struct_load.fca.0.insert, i32 %mul8, 1
  ret %Rect %struct_load.fca.1.insert
}
```

小さな構造体の戻り値はレジスタ渡しの集約値（`insertvalue`の連鎖）として返される。
メソッド名のマングリング規則は[../../interface/static-dispatch.md](../../interface/static-dispatch.md)と[../../generics/mangling.md](../../generics/mangling.md)を参照。

### 具象型への代入とO3での脱仮想化

`Area a = big;`の代入は、最適化前IRでは実体アドレスとvtableアドレスを`insertvalue`でfat pointerに詰める形になる。

```llvm
  %iface_fat = insertvalue %Area_fat_ptr undef, ptr %local_8, 0
  %iface_fat2 = insertvalue %Area_fat_ptr %iface_fat, ptr @Rect_Area_vtable, 1
```

このプログラムでは具象型が`Rect`だとコンパイル時に確定しているため、O3ではvtableロードが定数畳み込みで`Rect__area`へ解決され、さらにインライン化されて`main`は結果の定数だけを持つ。

```llvm
define i32 @main(i32 %0, ptr %1) local_unnamed_addr #0 {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  %2 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0))
  %3 = tail call ptr @cm_format_replace_int(ptr %2, i32 48)
  %4 = tail call ptr @cm_format_replace_string(ptr %3, ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [5 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
  tail call void @cm_println_string(ptr %4)
  ret i32 0
}
```

`a.area()`の結果は定数`48`に、`a.name()`の結果は文字列定数への直接参照に畳み込まれ、未参照になった`@Rect_Area_vtable`グローバルはO3で削除される。
脱仮想化されない経路（インターフェイス型引数など）のIRは[dispatch.md](dispatch.md)を参照。

## 関連資料

- [dispatch.md](dispatch.md) — 静的・動的ディスパッチ呼び出し側のIR対比
- [derive-with.md](derive-with.md) — `with`/`#[derive]`による自動実装関数のIR
- [../../interface/static-dispatch.md](../../interface/static-dispatch.md) — 直接呼び出しへの解決とimpl登録パスの内部実装
- [../../interface/dynamic-dispatch.md](../../interface/dynamic-dispatch.md) — fat pointer表現とvtable生成の内部実装
- [../../generics/mangling.md](../../generics/mangling.md) — `型名__メソッド名`マングリング規則
