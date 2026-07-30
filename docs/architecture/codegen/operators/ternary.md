# 三項演算子のLLVM IR対訳

三項演算子 `c ? a : b` はMIRの段階で「条件分岐 → 各腕の評価 → 合流変数への格納」というifと同じブロック構造に展開され、LLVM側に専用の生成コードはない。
O3では腕に副作用がなければSimplifyCFGが分岐を `select` へ平坦化し、パターンによっては `llvm.smax` のようなintrinsicにまで縮約されるが、腕に関数呼び出し等の副作用が残る場合は分岐と `phi` が保たれる。
本書の抜粋は `CM_DUMP_IR=2 ./cm compile -O3` の実IR（O0比較は `CM_DUMP_IR=1 -O0`）で、`args()` 由来の実行時値と `println` アンカーで畳み込みを避けている。

### O0での基本形（分岐と合流変数）

```cm
int m = a > b ? a : b;
```

```llvm
; -O0（CM_DUMP_IR=1）
  %gt = icmp sgt i32 %load12, %load13
  switch i8 %load14, label %bb7 [ i8 1, label %bb6 ]

bb6:                    ; 真の腕: aをlocal_15へ
  store i32 %load16, ptr %local_15, align 4
  br label %bb8

bb7:                    ; 偽の腕: bをlocal_15へ
  store i32 %load18, ptr %local_15, align 4
  br label %bb8

bb8:                    ; 合流: local_15が式の値
  %load19 = load i32, ptr %local_15, align 4
```

MIRは三項演算子を条件分岐と合流用の一時変数に展開するため、選ばれなかった腕は実行されない（短絡評価と同じ遅延評価の意味論）。
この構造は[比較・論理演算子](comparison-logical.md)の `&&` の展開と同型である。

### O3でのselect化とintrinsic縮約

```cm
int m = a > b ? a : b;
println(m);
```

```llvm
%gt = icmp sgt i32 %trunc, %trunc6
%trunc.trunc6 = tail call i32 @llvm.smax.i32(i32 %trunc, i32 %trunc6)
```

腕が副作用のない値なら、O3は分岐ブロックを消して `select` に置き換え、さらにmax/min等の既知パターンは `llvm.smax.i32` のようなintrinsicへ縮約する。
一般の値対（例: `c ? x + 1 : y`）では `select i1 %c, i32 %a, i32 %b` の形になる。
selectと分岐の使い分けはCm側ではなくLLVMのコスト判断で、副作用や実行コストの高い腕は分岐のまま残る。

### 集約型（構造体）の腕はフィールド単位のselectへ分解

```cm
Vec2 v = a > b ? Vec2{x: a, y: 1} : Vec2{x: b, y: 2};
println(v.x + v.y);
```

```llvm
%gt = icmp sgt i32 %trunc, %trunc6
%trunc.trunc6 = tail call i32 @llvm.smax.i32(i32 %trunc, i32 %trunc6)   ; v.x
%local_24.sroa.4.0 = select i1 %gt, i32 1, i32 2                        ; v.y
%add = add i32 %local_24.sroa.4.0, %trunc.trunc6
```

O0では両腕がそれぞれ構造体一時（alloca）を構築して合流変数へコピーするが、O3ではSROAが構造体をフィールドごとのスカラへ分解し、使われるフィールドだけがselect（またはintrinsic）として残る。
構造体コピーの扱いは[MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)を参照。

### 腕に副作用がある場合は分岐とphiが残る

```cm
int m = a > b ? f(a) : g(b);    // f/gはprintlnを含む関数
println(m);
```

```llvm
  br i1 %gt, label %bb6, label %bb7

bb6:
  tail call void @cm_println_string(...)    ; インライン化されたf
  %mul.i = shl i32 %trunc, 1
  br label %bb8

bb7:
  tail call void @cm_println_string(...)    ; インライン化されたg
  %add.i = add i32 %trunc6, 1
  br label %bb8

bb8:
  %local_15.0 = phi i32 [ %mul.i, %bb6 ], [ %add.i, %bb7 ]
```

腕に観測可能な副作用（出力・書き込み等）がある場合、O3でも分岐は消せず、MIRの合流変数はmem2regによって `phi` へ昇格した形で残る。
呼び出し自体はインライン化され、選ばれた腕の副作用だけが実行される意味論が保たれる。

### 腕の一時オブジェクトと共通部分のくくり出し

```cm
string s = a > 1 ? base + "-big" : base + "-small";
println(s);
```

```llvm
%gt = icmp sgt i32 %trunc, 1
%. = select i1 %gt, ptr ... @strh.1 ..., ptr ... @strh.2 ...
%6 = tail call ptr @cm_string_concat(ptr %5, ptr nonnull %.)
```

各腕が生成する文字列連結の一時オブジェクトは、MIR側で三項結果の一時として管理され、使用後にdrop対象となる。
この例ではO3が両腕に共通する `cm_string_concat` 呼び出しをくくり出し、選択はリテラルポインタの `select` だけに縮約されて、腕ごとの一時生成そのものが1回の呼び出しへ統合されている。
一時オブジェクトの解放経路は[dropと所有権](../../memory/drop-and-ownership.md)を参照。

## 関連資料

- [比較・論理演算子のLLVM IR対訳](comparison-logical.md)
- [演算子オーバーロードのLLVM IR対訳](operator-overload.md)
- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)
- [LLVM最適化の構成](../../codegen-native/llvm-optimization.md)
- [最適化レベルの構成](../../codegen-native/optimization-levels.md)
