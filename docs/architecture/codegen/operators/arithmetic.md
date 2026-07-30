# 算術演算子のLLVM IR対訳

Cmの算術演算子 `+ - * / %` は、`src/internal/codegen/llvm/core/operators.cpp` の二項演算処理で、オペランドの型（符号付き整数/符号なし整数/浮動小数）に応じて `add/sub/mul/sdiv/udiv/srem/urem/fadd/fsub/fmul/fdiv/frem` へ1対1で変換される。
本書はビルド済み `cm` に `CM_DUMP_IR=2`（最適化後IRのダンプ）を付けて `-O3` でコンパイルした実際のIR抜粋で、各演算子の対訳と、O3でのみ現れる変形（定数畳み込み・剰余の分解・未使用式の消滅）を示す。
O3では定数オペランドの演算は完全に畳み込まれるため、以降の例は `args()` 由来の実行時値をオペランドにし、結果を `println` や `return` で使用してアンカーしている。

### 符号付き整数の加算・減算・乗算

```cm
int a = args().length() as int;
int b = a * 7 + 3;
println(b);
return b - a;
```

```llvm
%mul = mul i32 %trunc, 7
%add = add i32 %mul, 3
%sub = sub i32 %add, %trunc
```

`int` は `i32`、`long` は `i64` に対応し、`+ - *` はそのまま `add/sub/mul` になる。
Cmは `nsw`/`nuw` フラグを一切付けないため、符号付きオーバーフローもLLVM上のラップ（2の補数の巡回）として定義され、未定義動作にならない。
命令選択の実体は `src/internal/codegen/llvm/core/operators.cpp` にあり、詳細は[MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)を参照。

### 符号付き除算・剰余とゼロ除算検査

```cm
int b = a * 7 + 3;
int q = b / a;
int r = b % a;
println(q + r);
```

```llvm
%divzero.check = icmp eq i32 %trunc, 0
br i1 %divzero.check, label %divzero.fail, label %divzero.cont12

divzero.fail:
  %4 = tail call i32 @puts(ptr nonnull dereferenceable(1) @panic_msg)
  tail call void @exit(i32 1)
  unreachable

divzero.cont12:
  %div = sdiv i32 %add.frozen, %trunc.frozen
  %5 = mul i32 %div, %trunc.frozen
  %mod.decomposed = sub i32 %add.frozen, %5
```

`/` と `%` の直前には除数のゼロ検査（`divzero.check`）が挿入され、ゼロならパニックメッセージを出して `exit(1)` する（`operators.cpp` の除算処理）。
O0では `%` は素直に `srem` として出るが、O3では同じオペランドの `sdiv` が既にあるためLLVMが `srem` を `x - (x / y) * y` に分解し、除算命令を1回に共有する。
`freeze` はLLVMがpoison値の伝播を止めるために挿入するもので、Cm側の生成物ではない。

### 符号なし整数

```cm
uint a = args().length() as uint;
uint b = a + 100;
println(b / a);
println(b % a);
```

```llvm
%add = add i32 %trunc, 100
%udiv = udiv i32 %add.frozen, %trunc.frozen
%5 = mul i32 %udiv, %trunc.frozen
%umod.decomposed = sub i32 %add.frozen, %5
```

加減乗はビット表現が同じため符号付きと同じ `add/sub/mul` を共有し、除算・剰余のみ `udiv/urem`（O3では同様に分解される）へ分かれる。
符号の判定はHIR型に基づいて `operators.cpp` が行い、`uint/ulong/utiny/ushort` が符号なし系列を選ぶ。

### 浮動小数

```cm
double a = args().length() as double;
double b = a * 1.5 + 0.25;
println(b - a);
println(b / a);
println(b % 2.0);
```

```llvm
%fmul = fmul double %uitofp, 1.500000e+00
%fadd = fadd double %fmul, 2.500000e-01
%fsub = fsub double %fadd, %uitofp
%fdiv = fdiv double %fadd, %uitofp
%fmod = frem double %fmul, 2.000000e+00
```

`double` は `double`、`float` は `float` に対応し、`+ - * / %` は `fadd/fsub/fmul/fdiv/frem` へ変換される。
浮動小数の除算にはゼロ検査は入らず、IEEE 754に従い `inf`/`nan` を返す。
浮動小数の出力表現は[数値出力とキャストの一貫性](../../codegen-native/numeric-and-casts.md)を参照。

### 定数式の畳み込み

```cm
int main() {
    int x = 2 * 3 + 4;
    return x;
}
```

```llvm
define i32 @main(i32 %0, ptr %1) local_unnamed_addr {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  ret i32 10
}
```

オペランドが全て定数の演算はO3で完全に畳み込まれ、`mul`/`add` 命令は残らず `ret i32 10` だけになる。
O0では各リテラルの `store` と `mul`/`add` がそのまま残るため、対訳確認の際は畳み込み前の形を見たければ `-O0` と `CM_DUMP_IR=1`（最適化前IR）を使う。

### 値を捨てる式文の消滅

```cm
1 * 2;
```

結果をどこにも使わない式文は、O0の時点でMIRが乗算を生成せずオペランドのstoreだけが残り、O3ではそのstoreも含めて完全に消滅してIRに痕跡が残らない。
最適化から計算を保護したい場合の言語側の手段は `must { ... }` 文だが、LLVMのO3パスまで確実に演算を残すには結果を `return`・`println`・外部関数呼び出しへ渡して観測可能にする必要がある。
各最適化レベルで何が消えるかは[最適化レベルの構成](../../codegen-native/optimization-levels.md)を参照。

## 関連資料

- [比較・論理演算子のLLVM IR対訳](comparison-logical.md)
- [ビット演算子のLLVM IR対訳](bitwise.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化の構成](../../codegen-native/llvm-optimization.md)
- [数値出力とキャストの一貫性](../../codegen-native/numeric-and-casts.md)
