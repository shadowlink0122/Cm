# ビット演算子のLLVM IR対訳

Cmのビット演算子 `& | ^ << >> ~` は `src/internal/codegen/llvm/core/operators.cpp` で `and/or/xor/shl/ashr/lshr` へ1対1で変換され、`~` だけは専用命令がないため全ビット1との `xor` になる。
シフト量はLLVMの制約（両オペランド同型）に合わせて左オペランドの型幅へ揃えられ、右シフトは左オペランドの符号で `ashr`（算術）と `lshr`（論理）に分かれる。
本書の抜粋は `CM_DUMP_IR=2 ./cm compile -O3` の実IRで、O3の定数畳み込みを避けるため `args()` 由来の実行時値をオペランドにし `println` でアンカーしている。

### ビットAND・OR・XOR

```cm
int a = args().length() as int;
int b = args()[0].length() as int;
println(a & b);
println(a | b);
println(a ^ b);
```

```llvm
%bitand = and i32 %trunc6, %trunc
%bitor = or i32 %trunc6, %trunc
%xor = xor i32 %trunc6, %trunc
```

`& | ^` はそのまま `and/or/xor` になり、符号付き・符号なしで命令は変わらない（ビット表現に対する演算のため）。
`bool` 同士の `& | ^` も同じ命令で、`i8` 幅のまま演算される。

### ビット反転 `~`

```cm
println(~a);
```

```llvm
%bitnot = xor i32 %trunc, -1
```

LLVMにはNOT命令がないため、`~` は全ビット1（`-1`）との `xor` として生成される。
単項演算の生成箇所も `operators.cpp` にある。

### 左シフト `<<`

```cm
println(a << 2);
long lv = a as long;
println(lv << b);
```

```llvm
%shl = shl i32 %trunc, 2
%shl30 = shl i64 %sext, %7      ; %7はint bをi64へsextした値
```

`<<` は `shl` になり、結果の型は左オペランドの型で決まる。
LLVMのシフト命令は両オペランドが同型である必要があるため、`long << int` のように幅が異なる場合は右オペランド（シフト量）が左オペランドの型幅へ符号拡張（符号なしならゼロ拡張）される。

### 右シフト `>>`（符号付きはashr・符号なしはlshr）

```cm
println(a >> 1);        // int: 算術シフト
uint u = a as uint;
println(u >> 1);        // uint: 論理シフト
```

```llvm
%shr = ashr i32 %trunc, 1
%lshr = lshr i32 %trunc, 1
```

`>>` は左オペランドが符号付きなら符号ビットを複製する `ashr`、符号なしなら0を詰める `lshr` になり、符号の判定はHIR型で行われる。
つまり `-8 >> 1` は `-4`、`0x80000000u >> 1` は `0x40000000` という C系言語の慣習どおりの意味論である。

### 小さい整数型のシフト幅

```cm
tiny t = args().length() as tiny;
tiny r = t << 1;
println(r);
```

```llvm
%trunc3 = trunc i64 %3 to i8
%4 = shl i8 %trunc3, 1
%sext = sext i8 %4 to i32       ; println(int)へ渡すための拡張
```

Cmには「演算前にintへ昇格する」という暗黙の整数拡張はなく、`tiny`（`i8`）や `short`（`i16`）のシフトは宣言型の幅のまま行われて上位ビットは自然に落ちる。
その後 `println` などより広い型が要る文脈に渡すときだけ `sext`/`zext` が入る。
なおO3ではLLVMが狭い幅のシフトを広い幅のシフトとマスク（`and`）の組へ書き換えることがあるが、観測される値は同じである。

### 定数シフトの畳み込み

オペランドが全て定数の `1 << 4 | 3` のような式はO3で完全に畳み込まれて即値になり、シフト命令はIRに残らない。
結果を使わない式文はO0の時点で演算が生成されず、O3ではオペランドの痕跡ごと消滅する（詳細は[算術演算子](arithmetic.md)の式文消滅の節）。

## 関連資料

- [算術演算子のLLVM IR対訳](arithmetic.md)
- [比較・論理演算子のLLVM IR対訳](comparison-logical.md)
- [キャストと`is`のLLVM IR対訳](cast-is.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化の構成](../../codegen-native/llvm-optimization.md)
