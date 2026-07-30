# ポインタ構文のLLVM IR対訳

本書はCmのポインタ関連構文（アドレス取得・デリファレンス・`->`・ポインタ演算・null比較・キャスト・`__sizeof__`・関数ポインタ）が、nativeバックエンドの`-O3`でどのLLVM IRに変換されるかを対訳形式で示す。
IR抜粋は `CM_DUMP_IR=2 ./cm compile -O3` で採取した最適化後の実出力であり、Cmの全ポインタ型はLLVMのopaqueポインタ型 `ptr` に統一される。

### アドレス取得 `&a`

```cm
int use_addr(int seed) {
    int x = seed;
    int* p = &x;
    *p = *p + 1;
    return x;
}
```

```llvm
define i32 @use_addr(i32 %arg0) local_unnamed_addr #2 {
entry:
  %add = add i32 %arg0, 1
  ret i32 %add
}
```

`&x` はMIRでは `x` のスタックスロット（`alloca`）のアドレスだが、アドレスが関数外へ逃げない場合はO3のSROA/mem2regが `alloca` ごと消去し、ポインタ経由の読み書きは純粋なレジスタ演算に畳み込まれる。
一方、`set_y(&pt, 5)` のようにアドレスが他関数へ渡って逃げる場合は `alloca` が残り、`call void @set_y(ptr nonnull %local_1, i32 5)` の形で実アドレスがそのまま渡される。
allocaとスタックスロットの割り当ては[MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)を参照。

### デリファレンス `*p`（読み/書き）

```cm
int deref_read(int* p) {
    return *p;
}
```

```llvm
define i32 @deref_read(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %field_load = load i32, ptr %arg0, align 4
  ret i32 %field_load
}
```

読み取り `*p` は `load`、代入 `*p = v` は `store i32 %arg1, ptr %arg0` の1命令にそのまま対応し、ポインタの指す型は命令側の値型（`i32`）とアライメントで表現される。
O3はアクセスパターンから `readonly`/`writeonly`/`nocapture` などの引数属性と `memory(argmem: ...)` 関数属性を推論し、呼び出し側の最適化を可能にする。
適用される属性推論パスは[LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md)を参照。

### フィールドアクセス `p->field`（GEP+load/store）

```cm
struct Point { int x; int y; }

int get_y(Point* pt) {
    return pt->y;
}
```

```llvm
define i32 @get_y(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %field_load.elt3 = getelementptr inbounds %Point, ptr %arg0, i64 0, i32 1
  %field_load.unpack4 = load i32, ptr %field_load.elt3, align 4
  ret i32 %field_load.unpack4
}
```

`->` は構造体型を添えた `getelementptr`（GEP）でフィールドのアドレスを計算し、読みなら `load`、書き（`pt->y = v`）なら同じGEPに `store i32 %arg1, ptr %field_ptr` を後続させる2命令構成になる。
`n->next->value` のような多段アクセスはGEP+loadの連鎖になり、先頭フィールド（オフセット0）へのアクセスではGEPが省略されてポインタが直接使われる。
構造体レイアウトの決定は[MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)を参照。

### ポインタ演算 `p + 1` / `p + i`

```cm
int at(int* p, int i) {
    return *(p + i);
}
```

```llvm
define i32 @at(ptr nocapture readonly %arg0, i32 %arg1) local_unnamed_addr #0 {
entry:
  %idx_ext = sext i32 %arg1 to i64
  %scaled_idx = shl nsw i64 %idx_ext, 2
  %ptr_add = getelementptr i8, ptr %arg0, i64 %scaled_idx
  %field_load = load i32, ptr %ptr_add, align 4
  ret i32 %field_load
}
```

ポインタ加算はC同様に要素サイズでスケーリングされ、コード生成はバイト単位GEP（`getelementptr i8`）にスケール済みオフセットを与える形を取り、変数添字は `sext` でi64へ拡張後に `shl nsw`（`sizeof(int) = 4` の乗算）でオフセットになる。
オフセットが定数の場合は畳み込まれ、`*(p + 1)` は `getelementptr i8, ptr %arg0, i64 4` の1命令になる。
配列とスライスの要素アクセスも同じGEP構成に合流する（[スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md)）。

