# 構造体のコピー・値渡し・RAIIのLLVM IR対訳

構造体の代入・引数渡し・戻り値は値セマンティクスであり、IR上は「小さい集約は第一級SSA値（`load %S`/`store %S`・値渡し）、大きい集約はmemcpy・ポインタ渡し・sret」の2系統に分かれ、デストラクタ`~self()`はMIR loweringがスコープ終端へ静的に挿入する通常関数呼び出しになる。
本書では最適化前IR（`CM_DUMP_IR=1`）で挿入位置と2系統の分岐を示し、O3後IR（`CM_DUMP_IR=2`）でSROA・インライン展開後に何が残るかを対比する。

### 構造体代入のコピー（小型はload/store・大型はmemcpy）

```cm
struct Small { int x; int y; };
struct Big { long[32] data; };

Small b = a;
Big h = g;
```

コピーサイズはDataLayoutで測られ、128バイト未満は第一級集約のload/store、128バイト以上はGEP展開せず`llvm.memcpy`1発になる（-O0のIR抜粋）。

```llvm
%struct_load = load %Small, ptr %local_2, align 4
store %Small %struct_load, ptr %local_1, align 4
call void @llvm.memcpy.p0.p0.i64(ptr %local_10, ptr %local_11, i64 256, i1 false)
```

小型をload/storeに保つのはmem2reg・SROAでレジスタへ昇格させるため、大型をmemcpyに落とすのはSROAの全要素SSA展開によるコンパイル時間爆発を防ぐためである。
さらにO1以上ではMIRの集約コピー伝播が「コピー元→一時→最終先」の連鎖を畳み込むため、O3では最適化前IRの時点でこの代入コピー自体が消えていることが多い。
閾値の設計意図と実装位置は[../../memory/aggregate-copy.md](../../memory/aggregate-copy.md)を参照。

### 値渡し引数（小型は第一級値・大型はポインタ+隔離コピー）

```cm
int sum_small(Small s) { s.x = s.x + s.y; return s.x; }
long head_big(Big b) { return b.data[0]; }
```

16バイト以下の構造体はLLVM引数として`%Small`の第一級値で渡り、それを超えるとポインタ渡しへ変わる。

```llvm
define i32 @sum_small(%Small %arg0) { ... }
define i64 @head_big(ptr %arg0) {
entry:
  %byval_copy_0 = alloca %Big, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr %byval_copy_0, ptr %arg0, i64 256, i1 false)
  ...
}
```

ポインタ渡しのままだと呼び出し先の変更が呼び出し元に波及して値セマンティクスが壊れるため、呼び出し先のエントリで`byval_copy`のローカルコピーを作って隔離する。
O3ではこの隔離コピーも読まれたフィールド分だけに縮退し、`head_big`は先頭8バイトの`load i64`1命令になる。

```llvm
define i64 @head_big(ptr nocapture readonly %arg0) {
entry:
  %byval_copy_0.sroa.0.0.copyload = load i64, ptr %arg0, align 1
  ret i64 %byval_copy_0.sroa.0.0.copyload
}
```

### 構造体戻り値（sret）

```cm
Big make_big(long seed) {
    Big b;
    b.data[0] = seed;
    return b;
}
```

16バイト超の構造体を返す関数は`ret`で値を返す代わりに、先頭に隠し出力ポインタを取る`void`関数へ変換される（sret）。

```llvm
define void @make_big(ptr noalias sret(%Big) %arg0, i64 %0) {
  ...
  call void @llvm.memcpy.p0.p0.i64(ptr %arg0, ptr %retval, i64 256, i1 false)
  ret void
}
```

呼び出し側は受け取り先のallocaを確保してそのポインタを渡すため、`Big g = make_big(42);`は`call void @make_big(ptr %local_7, i64 42)`になる。
O3ではローカル`b`と`%retval`がSROAで消え、sretポインタへ直接`store i64 %0`と残り248バイトのゼロ`memset`を書く形へ最適化される。
16バイト以下の構造体はsretにならず第一級値のまま`ret %Small`で返る（判定は[../../memory/aggregate-copy.md](../../memory/aggregate-copy.md)）。

### デストラクタ`~self()`のスコープ終端呼び出し

```cm
impl File {
    ~self() { println("close {self.fd}"); }
}

int main() {
    File a(1);
    {
        File b(2);
        println("inner");
    }
    println("outer");
    return 0;
}
```

`~self()`は`型名__dtor`という通常関数へloweringされ、呼び出しはMIR loweringのスコープ管理がブロック終端と関数終端（return直前）へ静的に挿入する。
最適化前IRでは、内側ブロックを抜ける位置に`b`のdtorが、`return`の直前に`a`のdtorが現れる。

```llvm
bb2:                                    ; println("inner")
  call void @cm_println_string(...)
bb3:                                    ; 内側ブロック終端: bのdtor
  call void @File__dtor(ptr %load2)
bb4:                                    ; println("outer")
  call void @cm_println_string(...)
bb5:                                    ; return直前: aのdtor
  store i32 0, ptr %retval, align 4
  call void @File__dtor(ptr %load3)
```

同一スコープに複数の変数がある場合は宣言と逆順に破棄され、ループ本体スコープでは毎周回の本体終端で呼ばれる。
O3では`File__dtor`本体がインライン展開されて呼び出し自体は消えるが、副作用（この例ではprintln）は挿入位置の順序どおり最終IRに残る。
挿入アルゴリズムと一時オブジェクトのdropは[../../memory/drop-and-ownership.md](../../memory/drop-and-ownership.md)を参照。

### move（所有権移動とdtor登録解除）

```cm
File a(1);
File b = move a;
```

`move`は型チェッカが移動元をmoved状態にして以後の使用をコンパイルエラーにし、MIR側では移動元のデストラクタ登録を解除する。
その結果IRには移動先`b`に対する`@File__dtor`呼び出しが1回だけ挿入され、二重解放が静的に排除される。
ビット列のコピー命令自体は通常代入と同じ（小型load/store・大型memcpy）で、moveの実体は「dtorをどちらに1回だけ挿入するか」の選択である。
use-after-move診断と登録解除の実装は[../../memory/drop-and-ownership.md](../../memory/drop-and-ownership.md)を参照。

## 関連資料

- [構造体の宣言とアクセスのIR対訳](struct-decl-access.md)
- [集約コピーのlowering](../../memory/aggregate-copy.md)
- [RAII・dropパスと所有権](../../memory/drop-and-ownership.md)
- [LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md)
