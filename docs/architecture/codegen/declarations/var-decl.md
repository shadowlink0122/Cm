# 変数宣言のLLVM IR対訳

ローカル変数宣言はMIRの仮想レジスタごとに関数先頭の `alloca` として発行され、初期化子は後続ブロックでの `store` になるスタックスロット方式が最適化前IRの基本形である。
O3ではmem2reg/SROAがスロットをSSA値へ昇格させるため、宣言に対応する `alloca` はほぼ常に消滅し、定数初期化子はLLVMの定数畳み込みで即値へ畳み込まれる。
本文書では未初期化宣言・初期化子付き宣言・const・型推論・集約型ローカルのそれぞれについて、実際の生成IR（arm64-apple-darwin、`CM_DUMP_IR=1`/`CM_DUMP_IR=2` で取得）の抜粋を対訳として示す。

### 未初期化スカラ宣言

```cm
int main() {
    int a;
    println(a);
    return 0;
}
```

最適化前IR（抜粋）:

```llvm
entry:
  %local_1 = alloca i32, align 4
  ...
bb0:
  %load = load i32, ptr %local_1, align 4
```

O3後のIR（抜粋）:

```llvm
  tail call void @cm_println_int(i32 undef)
```

未初期化のスカラ宣言は `alloca` のみを発行し、ゼロ初期化のstoreは付かないため、読み出した値は未定義（LLVM上は `undef`）になる。
O3では未定義値の読み出しがそのまま `undef` 即値として畳み込まれ、実行結果は不定になるので、スカラは初期化子付きで宣言するのが安全である（後述の集約型はこれと異なりゼロ初期化される）。

### 初期化子付き宣言と定数畳み込み

```cm
int main() {
    int b = 1 + 1;
    return b;
}
```

最適化前IR（抜粋）:

```llvm
entry:
  %local_2 = alloca i32, align 4
  %local_3 = alloca i32, align 4
  %local_4 = alloca i32, align 4
  ...
bb0:
  store i32 1, ptr %local_2, align 4
  store i32 1, ptr %local_3, align 4
  %load = load i32, ptr %local_2, align 4
  %load1 = load i32, ptr %local_3, align 4
  %add = add i32 %load, %load1
  store i32 %add, ptr %local_4, align 4
```

O3後のIR（全体）:

```llvm
define i32 @main(i32 %0, ptr %1) local_unnamed_addr {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  ret i32 2
}
```

Cmのフロントエンドは `1 + 1` を畳み込まず、リテラルごとのstoreと `add` 命令をそのままIRへ発行する（畳み込みはLLVM側の責務という分担）。
O3ではmem2regが全 `alloca` をSSA値化した後にInstCombine/SCCPが定数伝播し、宣言と演算の痕跡は消えて `ret i32 2` だけが残る。
最適化パイプラインの詳細は[LLVM最適化](../../codegen-native/llvm-optimization.md)を参照。

### const宣言

```cm
int main() {
    int b = 1 + 1;
    const int c = b * 3;
    return b + c;
}
```

ローカルの `const` は型検査段階の再代入禁止制約であり、IR上は非constの宣言と完全に同一のalloca+storeとして発行される（constを示すIR上の目印は存在しない）。
したがってO3での挙動も通常の宣言と同じで、この例は `ret i32 8` へ畳み込まれる。

### 型推論宣言（auto）

```cm
int main() {
    int b = 1 + 1;
    auto k = b * 10;
    println(k);
    return b;
}
```

O3後のIR（抜粋）:

```llvm
  tail call void @cm_println_int(i32 20)
  ret i32 2
```

`auto` は型検査で初期化子の型をそのまま採用する構文であり、HIR以降は明示型の宣言と区別が付かないため、生成IRも明示型と完全に同一である。
推論規則そのものは[型推論の設計](../../types/inference.md)を参照。

### 集約型ローカル宣言（構造体・固定長配列）

```cm
struct Point {
    int x;
    int y;
};

int main() {
    Point p;
    int[4] arr;
    println(p.x);
    println(arr[2]);
    return 0;
}
```

最適化前IR（抜粋）:

```llvm
entry:
  %local_1 = alloca %Point, align 8
  call void @llvm.memset.p0.i64(ptr %local_1, i8 0, i64 8, i1 false)
  %local_2 = alloca [4 x i32], align 4
  call void @llvm.memset.p0.i64(ptr %local_2, i8 0, i64 16, i1 false)
bb0:
  %field_ptr = getelementptr %Point, ptr %local_1, i32 0, i32 0
  %field_load = load i32, ptr %field_ptr, align 4
```

O3後のIR（抜粋）:

```llvm
  tail call void @cm_println_int(i32 0)
  tail call void @cm_println_int(i32 0)
```

構造体と固定長配列のローカル宣言はスカラと異なり `alloca` 直後に `llvm.memset` によるゼロ初期化が必ず発行されるため、未初期化でもフィールド・要素の読み出しは0を返すことが保証される。
初期化子付きの集約宣言（`Point p = {x: n, y: n + 1};` や `int[4] arr = [n, 2, 3, n * 2];`）は、memsetの後にフィールド単位・要素単位の `getelementptr` + `store` 列として展開される。
O3ではSROAが集約allocaをフィールドごとのSSA値へ分解するため、集約宣言のalloca/memset/storeは消滅し、上の例では読み出しがmemset由来の即値0へ畳み込まれる。
フィールドアクセスと配列添字のIR詳細は[スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md)、集約のコピー戦略は[集約コピー](../../memory/aggregate-copy.md)を参照。

## 関連資料

- [代入と複合代入のIR対訳](assignment.md)
- [グローバル宣言のIR対訳](global-decl.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化パイプラインとCM_DUMP_IR](../../codegen-native/llvm-optimization.md)
- [最適化レベルの意味](../../codegen-native/optimization-levels.md)
- [型推論の設計](../../types/inference.md)