### null比較 `p == null` / `p != null`

```cm
struct Node { int value; Node* next; }

bool is_null(Node* p) {
    return p == null;
}
```

```llvm
define i8 @is_null(ptr readnone %arg0) local_unnamed_addr #0 {
entry:
  %ptr_eq = icmp eq ptr %arg0, null
  %bool_ext = zext i1 %ptr_eq to i8
  ret i8 %bool_ext
}
```

`null` リテラルとの比較はLLVMのnullポインタ定数との `icmp eq`/`icmp ne` になり、`bool` 値として返す場合はi8表現へ `zext` で拡張される。
`while (cur != null)` のようなループ条件では同じ `icmp` が拡張なしで分岐条件（`br i1`）に直結する（boolのi8表現は[MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md)を参照）。

### ポインタキャスト `as Node*` / `as long`

```cm
Node* to_node(void* raw) {
    return raw as Node*;
}
```

```llvm
define ptr @to_node(ptr readnone returned %arg0) local_unnamed_addr #2 {
entry:
  ret ptr %arg0
}
```

ポインタ型同士のキャストはopaqueポインタでは型情報の付け替えだけなので命令を一切生成せず、値がそのまま流れる（型検査上の意味だけを持つ）。
ポインタと整数の相互変換は命令になり、`p as long` は `ptrtoint ptr %arg0 to i64`、整数からの逆方向は `inttoptr`、`0 as void*` のような定数は `ptr null` に畳み込まれる。
`as` キャストの型規則は[キャストと型変換](../../types/casts.md)、数値側の変換命令は[数値演算とキャストのコード生成](../../codegen-native/numeric-and-casts.md)を参照。

### `__sizeof__`

```cm
long node_size() {
    return __sizeof__(Node) as long;
}
```

```llvm
define i64 @node_size() local_unnamed_addr #2 {
entry:
  ret i64 16
}
```

`__sizeof__(T)` はDataLayoutに基づくコンパイル時定数に解決されて実行時計算は発生せず（`Node = { i32, ptr }` はパディング込みで16バイト）、ジェネリクス内の `__sizeof__(T)` も単相化後に同様の定数へ落ちる（[ジェネリクスの単相化](../../generics/monomorphization.md)を参照）。

### 関数ポインタ変数と間接呼び出し

```cm
int apply(int*(int, int) fn, int x, int y) {
    return fn(x, y);
}
```

```llvm
define i32 @apply(ptr nocapture readonly %arg0, i32 %arg1, i32 %arg2) local_unnamed_addr {
entry:
  %indirect_call = tail call i32 %arg0(i32 %arg1, i32 %arg2)
  ret i32 %indirect_call
}
```

関数ポインタ型 `int*(int, int)` も値としてはただの `ptr` であり、関数名を値として使うと `ptr @add` のような関数アドレス定数になり、変数経由の呼び出しはレジスタ値への `call`（間接呼び出し）になる。
`if (use_mul) { return mul; } return add;` のような選択は `select i1 %cond, ptr @mul, ptr @add` になり、その値の呼び出しには `tail call i32 %common.ret.op.i(...), !callees !{ptr @add, ptr @mul}` と候補集合が `!callees` メタデータで注記される。
代入経路から呼び出し先が一意に定まる場合はO3が間接呼び出しを直接呼び出しへ脱仮想化して定数畳み込みまで進める。
キャプチャを持つラムダは関数ポインタではなくクロージャ表現になる（[クロージャのlowering](../../lowering/closures.md)）。

## 関連資料

- [MIRからLLVM IRへの変換](../../codegen-native/mir-to-llvm.md) — alloca・GEP・分岐など本書のIRを生成する変換層の詳細
- [LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md) — O3で適用されるパス構成と属性推論
- [キャストと型変換](../../types/casts.md) — `as` キャストの型規則
- [スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md) — 配列・スライス要素アクセスのGEP構成
- [FFI呼び出しのIR対訳](ffi-calls.md) — extern関数へのポインタ・文字列渡し
