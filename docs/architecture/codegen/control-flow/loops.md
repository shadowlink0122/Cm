# ループ（while / for / while(true)）のコード生成

Cmのwhile文とC形式for文は、MIR lowering段で「ヘッダ（条件判定）・ボディ・ラッチ（更新と後方分岐）・エグジット」の基本ブロック群へ展開され、LLVM IRではヘッダへの後方`br`を持つ自然ループとして現れる。
break/continueはそれぞれエグジットブロック・更新ブロック（whileではヘッダ）への`br`に置き換えられる。
O3ではLoopRotate（ループ回転）によりガード付きdo-while形へ変形され、帰納変数計算（SCEV）が閉形式を導出できる場合はループ自体が消えて代数式1本になる。

### whileループとO3での閉形式評価

```cm
#[noinline]
int sum_upto(int n) {
    int sum = 0;
    int i = 0;
    while (i < n) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
```

```llvm
define i32 @sum_upto(i32 %arg0) local_unnamed_addr #1 {
entry:
  %lt19 = icmp sgt i32 %arg0, 0
  br i1 %lt19, label %bb2.preheader, label %bb3

bb2.preheader:
  %0 = add nsw i32 %arg0, -1
  %1 = zext i32 %0 to i33
  %2 = add nsw i32 %arg0, -2
  %3 = zext i32 %2 to i33
  %4 = mul i33 %1, %3
  %5 = lshr i33 %4, 1
  %6 = trunc i33 %5 to i32
  %7 = add i32 %6, %arg0
  %8 = add i32 %7, -1
  br label %bb3

bb3:
  %local_2.0.lcssa = phi i32 [ 0, %entry ], [ %8, %bb2.preheader ]
  ret i32 %local_2.0.lcssa
}
```

ScalarEvolutionが帰納変数の総和を等差数列の和として認識し、ループ全体が`(n-1)(n-2)/2 + n - 1`の乗算にオーバーフロー余裕を持たせたi33演算へ置換され、後方分岐が存在しない。
ループが実行されないケース（`n <= 0`）は入口ガードの`br i1`が`phi`の初期値0を選ぶ経路として残る。
どの最適化パスがこの変形を行うかは[../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md)を参照。

### C形式forとループ回転

ボディに外部呼び出しがあるループは畳み込めず、O3でも回転済みループとして残る。

```cm
#[noinline]
void print_each(int n) {
    for (int i = 0; i < n; i = i + 1) {
        println(i);
    }
}
```

```llvm
define void @print_each(i32 %arg0) local_unnamed_addr {
entry:
  %lt11 = icmp sgt i32 %arg0, 0
  br i1 %lt11, label %bb2, label %bb3

bb2:                                              ; ボディ兼ラッチ
  %local_2.012 = phi i32 [ %add, %bb2 ], [ 0, %entry ]
  tail call void @cm_println_int(i32 %local_2.012)
  %add = add nuw nsw i32 %local_2.012, 1
  %exitcond.not = icmp eq i32 %add, %arg0
  br i1 %exitcond.not, label %bb3, label %bb2

bb3:
  ret void
}
```

最適化前は「bb1: ヘッダで条件判定 → bb2: ボディ → bb5: ラッチで`i = i + 1`してbb1へ後方分岐」という4ブロック構造だが、LoopRotateがヘッダ判定を入口ガード（`%lt11`）とラッチ末尾の終了判定（`%exitcond.not`）に分割し、ボディとラッチが単一ブロックへ融合したdo-while形になる。
帰納変数`i`はallocaへの格納から`phi`ノードへ昇格し、`add nuw nsw`のオーバーフローフラグ付き加算になる。
O0時点の素直な4ブロック構造は[../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md)の生成過程を参照。

### break / continue の分岐先

```cm
#[noinline]
int find_first_multiple(int n, int k) {
    int found = -1;
    for (int i = 1; i <= n; i = i + 1) {
        if (i % 2 == 0) {
            continue;
        }
        if (i % k == 0) {
            found = i;
            break;
        }
    }
    return found;
}
```

