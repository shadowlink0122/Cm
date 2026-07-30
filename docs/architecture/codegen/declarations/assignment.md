# 代入・複合代入・インクリメントのLLVM IR対訳

代入文は最適化前IRではスタックスロットへの `store`、複合代入は `load` → 演算命令 → `store` の3命令列として発行され、O3ではmem2regによりSSA値の付け替えと演算命令1つに縮退する。
`a = expr` の対訳を確認する際は、O3で定数畳み込みされないよう実行時にしか決まらない値（コマンドライン引数の個数など）を関数引数として渡すのが定石で、本文書のO3後IRはすべてその形（`CM_DUMP_IR=2`、arm64-apple-darwin）で、最適化前IRは `CM_DUMP_IR=1` で採取した。
複合代入は `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=` の10種が実装されており、それぞれ対応するLLVM命令1つへ写像される。

### 単純代入

```cm
int f_assign(int a, int b) { a = b; return a; }
```

O3後のIR:

```llvm
define i32 @f_assign(i32 %arg0, i32 returned %arg1) local_unnamed_addr {
entry:
  ret i32 %arg1
}
```

最適化前は引数用スロット `%arg_0 = alloca i32` への `store i32 %arg1` として発行されるが、mem2regでストアはSSA値の付け替えに還元され、O3では命令が1つも残らない。
つまり単純代入そのものは実行時コストを持たず、代入先の以降の使用箇所が代入元の値を直接参照する形になる。

### 複合代入（算術・ビット演算）

```cm
int f_add(int a, int b) { a += b; return a; }
```

最適化前IR（抜粋）:

```llvm
  %load1 = load i32, ptr %local_3, align 4
  %load2 = load i32, ptr %local_4, align 4
  %add = add i32 %load1, %load2
  store i32 %add, ptr %arg_0, align 4
```

O3後のIR:

```llvm
define i32 @f_add(i32 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  %add = add i32 %arg1, %arg0
  ret i32 %add
}
```

複合代入 `a OP= b` は構文糖ではなくMIRの読み出し・演算・書き戻しとして展開されるが、O3では演算命令1つに縮退する。
各演算子とLLVM命令の対応は次のとおりで、いずれも同じ形の関数で実機確認した結果である。

| Cm構文 | LLVM命令（int） | 備考 |
|---|---|---|
| `a += b` | `add i32` | doubleでは `fadd` |
| `a -= b` | `sub i32` | doubleでは `fsub` |
| `a *= b` | `mul i32` | doubleでは `fmul`（`f_scale` で確認） |
| `a /= b` | `sdiv i32` | ゼロ除算ガード付き（後述） |
| `a %= b` | `srem i32` | ゼロ除算ガード付き（後述） |
| `a &= b` | `and i32` | |
| `a \|= b` | `or i32` | |
| `a ^= b` | `xor i32` | |
| `a <<= b` | `shl i32` | |
| `a >>= b` | `ashr i32` | uintでは `lshr`（算術/論理シフトを型で選択） |

### 除算系複合代入とゼロ除算ガード

```cm
int f_div(int a, int b) { a /= b; return a; }
```

O3後のIR:

```llvm
define i32 @f_div(i32 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  %divzero.check = icmp eq i32 %arg1, 0
  br i1 %divzero.check, label %divzero.fail, label %divzero.cont

divzero.fail:
  %0 = tail call i32 @puts(ptr nonnull dereferenceable(1) @panic_msg.1)
  tail call void @exit(i32 1)
  unreachable

divzero.cont:
  %div = sdiv i32 %arg0, %arg1
  ret i32 %div
}
```

`/=` と `%=` だけはMIR段階でゼロ除算ガードが挿入され、除数0のとき "integer division by zero" を出力して `exit(1)` する分岐がO3でも保存される（除数が0でないと証明できる場合はガードごと消える）。
このガードはMIRの安全性検査に由来し、詳細は[MIR最適化パス](../../pipeline/mir-optimization-passes.md)を参照。

### インクリメント・デクリメント

```cm
int f_inc(int a) { a++; return a; }
int f_dec(int a) { --a; return a; }
int f_post(int a) { int b = a++; return b * 100 + a; }
```

O3後のIR（抜粋）:

```llvm
define i32 @f_inc(i32 %arg0) local_unnamed_addr {
entry:
  %add = add i32 %arg0, 1
  ret i32 %add
}

define i32 @f_dec(i32 %arg0) local_unnamed_addr {
entry:
  %sub = add i32 %arg0, -1
  ret i32 %sub
}

define i32 @f_post(i32 %arg0) local_unnamed_addr {
entry:
  %add = add i32 %arg0, 1
  %mul = mul i32 %arg0, 100
  %add7 = add i32 %add, %mul
  ret i32 %add7
}
```

前置・後置とも増分は `add i32 ..., 1`（デクリメントは `add i32 ..., -1` へ正規化）として現れ、文として使う限り前置と後置のIRは同一である。
式として使った場合のみ差が出て、`f_post` では後置 `a++` の評価値として更新前の `%arg0` が `b` に束縛され、更新後の `%add` と両方がSSA値として生き残る。

### 複合代入の連鎖とO3の代数的整理

```cm
int calc(int x) {
    int a = x;
    a += 3;
    a -= 1;
    a *= 5;
    a /= 2;
    return a;
}
```

O3後のIR（抜粋）:

```llvm
  %0 = mul i32 %arg0, 5
  %mul = add i32 %0, 10
  %div = sdiv i32 %mul, 2
```

複合代入を連ねた場合、O3のInstCombine/Reassociateが `(x + 3 - 1) * 5` を `x * 5 + 10` のように代数的に整理するため、ソース上の文数とIRの命令数は一致しない。
文ごとの対応を確認したいときは `-O0` と `CM_DUMP_IR=1` を使うか、上の各例のように演算子1つだけの関数へ切り出すのが確実である。

## 関連資料

- [変数宣言のIR対訳](var-decl.md)
- [グローバル宣言のIR対訳](global-decl.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化パイプラインとCM_DUMP_IR](../../codegen-native/llvm-optimization.md)
- [数値演算とキャストの一貫性](../../codegen-native/numeric-and-casts.md)
