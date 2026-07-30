# if / else のコード生成

Cmのif文はMIRの基本ブロックと`SwitchInt`終端命令に分解され、LLVM IRでは条件のi1化（`icmp`）とブロック間分岐（`br` / `switch`）として現れる。
フロントエンドが生成する素直なIRでは条件bool値をi8としてスタックに格納してから分岐するが、O3では`mem2reg`と`SimplifyCFG`により中間の格納・再読込が消え、分岐そのものが`select`命令や算術演算へ変換されることも多い。
本書では基本ブロック構造が見える最適化前IRと、O3で分岐が残る場合・消える場合の両方を実際の出力で示す。

### if-else の基本ブロック構造

```cm
#[noinline]
void report(int x) {
    if (x > 10) {
        println("big");
    } else {
        println("small");
    }
}
```

最適化前（`CM_DUMP_IR=1`）のIRでは、条件式の結果がi1からi8へ`zext`されて一時ローカルへ格納され、それを`switch i8`で判定してthen/elseブロックへ分岐する。

```llvm
bb0:
  %gt = icmp sgt i32 %load, %load1
  %bool_ext = zext i1 %gt to i8
  store i8 %bool_ext, ptr %local_4, align 1
  %load2 = load i8, ptr %local_4, align 1
  switch i8 %load2, label %bb2 [
    i8 1, label %bb1
  ]

bb1:                                              ; then節
  call void @cm_println_string(ptr getelementptr inbounds (i8, ptr @strh, i64 16))
  br label %bb4

bb2:                                              ; else節
  call void @cm_println_string(ptr getelementptr inbounds (i8, ptr @strh.1, i64 16))
  br label %bb3
```

MIRの分岐終端は真偽2択でも`SwitchInt`で統一表現されるため、LLVMでも`br i1`ではなく「i8値1ならthen、それ以外はelse」という`switch i8`の形で出力される。
then/else両ブロックは合流ブロックへ`br`で戻り、これがif-else後の続行地点になる。
MIRの終端命令設計は[../../pipeline/mir-design.md](../../pipeline/mir-design.md)を参照。

### O3でのselect化（分岐除去）

上記`report`をO3でコンパイルすると、両arm唯一の差分である文字列ポインタが`select`に畳まれ、分岐が完全に消える。

```llvm
define void @report(i32 %arg0) local_unnamed_addr {
entry:
  %gt = icmp sgt i32 %arg0, 10
  %. = select i1 %gt, ptr getelementptr inbounds (..., ptr @strh, ...), ptr getelementptr inbounds (..., ptr @strh.1, ...)
  tail call void @cm_println_string(ptr nonnull %.)
  ret void
}
```

`SimplifyCFG`が両armの共通部分（`cm_println_string`呼び出し）をくくり出し、異なる部分（引数の文字列定数）だけを`select`で選択する形へ変換している。
i8への`zext`とスタック往復も`mem2reg`と`InstCombine`で除去され、条件は裸のi1（`%gt`）として直接使われる。
最適化パスの適用順は[../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md)を参照。

### 分岐が残るif（elseなし・副作用あり）

armの副作用が共通化できない場合は、O3でも`br i1`による本物の条件分岐が残る。

```cm
#[noinline]
void warn_if_over(int x, int limit) {
    if (x > limit) {
        println("over: {x}");
    }
    println("checked");
}
```

```llvm
define void @warn_if_over(i32 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  %gt = icmp sgt i32 %arg0, %arg1
  br i1 %gt, label %bb1, label %bb3

bb1:
  %0 = tail call ptr @cm_format_unescape_braces(...)
  %1 = tail call ptr @cm_format_replace_int(ptr %0, i32 %arg0)
  tail call void @cm_println_string(ptr %1)
  br label %bb3

bb3:
  tail call void @cm_println_string(...)
  ret void
}
```

then節のフォーマット・出力呼び出しは条件成立時のみ実行すべき副作用なので`select`化できず、`icmp` + `br i1` + 合流ブロックという教科書的な形が保たれる。
elseがないifは「then節→合流」「条件不成立→合流へ直行」の2経路になる。

### else-ifチェーンの畳み込み

```cm
#[noinline]
int classify(int x) {
    if (x > 0) {
        return 1;
    } else if (x == 0) {
        return 0;
    } else {
        return -1;
    }
}
```

```llvm
define i32 @classify(i32 %arg0) local_unnamed_addr #0 {
entry:
  %eq = icmp ne i32 %arg0, 0
  %. = sext i1 %eq to i32
  %gt.inv = icmp slt i32 %arg0, 1
  %common.ret.op = select i1 %gt.inv, i32 %., i32 1
  ret i32 %common.ret.op
}
```

最適化前は各条件ごとにブロックが連なる3分岐チェーンだが、O3では全armが定数returnであることから完全にブランチレス化され、`sext i1`（0か-1）と`select`の組み合わせだけで3値を合成する。
複数の`ret`は`common.ret`ブロックへ統合され、関数全体が単一基本ブロックになる。
純関数と判定されたため`memory(none)`属性（`#0`）も付与されている。

### 条件のi1化と符号なし比較

Cmの比較演算子は型検査で決まった符号性に応じて`icmp sgt/ugt`等へ写像され、結果は常にLLVMのi1になる。
MIR上でbool型はi8（0/1）として扱われるため、変数へ格納する場合のみ`zext i1 → i8`と`load i8 → switch`の対が挿入され、分岐条件として直接使われる場合はO3でi1のまま伝播する。
数値型と比較命令の対応は[../../codegen-native/numeric-and-casts.md](../../codegen-native/numeric-and-casts.md)を参照。

## 関連資料

- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — MIRの基本ブロックとSwitchInt終端命令の設計
- [../../codegen-native/mir-to-llvm.md](../../codegen-native/mir-to-llvm.md) — MIRからLLVM IRへの変換過程
- [../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md) — SimplifyCFG・InstCombine等の最適化パス
- [../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md) — O0/O3での生成IRの違い
- [loops.md](loops.md) — ループ内条件分岐のコード生成
- [switch.md](switch.md) — 多分岐のswitch命令とジャンプテーブル
