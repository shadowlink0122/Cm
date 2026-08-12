# 演算子オーバーロードのLLVM IR対訳

`impl T { operator ... }` で定義した演算子は、HIR/MIRの段階で `Type__op_add` や `Type__op_lt` という通常関数へ名前解決され、演算子式はその関数への `call` として生成される（動的ディスパッチは介在しない）。
`!=` は `==` から、`> <= >=` は `<` から自動導出されるため、専用関数は生成されず既存関数の呼び出し（引数の入れ替えや結果の反転）に落ちる。
O3ではこれらの呼び出しがLLVMのインライナで展開されてフィールド演算だけが残るが、一定の命令数を超える関数はインライン化が抑止されて `call` のまま残る。

### 定義と呼び出しの対応（O0）

```cm
struct Vec2 { int x; int y; }

impl Vec2 {
    operator Vec2 +(Vec2 other) {
        return Vec2{x: self.x + other.x, y: self.y + other.y};
    }
    operator bool <(Vec2 other) {
        return self.x * self.x + self.y * self.y < other.x * other.x + other.y * other.y;
    }
}

Vec2 sum = p + q;
println(p < q);
```

```llvm
; -O0（CM_DUMP_IR=1）
define %Vec2 @Vec2__op_add(%Vec2 %arg0, %Vec2 %arg1) { ... }
define i8 @Vec2__op_lt(%Vec2 %arg0, %Vec2 %arg1) { ... }

%7 = call %Vec2 @Vec2__op_add(%Vec2 %struct_load23, %Vec2 %struct_load24)
%8 = call i8 @Vec2__op_lt(%Vec2 %struct_load29, %Vec2 %struct_load30)
```

演算子定義は `型名__op_演算子名` の自由関数となり、`self` が第1引数、右オペランドが第2引数に対応する。
小さな構造体は値渡し（LLVMの第一級集約 `%Vec2`）で受け渡され、大きな構造体はポインタ渡しへ切り替わる（[集約コピーの設計](../../memory/aggregate-copy.md)を参照）。
この静的解決の仕組みはinterfaceの静的ディスパッチと同じ経路であり、詳細は[静的ディスパッチ](../../interface/static-dispatch.md)を参照。

### 導出演算子は引数入れ替え・結果反転の呼び出しになる

```cm
println(p < q);
println(p > q);     // >はoperator <から自動導出
```

```llvm
%7 = tail call i8 @Vec2__op_lt(%Vec2 %struct_load.fca.1.insert, %Vec2 %struct_load14.fca.1.insert)
%8 = tail call i8 @Vec2__op_lt(%Vec2 %struct_load14.fca.1.insert, %Vec2 %struct_load.fca.1.insert)
```

`p > q` は `q < p` として同じ `Vec2__op_lt` を引数を入れ替えて呼ぶだけで、`__op_gt` という関数は生成されない。
同様に `!=` は `__op_eq` の結果を反転した形になり、ユーザーは `==` と `<` の2つを定義すれば6種の比較が揃う。

### O3でのインライン化

```cm
Vec2 sum = p + q;
println(sum.x);
println(sum.y);
```

```llvm
; -O3（CM_DUMP_IR=2）: Vec2__op_addへのcallは消えている
%add.i = add i32 %trunc6, %trunc
%add12.i = add i32 %trunc6, 1
tail call void @cm_println_int(i32 %add.i)
tail call void @cm_println_int(i32 %add12.i)
```

O3ではインライナが `Vec2__op_add` を呼び出し元へ展開し、SROAが構造体をフィールドごとのスカラへ分解するため、最終的には素の `add` 命令だけが残ってオーバーロードのコストは消える。
つまり十分小さな演算子オーバーロードは、組み込み演算子と同じコードにまで最適化される。

### インライン化されずcallが残る場合

```llvm
; -O3でもVec2__op_ltはcallのまま
%7 = tail call i8 @Vec2__op_lt(%Vec2 ..., %Vec2 ...), !range !0

define i8 @Vec2__op_lt(%Vec2 %arg0, %Vec2 %arg1) local_unnamed_addr #2 { ... }
attributes #2 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) }
```

O3パイプラインの前置処理は、最適化前のIRで一定の命令数を超える関数に `noinline` を付けるため、乗算を多く含む `operator <` のような大きめの定義は展開されず `call` として残る。
この閾値判定は最適化前の冗長なIRに対して行われる点に注意が必要で、ソース上は1行の演算子でも展開後のロード・ストア数によって抑止され得る。
背景と閾値の詳細は[LLVM最適化の構成](../../codegen-native/llvm-optimization.md)と[最適化レベルの構成](../../codegen-native/optimization-levels.md)を参照。

### 組み込み型の演算はオーバーロード探索を経ない

`int + int` や `string == string` のような組み込み型の演算は、`__op_*` 関数の探索を行わず `src/internal/codegen/llvm/core/operators.cpp` の組み込み経路（`add` や `cm_strcmp`）へ直接落ちる。
そのためユーザー定義演算子の有無が組み込み演算のコード生成へ影響することはない。

## 関連資料

- [算術演算子のLLVM IR対訳](arithmetic.md)
- [比較・論理演算子のLLVM IR対訳](comparison-logical.md)
- [静的ディスパッチ](../../interface/static-dispatch.md)
- [集約コピーの設計](../../memory/aggregate-copy.md)
- [LLVM最適化の構成](../../codegen-native/llvm-optimization.md)
