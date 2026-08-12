# ラムダ式・関数ポインタのLLVM IR対訳

Cmのラムダ式`(int x) => { ... }`はHIR loweringで`__lambda_N`という名前の独立関数へ切り出され、キャプチャした外部変数は生成関数の先頭引数として前置される。
クロージャオブジェクトのヒープ確保は行われず、直接呼び出せる経路ではキャプチャ値を呼び出し点で引数に積むだけで完結する。
本書はラムダ・キャプチャ・関数ポインタ変数経由の間接呼び出しが-O3でどのようなIRに落ちるかの対訳を示す。

### ラムダ式と関数ポインタ型変数への代入

```cm
int main() {
    // 関数ポインタ型は 戻り値型*(引数型列) で書く
    const int*(int) double_it = (int x) => {
        return x * 2;
    };

    const int r = double_it(5);
    ...
}
```

```llvm
define i32 @__lambda_0(i32 %arg0) local_unnamed_addr #0 {
entry:
  %mul = shl i32 %arg0, 1
  ret i32 %mul
}

; 最適化前IR（CM_DUMP_IR=1）の呼び出し部
%2 = call i32 @__lambda_0(i32 %load1)
```

ラムダ本体は`__lambda_0`という通常の関数として定義され、変数`double_it`経由の呼び出しはMIR loweringが直接呼び出しへ解決する。
`__lambda_`プレフィックスの関数は引数型サフィックスマングリングの対象外で、名前がそのままシンボルになる。
-O3では本体が`shl`1命令へ縮約され、この例のように引数が定数なら呼び出しごと定数に畳み込まれる。
命名規約と切り出しの実装は[クロージャのlowering](../../lowering/closures.md)を参照。

### キャプチャの環境渡し

```cm
int main() {
    const int multiplier = 3;

    // 外部変数multiplierを値キャプチャするクロージャ
    const int*(int) scale = (int x) => {
        return x * multiplier;
    };

    const int r = scale(5);
    ...
}
```

```llvm
define i32 @__lambda_1(i32 %arg0, i32 %arg1) local_unnamed_addr #0 {
entry:
  %mul = mul i32 %arg1, %arg0   ; %arg0 = キャプチャmultiplier、%arg1 = 仮引数x
  ret i32 %mul
}

; 最適化前IRの呼び出し部: キャプチャ値を先頭引数へ積む
%3 = call i32 @__lambda_1(i32 %load2, i32 %load3)
```

キャプチャ変数は生成関数のシグネチャ先頭へ前置され（`fn(cap0, ..., param0, ...)`）、呼び出し点でキャプチャ値が第0引数から順に積まれる。
キャプチャは常に値キャプチャであり、環境構造体のヒープ確保は発生しない。
`map`/`filter`等のC実装高階関数へクロージャを渡す場合のみ、キャプチャ列をスタック上のi64環境配列へ集約してサンク経由で呼ぶ環境化が行われる（詳細は[クロージャのlowering](../../lowering/closures.md)）。

### 関数ポインタへの関数代入と間接呼び出し

```cm
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int*(int, int) pick(bool use_add) {
    if (use_add) {
        return &add;
    }
    return &sub;
}

int main() {
    int*(int, int) op = pick(true);
    const int r1 = op(3, 2);
    op = pick(false);
    const int r2 = op(3, 2);
    ...
}
```

```llvm
; 最適化前IR: 関数アドレスを返し、変数経由で間接呼び出しする
define ptr @pick(i8 %arg0) {
  ...
  ret ptr @add
  ...
  ret ptr @sub
}
%indirect_call = call i32 %load2(i32 %load3, i32 %load4)

; -O3: 分岐がselectへ縮約され、mainの間接呼び出しは脱仮想化+定数畳み込みで消滅
define nonnull ptr @pick(i8 %arg0) local_unnamed_addr #0 {
  %common.ret.op = select i1 %cond, ptr @add, ptr @sub
  ...
}
%3 = tail call ptr @cm_format_replace_int(ptr %2, i32 5)
%4 = tail call ptr @cm_format_replace_int(ptr %3, i32 1)
```

`&add`は関数アドレス定数`ptr @add`になり、関数ポインタ変数経由の呼び出しはロードした`ptr`への間接call（`call i32 %load2(...)`）に落ちる。
-O3ではポインタの流れが追跡できる場合に間接呼び出しが直接呼び出しへ脱仮想化され、この例では`add(3,2)=5`と`sub(3,2)=1`まで定数畳み込みされる。
MIRの時点でクロージャ変数の参照先が確定している場合は、間接callを経由せず最初から直接呼び出し+キャプチャ前置が生成される。

### -O3でのインライン化との関係

`__lambda_N`関数は到達可能関数の起点集合に常に含められ、関数ポインタ経由で参照されうるためデッドコード除去では消えない。
一方で呼び出し点が静的に解決できる場合、-O3のインライナは通常関数と同様にラムダ本体を呼び出し側へ展開するため、上記の各例でもmain内の呼び出し命令自体は消えて定義だけが残る。
MIRレベルのインライナはラムダ・クロージャをインライン化対象から除外し、展開判断をLLVM側に一任している。

## 関連資料

- [クロージャのlowering](../../lowering/closures.md) — `__lambda_N`切り出し・キャプチャ前置・高階関数向け環境化の実装詳細
- [関数宣言・呼び出しのIR対訳](function-decl-call.md) — 通常関数のシンボル名と引数渡しの規則
- [MIR→LLVM IR変換の構造](../../codegen-native/mir-to-llvm.md) — 間接呼び出しの直接化を含む終端命令変換
