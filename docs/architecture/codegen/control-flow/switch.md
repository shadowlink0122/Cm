# switch文のコード生成

Cmのswitch文はcaseごとに独立したブロックを持つ非フォールスルー構文であり、MIRの`SwitchInt`終端命令を経てLLVMの`switch`命令へ直接写像される。
ORパターン（`case (0 | 1 | 2)`）と範囲パターン（`case (3 ... 5)`）は同一ブロックを指す複数のcase値へ展開され、`else`節が`switch`命令のdefault行き先になる。
O3では各armの内容に応じて、`switch`命令の維持・ルックアップテーブル化（`switch.table`）・`select`連鎖への縮退のいずれかが選ばれる。

### 基本形とLLVM switch命令

```cm
#[noinline]
int day_score(int day) {
    int score = 0;
    switch (day) {
        case (0) { score = 10; }
        case (1) { score = 20; }
        case (2) { score = 35; }
        case (3) { score = 41; }
        else { score = -1; }
    }
    return score;
}
```

最適化前IRでは、判定値に対する多分岐がそのまま`switch i32`として現れる。

```llvm
bb0:
  %load1 = load i32, ptr %local_4, align 4
  switch i32 %load1, label %bb5 [
    i32 0, label %bb1
    i32 1, label %bb2
    i32 2, label %bb3
    i32 3, label %bb4
  ]
```

Cmのcaseは`case (値) { ブロック }`の形でフォールスルーが存在しないため、C言語と異なりbreak挿入や暗黙の落下を考慮するIRは生成されず、各caseブロックは末尾で必ず合流ブロックへ`br`する。
`else`節は`switch`命令のdefaultラベル（`%bb5`）にそのまま対応する。
MIRの`SwitchInt`終端は[../../pipeline/mir-design.md](../../pipeline/mir-design.md)を参照。

### O3でのルックアップテーブル化

全armが「同じ変数への定数代入」の場合、SimplifyCFGが`switch`を定数配列からのロードへ変換する。

```llvm
@switch.table.day_score = private unnamed_addr constant [4 x i32] [i32 10, i32 20, i32 35, i32 41], align 4

define i32 @day_score(i32 %arg0) local_unnamed_addr #0 {
entry:
  %0 = icmp ult i32 %arg0, 4
  br i1 %0, label %switch.lookup, label %bb6

switch.lookup:
  %1 = sext i32 %arg0 to i64
  %switch.gep = getelementptr inbounds [4 x i32], ptr @switch.table.day_score, i64 0, i64 %1
  %switch.load = load i32, ptr %switch.gep, align 4
  br label %bb6

bb6:
  %local_2.0 = phi i32 [ %switch.load, %switch.lookup ], [ -1, %entry ]
  ret i32 %local_2.0
}
```

case値が0起点の密な範囲なので、範囲判定`icmp ult 4`＋テーブル参照1回で全4分岐が置き換わり、defaultは範囲外経路の`phi`初期値-1として残る。
armが2個程度の場合はテーブルすら作らず`select`の連鎖（`switch.selectcmp` / `switch.select`）へ縮退する。
変換を担うSimplifyCFGの詳細は[../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md)を参照。

### ORパターンと範囲パターン

```cm
#[noinline]
int grade(int score) {
    int g = 0;
    switch (score) {
        case (0 | 1 | 2) { g = 1; }
        case (3 ... 6) { g = 2; }
        else { g = 3; }
    }
    return g;
}
```

```llvm
bb0:
  switch i32 %load1, label %bb3 [
    i32 0, label %bb1
    i32 1, label %bb1
    i32 2, label %bb1
    i32 3, label %bb2
    i32 4, label %bb2
    i32 5, label %bb2
    i32 6, label %bb2
  ]
```

ORパターンも範囲パターンもcase値の列挙に展開され、同一armのブロックを複数のcase値が指す形になる（範囲は各値へ展開されるため要素数分のエントリを持つ）。
この例もO3では`[7 x i32] [1,1,1,2,2,2,2]`というテーブル`@switch.table.grade`への参照1回に畳まれる。

### switch命令がO3でも残る場合

armごとの副作用が共通化できない場合は、O3後も`switch`命令による多分岐がそのまま残る。

```cm
#[noinline]
void handle(int code, int payload) {
    switch (code) {
        case (0) { println(payload); }
        case (1) { println("retry: {payload}"); }
        case (2) {
            println("fatal");
            println(payload * 2);
        }
        else { println("unknown code"); }
    }
}
```

```llvm
define void @handle(i32 %arg0, i32 %arg1) local_unnamed_addr {
entry:
  switch i32 %arg0, label %bb4 [
    i32 0, label %bb1
    i32 1, label %bb2
    i32 2, label %bb3
  ]

bb1:
  tail call void @cm_println_int(i32 %arg1)
  br label %bb5

bb2:
  %0 = tail call ptr @cm_format_unescape_braces(...)
  %1 = tail call ptr @cm_format_replace_int(ptr %0, i32 %arg1)
  tail call void @cm_println_string(ptr %1)
  br label %bb5

bb3:
  tail call void @cm_println_string(...)
  %mul = shl i32 %arg1, 1
  tail call void @cm_println_int(i32 %mul)
  br label %bb5

bb4:
  tail call void @cm_println_string(...)
  br label %bb5

bb5:
  ret void
}
```

呼び出し関数や呼び出し回数がarmごとに異なるため`select`にもテーブルにも変換できず、フロントエンドが発行した`switch i32`と4つのcase/defaultブロックがほぼ原形のまま保存される。
逆に、全armが「同じ関数を異なる定数引数で呼ぶ」形に正規化できる場合は、引数側だけがテーブル化されて呼び出しは1箇所へ共通化（sink）される。
バックエンドはこの`switch`命令から、ターゲットに応じてジャンプテーブルまたは比較ツリーを選択して機械語を生成する。

## 関連資料

- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — SwitchInt終端命令とブロック構成
- [../../codegen-native/mir-to-llvm.md](../../codegen-native/mir-to-llvm.md) — SwitchIntからLLVM switch命令への変換
- [../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md) — SimplifyCFGによるテーブル化・select化
- [if-else.md](if-else.md) — 2分岐の場合のbr/select生成
- [match.md](match.md) — enumタグに対する多分岐（match文）
