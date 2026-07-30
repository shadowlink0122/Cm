# 構造体の宣言とアクセスのLLVM IR対訳

Cmの`struct`宣言はLLVMの名前付き構造体型`%型名`へ、フィールドアクセスは`getelementptr`（GEP）へ素直に変換され、その上でO3ではSROA/mem2regがallocaとGEPをスカラSSA値へ分解するため、最終IRには構造体の痕跡がほとんど残らない。
本書ではフロントエンドが発行する最適化前IR（`CM_DUMP_IR=1`で取得）で変換の骨格を示し、O3最適化後IR（`CM_DUMP_IR=2`で取得）で分解後の姿を対比する。

### 構造体宣言のLLVM型

```cm
struct Point {
    int x;
    int y;
};

struct Sensor {
    string name;
    Point pos;
    bool active;
};
```

```llvm
%Point = type { i32, i32 }
%Sensor = type { ptr, %Point, i8 }
```

`struct 名前`は宣言と同名の名前付き型`%名前`になり、フィールドは宣言順のままLLVM型の要素順に並ぶ。
`string`はヒープ上の文字列本体を指す`ptr`、`bool`は`i8`としてレイアウトされ、ネストした構造体はポインタではなく`%Point`そのものが埋め込まれる（値セマンティクス）。
パディングと合計サイズの計算はLLVM DataLayoutに一本化されている（詳細は[../../memory/aggregate-copy.md](../../memory/aggregate-copy.md)）。

### フィールドの読み書き（GEPパターン）

```cm
Point p = {x: 3, y: 4};
p.x = p.y + 10;
```

最適化前IRでは、フィールドアクセスは「構造体型・ベースポインタ・インデックス0・フィールド番号」のGEPと、その結果ポインタへのload/storeになる。

```llvm
%field_ptr2 = getelementptr %Point, ptr %local_1, i32 0, i32 1   ; p.y の読み出し
%field_load = load i32, ptr %field_ptr2, align 4
%add = add i32 %field_load, 10
%field_ptr4 = getelementptr %Point, ptr %local_1, i32 0, i32 0   ; p.x への書き込み
store i32 %add, ptr %field_ptr4, align 4
```

ネストしたフィールド`r.max.x`は外側から内側へGEPを連ねる形になる。

```llvm
%field_ptr9 = getelementptr %Rect, ptr %local_1, i32 0, i32 1    ; r.max
%field_ptr10 = getelementptr %Point, ptr %field_ptr9, i32 0, i32 0  ; .x
%field_load = load i32, ptr %field_ptr10, align 4
```

O3ではSROAが`alloca %Point`をフィールドごとのSSA値へ分解し、このプログラム全体は定数畳み込みで`call void @cm_println_int(i32 14)`の1命令に潰れる。
つまりGEP構造は最適化の入力であり、ローカル構造体に限れば最終IRに残らないのが正常である（分解の仕組みは[../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md)）。

### コンストラクタ`self()`の呼び出し

```cm
impl Counter {
    overload self(int start, int step) {
        self.value = start;
        self.step = step;
    }
}

Counter c(100, 5);
```

コンストラクタは`型名__ctor`（オーバーロードは`__ctor_2`のような連番）という通常関数へloweringされ、第1引数に構築先のポインタ（`self`）、以降にユーザー引数を取る。

```llvm
call void @Counter__ctor_2(ptr %load, i32 100, i32 5)
```

O3後もexternalリンケージの関数定義は残り、`self`ポインタ経由のGEP storeという構造が観察できる（先頭フィールドのGEPはベースポインタそのものに畳まれる）。

```llvm
define void @Counter__ctor_2(ptr nocapture writeonly %arg0, i32 %arg1, i32 %arg2) {
entry:
  store i32 %arg1, ptr %arg0, align 4
  %field_ptr2 = getelementptr %Counter, ptr %arg0, i64 0, i32 1
  store i32 %arg2, ptr %field_ptr2, align 4
  ret void
}
```

一方`main`側の呼び出しはO3のインライン展開で消え、この例では`c.next()`の結果まで畳み込まれて`cm_println_int(i32 105)`だけが残る。
メソッド呼び出し（`c.next()`→`@Counter__next(ptr)`）も同じ「selfポインタを第1引数に取る通常関数」方式である（[../../lowering/method-chains.md](../../lowering/method-chains.md)）。

### 構造体配列

```cm
Point[4] pts;
pts[i].y = i * 2;
```

固定長の構造体配列は`[N x %型]`のallocaになり、要素アクセスは「配列GEPで要素ポインタ→構造体GEPでフィールドポインタ」の2段構成になる。

```llvm
%local_1 = alloca [4 x %Point], align 8
%idx_ext = sext i32 %idx_load to i64
%elem_ptr = getelementptr inbounds [4 x %Point], ptr %local_1, i64 0, i64 %idx_ext
%field_ptr = getelementptr %Point, ptr %elem_ptr, i32 0, i32 1
store i32 %load, ptr %field_ptr, align 4
```

インデックスは`i64`へ符号拡張されてからGEPに入り、定数トリップのループならO3でループごと展開・畳み込みされる。
配列・スライス全般のcodegenは[../../codegen-native/slice-and-array-codegen.md](../../codegen-native/slice-and-array-codegen.md)を参照。

### ジェネリック構造体の特殊化型名

```cm
struct Pair<K, V> {
    K key;
    V value;
};

Pair<int, string> p;
Pair<long, bool> q;
```

```llvm
%Pair__int__string = type { i32, ptr }
%Pair__long__bool = type { i64, i8 }
```

ジェネリック構造体は使用された型引数の組ごとに単相化され、`基本名__型引数1__型引数2`という`__`区切りのマングル名で独立したLLVM型が生成される。
型パラメータのままIRに現れることはなく、フィールド型は特殊化時点で具象型（`K=int`→`i32`、`V=string`→`ptr`）に置換される。
命名規則と単相化の仕組みは[../../generics/mangling.md](../../generics/mangling.md)と[../../generics/monomorphization.md](../../generics/monomorphization.md)を参照。

## 関連資料

- [構造体のコピー・値渡し・RAIIのIR対訳](struct-copy-raii.md)
- [集約コピーのlowering](../../memory/aggregate-copy.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md)
- [ジェネリクスのマングリング](../../generics/mangling.md)
