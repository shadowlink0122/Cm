# for-in（range-based for）のコード生成

Cmのrange-based for文 `for (int x in xs)` は専用のMIRノードを持たず、HIR lowering段でイテレート対象の種類に応じた既存構文へ脱糖される。
スライスと固定長配列は「インデックス変数の初期化・長さとの比較・要素ロード・インデックス増分」を持つC形式forへ、`iter()`メソッドを持つ構造体は`while (it.has_next()) { x = it.next(); ... }`パターンへ展開される。
そのためLLVM IRに現れるのは通常のループと同じヘッダ・ボディ・ラッチ構造であり、O3では要素アクセスのインライン化や閉形式評価まで通常ループと同様に適用される。

### スライスに対するfor-in

```cm
#[noinline]
int sum_slice(int[] xs) {
    int sum = 0;
    for (int x in xs) {
        sum = sum + x;
    }
    return sum;
}
```

```llvm
define i32 @sum_slice(ptr %arg0) local_unnamed_addr #0 {
entry:
  %0 = tail call i64 @cm_slice_len(ptr %arg0)
  %1 = and i64 %0, 4294967295
  %ult21.not = icmp eq i64 %1, 0
  br i1 %ult21.not, label %bb3, label %bb2

bb2:                                              ; ボディ兼ラッチ
  %local_2.023 = phi i32 [ %add, %bb2 ], [ 0, %entry ]
  %local_4.022 = phi i32 [ %add15, %bb2 ], [ 0, %entry ]
  %sext = sext i32 %local_4.022 to i64
  %2 = tail call i32 @cm_slice_get_i32(ptr %arg0, i64 %sext)
  %add = add i32 %2, %local_2.023
  %add15 = add nuw i32 %local_4.022, 1
  %3 = tail call i64 @cm_slice_len(ptr %arg0)
  %trunc = trunc i64 %3 to i32
  %ult = icmp ult i32 %add15, %trunc
  br i1 %ult, label %bb2, label %bb3

bb3:
  %local_2.0.lcssa = phi i32 [ 0, %entry ], [ %add, %bb2 ]
  ret i32 %local_2.0.lcssa
}
```

HIR loweringが隠しインデックス変数`__for_in_idx_x`を導入し、条件を`__i < xs.len()`、要素取得を`xs[__i]`とするforへ書き換えるため、IRにはランタイム関数`cm_slice_len`（長さ取得）と`cm_slice_get_i32`（境界検査付き要素ロード）の呼び出しが現れる。
長さはループヘッダで毎回再評価される仕様のため、O3の回転後ループでもラッチに`cm_slice_len`呼び出しが残っている（ループ中の要素追加・削除に追従する）。
束縛変数`x`は要素ロード結果のコピーであり、O3では独立したallocaを持たず`phi`と演算列に溶け込む。
スライスのランタイム表現と要素ディスパッチは[../../codegen-native/slice-and-array-codegen.md](../../codegen-native/slice-and-array-codegen.md)を参照。

### 固定長配列に対するfor-in

```cm
#[noinline]
int sum_array(int seed) {
    int[4] arr = [seed, seed + 1, seed + 2, seed + 3];
    int sum = 0;
    for (int a in arr) {
        sum = sum + a;
    }
    return sum;
}
```

```llvm
define i32 @sum_array(i32 %arg0) local_unnamed_addr #1 {
entry:
  %add7 = add i32 %arg0, 2
  %add13 = add i32 %arg0, 3
  %reass.add = shl i32 %arg0, 1
  %add29.1 = or i32 %reass.add, 1
  %add29.2 = add i32 %add7, %add29.1
  %add29.3 = add i32 %add13, %add29.2
  ret i32 %add29.3
}
```

固定長配列では長さがコンパイル時定数なので、脱糖後の条件は`__i < 4`という定数比較になり、要素アクセスも配列allocaへの直接GEPになる。
トリップカウント4の小ループはO3で完全アンロールされ、SROAが配列を4つのスカラへ分解した結果、ループも配列も消えて`4*seed + 6`相当の算術式だけが残る。
最適化前IRでは`icmp slt i32 %i, 4`のヘッダと`getelementptr [4 x i32]`の要素ロードを持つ通常のループ構造が観察できる。

### iter()メソッドを持つ型に対するfor-in

配列以外の型は、`iter()`メソッドが返すイテレータの`has_next()` / `next()`プロトコルへ脱糖される。

```cm
struct Counter { int limit; }
struct CounterIter { int current; int limit; }

impl Counter {
    CounterIter iter() { CounterIter it; it.current = 0; it.limit = self.limit; return it; }
}

impl CounterIter {
    bool has_next() { return self.current < self.limit; }
    int next() { const int v = self.current; self.current = self.current + 1; return v; }
}

#[noinline]
int sum_counter(int limit) {
    Counter c;
    c.limit = limit;
    int sum = 0;
    for (int v in c) {
        sum = sum + v;
    }
    return sum;
}
```

```llvm
define i32 @sum_counter(i32 %arg0) local_unnamed_addr #0 {
entry:
  %lt.i.not15 = icmp sgt i32 %arg0, 0
  br i1 %lt.i.not15, label %bb3.preheader, label %bb4

bb3.preheader:                                    ; 閉形式: (n-1)(n-2)/2 + n - 1
  %0 = add nsw i32 %arg0, -1
  %1 = zext i32 %0 to i33
  %2 = add nsw i32 %arg0, -2
  %3 = zext i32 %2 to i33
  %4 = mul i33 %1, %3
  %5 = lshr i33 %4, 1
  %6 = trunc i33 %5 to i32
  %7 = add i32 %6, %arg0
  %8 = add i32 %7, -1
  br label %bb4

bb4:
  %local_4.0.lcssa = phi i32 [ 0, %entry ], [ %8, %bb3.preheader ]
  ret i32 %local_4.0.lcssa
}
```

HIR loweringは隠し変数`__for_in_iter_v = Counter__iter(c)`を導入し、ループを`while (CounterIter__has_next(&it)) { int v = CounterIter__next(&it); ... }`へ書き換える（selfはポインタ渡し）。
O3では`iter`/`has_next`/`next`の3メソッドがすべてインライン化されてイテレータ構造体がSROAでスカラ分解され、結果として単純なカウントアップループと等価になり、スライスの節と同じ閉形式（等差数列の和）へ畳まれる。
イテレータという抽象を挟んでも、最終的なIRは手書きのwhileループと同水準まで消えることがこの例で確認できる。
どの型がイテレータ展開を受けるかの判定（`iter()`メソッドの有無）は型検査段で行われる。

## 関連資料

- [../../codegen-native/slice-and-array-codegen.md](../../codegen-native/slice-and-array-codegen.md) — スライス・配列のランタイム表現と要素アクセス
- [../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md) — インライン化・SROA・完全アンロールのパス
- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — 脱糖後ループのMIR表現
- [loops.md](loops.md) — 脱糖先であるwhile / C形式forのブロック構造
- [if-else.md](if-else.md) — ループ内条件のi1化とselect化
