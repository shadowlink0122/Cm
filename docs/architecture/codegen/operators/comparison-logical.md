# 比較・論理演算子のLLVM IR対訳

比較演算子 `== != < <= > >=` は `src/internal/codegen/llvm/core/operators.cpp` でオペランド型に応じた `icmp`/`fcmp` の述語へ変換され、文字列だけは内容比較のランタイム呼び出し `cm_strcmp` になる。
論理演算子 `&& ||` はMIRの段階で短絡評価の分岐ブロックに展開され、O3では腕に副作用がなければLLVMが `select` や論理積命令へ平坦化する。
本書の抜粋は `CM_DUMP_IR=2 ./cm compile -O3` の実IRで、O3の畳み込みを避けるため `args()` 由来の実行時値を使い `println` でアンカーしている。

### 整数比較（icmpの述語対応）

```cm
int a = args().length() as int;
int b = args()[0].length() as int;
println(a == b);
println(a < b);
uint ua = a as uint;
uint ub = b as uint;
println(ua < ub);
```

```llvm
%eq = icmp eq i32 %trunc, %trunc6
%lt = icmp slt i32 %trunc, %trunc6
%ult = icmp ult i32 %trunc, %trunc6
%bool_ext = zext i1 %eq to i8
```

述語の対応は次の通りで、順序比較のみ符号付き（`s`系）と符号なし（`u`系）に分かれる。

| Cm | 符号付き整数 | 符号なし整数 | 浮動小数 |
|---|---|---|---|
| `==` | `icmp eq` | `icmp eq` | `fcmp oeq` |
| `!=` | `icmp ne` | `icmp ne` | `fcmp one` |
| `<` | `icmp slt` | `icmp ult` | `fcmp olt` |
| `<=` | `icmp sle` | `icmp ule` | `fcmp ole` |
| `>` | `icmp sgt` | `icmp ugt` | `fcmp ogt` |
| `>=` | `icmp sge` | `icmp uge` | `fcmp oge` |

比較結果の `i1` はCmの `bool`（`i8`）へ `zext` で拡張されて格納される。
符号の選択はHIR型に基づき `operators.cpp` が行う。

### 浮動小数比較（fcmpのordered述語）

```cm
double x = (args().length() as double) * 0.5;
double y = (args()[0].length() as double) * 0.25;
println(x == y);
println(x < y);
println(x >= y);
```

```llvm
%feq = fcmp oeq double %fmul, %fmul11
%flt = fcmp olt double %fmul, %fmul11
%fge = fcmp oge double %fmul, %fmul11
```

浮動小数はordered述語（`o`系）を使うため、どちらかが `nan` なら全ての比較が偽になる。
なお整数から変換しただけの値どうしの比較は、O3でLLVMが `fcmp` を元の整数の `icmp` へ書き戻すことがある。

### 文字列比較（cm_strcmpによる内容比較）

```cm
string s = args()[0];
if (s == "hello") { println("match"); }
println(s < "abc");
```

```llvm
%cm_strcmp = tail call i32 @cm_strcmp(ptr %3, ptr nonnull getelementptr inbounds (..., ptr @strh.1, ...))
%streq = icmp eq i32 %cm_strcmp, 0
%cm_strcmp9 = tail call i32 @cm_strcmp(ptr %3, ptr nonnull getelementptr inbounds (..., ptr @strh.4, ...))
%cm_strcmp9.lobit = lshr i32 %cm_strcmp9, 31
```

`string` の比較はポインタ比較ではなく、ランタイムの `cm_strcmp` を呼んで戻り値を0と比較する内容比較になる（`operators.cpp` の文字列分岐）。
順序比較 `<` も `cm_strcmp` の符号判定で、O3では `< 0` が符号ビット抽出（`lshr 31`）へ最適化されている。
文字列の内部表現は[文字列の内部表現](../../strings/representation.md)を参照。

### 短絡評価 `&&` `||`（O0の分岐ブロック構造）

```cm
bool r = a > 0 && a < 10;
```

```llvm
; -O0（CM_DUMP_IR=1）
bb2:
  %gt = icmp sgt i32 %load4, %load5
  switch i8 %load6, label %bb4 [ i8 1, label %bb3 ]

bb3:                    ; 左辺が真: 右辺を評価
  %lt = icmp slt i32 %load8, %load9
  store i8 %bool_ext10, ptr %local_9, align 1
  br label %bb5

bb4:                    ; 左辺が偽: 右辺を飛ばしてfalse
  store i8 0, ptr %local_9, align 1
  br label %bb5
```

`&&` はMIRで「左辺の分岐 → 真なら右辺評価、偽なら `false` を合流変数へ格納」という3ブロック構造に展開され、右辺は左辺が真のときしか評価されない（`||` は真偽が逆）。
この構造がCmの短絡評価の意味論そのものであり、右辺に関数呼び出しがあっても条件付きでしか実行されない。

### 短絡評価のO3でのselect化

```cm
bool p = a > 0;
bool q = b < 100;
println(p && q);
println(p || q);
if (a > 0 && check(b)) { println("both"); }
```

```llvm
%.bool_ext12 = select i1 %gt, i8 0, i8 %bool_ext12      ; p && q
%local_24.0 = select i1 %gt, i8 %bool_ext12, i8 1        ; p || q
%or.cond.not = select i1 %gt, i1 true, i1 %eq.i.not      ; a > 0 && check(b)
br i1 %or.cond.not, label %bb21, label %bb19
```

腕に副作用がない場合、O3のSimplifyCFGが短絡分岐を `select` へ平坦化し、分岐ブロックは消える。
関数呼び出しを含む条件でも、呼び出し先がインライン化されて副作用がなければ同様に `select` へ合成され、最終的な `if` の分岐1つに集約される。
この変形は意味論を変えない（右辺の観測可能な副作用がないと証明できた場合のみ行われる）。

### 論理否定 `!`

```cm
println(!p);
```

```llvm
%not_cmp = icmp eq i8 %load30, 0
%logical_not = zext i1 %not_cmp to i8
```

`!` は `bool` 値（`i8`）と0の等価比較として生成され、O3では元の比較述語の反転（`sgt` → `slt` 等）に畳み込まれて否定命令自体が消えることが多い。

## 関連資料

- [算術演算子のLLVM IR対訳](arithmetic.md)
- [三項演算子のLLVM IR対訳](ternary.md)
- [演算子オーバーロードのLLVM IR対訳](operator-overload.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化の構成](../../codegen-native/llvm-optimization.md)