```llvm
bb2:                                              ; ボディ先頭（奇偶判定）
  %local_6.037 = phi i32 [ %local_6.0.be, %bb1.backedge ], [ 1, %bb2.lr.ph ]
  %0 = and i32 %local_6.037, 1
  %eq = icmp eq i32 %0, 0
  br i1 %eq, label %bb1.backedge, label %bb6      ; continueはラッチへ直行

bb6:                                              ; 倍数判定
  %mod18 = srem i32 %local_6.037, %arg1.fr
  %eq20 = icmp eq i32 %mod18, 0
  br i1 %eq20, label %bb3, label %bb1.backedge    ; breakはエグジットへ直行

bb1.backedge:                                     ; ラッチ（更新式）
  %local_6.0.be = add i32 %local_6.037, 1
  %le.not = icmp sgt i32 %local_6.0.be, %arg0
  br i1 %le.not, label %bb3, label %bb2

bb3:                                              ; エグジット
  %local_3.0 = phi i32 [ -1, %entry ], [ %local_6.037, %bb6 ], [ -1, %bb1.backedge ]
  ret i32 %local_3.0
```

MIR loweringはcontinueをforの更新ブロック（whileではヘッダ）への`Goto`、breakをエグジットブロックへの`Goto`として発行し、O3後もそれぞれ`%bb1.backedge`・`%bb3`への`br`として観察できる。
`i % 2`は`and i32 ... 1`へ、`found`変数はエグジットの`phi`（break経路は`%local_6.037`、正常終了経路は-1）へと畳まれている。
なおスコープ脱出時のdefer・デストラクタ実行はbreak/continueの`Goto`より前に挿入される（[return-defer-must.md](return-defer-must.md)参照）。

### 無限ループ while(true)

```cm
#[noinline]
int collatz_steps(int n) {
    int steps = 0;
    int x = n;
    while (true) {
        if (x <= 1) {
            break;
        }
        if (x % 2 == 0) {
            x = x / 2;
        } else {
            x = 3 * x + 1;
        }
        steps = steps + 1;
    }
    return steps;
}
```

```llvm
bb5:                                              ; 回転後のループ本体
  %local_2.031 = phi i32 [ %add21, %bb5 ], [ 0, %entry ]
  %local_4.030 = phi i32 [ %local_4.1, %bb5 ], [ %arg0, %entry ]
  %mod = and i32 %local_4.030, 1
  %eq = icmp eq i32 %mod, 0
  %div28 = lshr i32 %local_4.030, 1
  %mul = mul i32 %local_4.030, 3
  %add = add i32 %mul, 1
  %local_4.1 = select i1 %eq, i32 %div28, i32 %add
  %add21 = add i32 %local_2.031, 1
  %le = icmp slt i32 %local_4.1, 2
  br i1 %le, label %bb3, label %bb5
```

最適化前の`while (true)`はヘッダに定数条件`store i8 1`と`switch i8`を持つ形で出力されるが、O3では定数分岐が除去され、先頭のbreak条件がループの実質的な終了判定として後方分岐の条件（`%le`）に繰り上がる。
ボディ内のif-elseは副作用がないため`select`によるif変換を受け、ループ本体は分岐なしの直線コード1ブロックになっている。
breakを持たない純粋な無限ループの場合は終了判定のない後方`br`だけが残る。

## 関連資料

- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — MIRのループブロック構成とGoto/SwitchInt終端
- [../../pipeline/mir-optimization-passes.md](../../pipeline/mir-optimization-passes.md) — MIR段のループ関連最適化
- [../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md) — LoopRotate・IndVarSimplify・SCEVによる閉形式評価
- [../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md) — O0とO3のループIR比較
- [if-else.md](if-else.md) — ループ内分岐のselect化
- [for-in.md](for-in.md) — range-based forの展開形
