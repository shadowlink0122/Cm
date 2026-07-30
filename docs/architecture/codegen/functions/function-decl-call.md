# 関数宣言・呼び出しのLLVM IR対訳

Cmの関数宣言は原則としてソース上の関数名がそのままLLVMシンボル名になり、mainだけがargc/argv受け取りとi32戻り値固定の特別扱いを受ける。
引数はスカラと16バイト以下の構造体を値渡し、16バイト超の集約をポインタ渡しに落とし、16バイト超の構造体戻り値はsret（隠し出力ポインタ）宣言へ変換される。
本書は代表的な関数宣言・呼び出し構文と、-O3で実際に生成されるIRの対訳を示す。

### 関数宣言と呼び出し

```cm
int add(int a, int b) {
    return a + b;
}

int main() {
    const int r = add(1, 2);
    println("{r}");
    return 0;
}
```

```llvm
define i32 @add(i32 %arg0, i32 %arg1) local_unnamed_addr #0 {
  ; -O3では本体が %arg0 + %arg1 の1命令に縮約される
}

define i32 @main(i32 %0, ptr %1) local_unnamed_addr {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  %3 = tail call ptr @cm_format_replace_int(ptr %2, i32 3)   ; add(1, 2)はインライン化+定数畳み込みで消滅
  ...
}
```

関数名`add`がそのままシンボル`@add`になり、-O3では呼び出し側にインライン化されて定数`3`へ畳み込まれる一方、外部から参照されうる`@add`の定義自体は残る。
コード生成内部では引数型サフィックス（`_i`/`_i64`/`_S構造体名`等）を付けた関数ID（`generateFunctionId`、`src/internal/codegen/llvm/core/translate/signature.cpp`）で関数マップを引くが、これは呼び出し解決用の内部キーであり、出力シンボル名には付かない。
自由関数の同名オーバーロードは型検査段階で`free-function overloading is not supported`として拒否されるため、シンボル衝突は起きない。
変換全体の枠組みは[MIR→LLVM IR変換の構造](../../codegen-native/mir-to-llvm.md)を参照。

### mainの特別扱い

```cm
int main() {
    return 0;
}
```

```llvm
define i32 @main(i32 %0, ptr %1) local_unnamed_addr {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  ...
  ret i32 0
}
```

Cm言語仕様上のmainは無引数だが、ホストOS向けコード生成はシグネチャを`i32 (i32 argc, ptr argv)`へ差し替え、プロローグで`cm_args_init`へargc/argvを保存する（`signature.cpp`の`convertFunctionSignature`）。
保存された引数はCmコードから`std::env::args()`で取得でき、mainの戻り値型はC標準準拠で常に`i32`に固定される。
mainは関数IDマングリングの対象外で、シンボル名は常に`@main`のままリンカへ渡る。

### スカラ引数と小さな構造体の値渡し

```cm
struct Small {
    int a;
    int b;
}

int sum_small(Small s) {
    return s.a + s.b;
}
```

```llvm
define i32 @sum_small(%Small %arg0) local_unnamed_addr #0 {
entry:
  %arg0.fca.0.extract = extractvalue %Small %arg0, 0
  %arg0.fca.1.extract = extractvalue %Small %arg0, 1
  %add = add i32 %arg0.fca.0.extract, %arg0.fca.1.extract
  ret i32 %add
}
```

スカラ引数はそのままの型で、16バイト以下の構造体はSystem V ABI準拠の第一級構造体値（`%Small`）で値渡しされる。
-O3ではSROAが構造体値を`extractvalue`でフィールドSSA値へ分解し、alloca経由のコピーは消える。

### 大きな集約のポインタ渡し

```cm
struct Big {
    long[32] data;
}

long sum_big(Big b) {
    long total = 0;
    for (int i = 0; i < 32; i++) {
        total = total + b.data[i];
    }
    return total;
}
```

```llvm
define i64 @sum_big(ptr nocapture readonly %arg0) local_unnamed_addr #1 {
  ; 呼び出し先エントリで値渡しセマンティクス用のコピーを読み出す
}

; 呼び出し側（main、-O3）
call void @llvm.memcpy.p0.p0.i64(ptr ... %local_5, ptr ... %local_7, i64 256, i1 false)
%2 = call i64 @sum_big(ptr nonnull %local_5)
```

16バイト超の構造体引数は宣言が`ptr`のポインタ渡しへ変換され、値渡しセマンティクスは呼び出し境界のコピー（-O3では`llvm.memcpy`）で保たれる。
巨大な集約を第一級値として渡すとSROAによるIRの超線形膨張を招くため、この経路はサイズ判定で明示的に塞がれている。
サイズ閾値とコピー戦略の全体像は[集約コピーのlowering](../../memory/aggregate-copy.md)を参照。

### sret戻り値の宣言形

```cm
Big make_big(long seed) {
    Big b;
    for (int i = 0; i < 32; i++) {
        b.data[i] = seed + i;
    }
    return b;
}
```

```llvm
define void @make_big(ptr noalias nocapture writeonly sret(%Big) %arg0, i64 %0) local_unnamed_addr #2 {
entry:
  %add.1 = add i64 %0, 1   ; -O3でループが全展開され、呼び出し元バッファへ直接store
  ...
}

; 呼び出し側（main、-O3）
call void @make_big(ptr nonnull %local_7, i64 100)
```

16バイト超の構造体戻り値は戻り値型が`void`になり、第0引数に`sret`属性付きの隠し出力ポインタが前置される。
`noalias`と`sret`属性により、最適化は呼び出し元バッファへの直接書き込みとして扱える。
判定関数`needsSretReturn`と属性付与は`signature.cpp`の`convertFunctionSignature`にある。

### デフォルト引数

```cm
int greet(int a, int b = 10) {
    return a + b;
}

int main() {
    const int r1 = greet(1);
    const int r2 = greet(1, 2);
    ...
}
```

```llvm
%2 = call i32 @greet(i32 %load, i32 %load1)   ; greet(1) → greet(1, 10)に補完済み
%3 = call i32 @greet(i32 %load3, i32 %load4)  ; greet(1, 2)
```

デフォルト引数はHIR/MIR loweringが呼び出し側で実引数として補完するため、LLVMに届く時点で全呼び出しが完全な引数列を持ち、関数定義は1つのままになる。
なおコンストラクタに限り`overload self(int v)`による同名多重定義が可能で、`Resource__ctor`と`Resource__ctor_1`のような連番サフィックスのシンボルに分かれる。

## 関連資料

- [MIR→LLVM IR変換の構造](../../codegen-native/mir-to-llvm.md) — 関数ID生成・シグネチャ変換・sret変換の実装詳細
- [集約コピーのlowering](../../memory/aggregate-copy.md) — 値渡し/ポインタ渡し/sretのサイズ判定とmemcpy戦略
- [最適化レベルの生成過程](../../codegen-native/optimization-levels.md) — O0/O3での関数境界の残り方の違い
