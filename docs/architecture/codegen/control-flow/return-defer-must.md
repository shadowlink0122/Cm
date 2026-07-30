# return / defer / must のコード生成

return文はMIRの`Return`終端命令として戻り値ローカルへの代入＋`ret`命令に写像され、O3では複数のreturn地点が`common.ret`ブロックへ統合される。
defer文はMIR loweringが「return・break・continue・スコープ終端」の各脱出地点に登録の逆順で本体をインライン展開するため、IR上は`ret`の直前に逆順で並ぶ通常の命令列として現れる。
must文はMIR段の最適化パス（SCCP・コピー伝播・DSE）に対して文単位の`no_opt`フラグで削除・定数畳み込みを禁止し、インラインasmを含む場合はLLVMレベルでも`sideeffect`＋メモリクロバーで保護する。

### 値return

```cm
#[noinline]
int square(int x) {
    return x * x;
}
```

```llvm
define i32 @square(i32 %arg0) local_unnamed_addr #0 {
entry:
  %mul = mul i32 %arg0, %arg0
  ret i32 %mul
}
```

最適化前は戻り値専用のalloca（`%retval`）へ格納してからロードして`ret`する2段構えだが、mem2regにより`ret i32 %mul`の直接返却になる。
構造体等の大きな値はレジスタ返却ではなくsret（呼び出し側確保バッファへの書き込み）に変換される（[../../codegen-native/mir-to-llvm.md](../../codegen-native/mir-to-llvm.md)参照）。

### voidの早期returnとreturn地点の統合

```cm
#[noinline]
void maybe_log(int x) {
    if (x < 0) {
        return;
    }
    println(x);
}
```

```llvm
define void @maybe_log(i32 %arg0) local_unnamed_addr {
entry:
  %lt = icmp slt i32 %arg0, 0
  br i1 %lt, label %common.ret, label %bb2

common.ret:
  ret void

bb2:
  tail call void @cm_println_int(i32 %arg0)
  br label %common.ret
}
```

早期returnと関数末尾の暗黙returnはそれぞれ独立した`ret void`ブロックとして生成されるが、O3では`common.ret`という単一ブロックへ統合され、各経路は`br`で合流する。
値を返す関数では同様に`%common.ret.op`という`phi`または`select`が戻り値を合成する。

### mainのret i32

Cmの`int main(string[] args)`はC ABIの`i32 @main(i32, ptr, ptr)`として出力され、先頭に`tail call void @cm_args_init(i32 %arg0, ptr %0)`が挿入されてargsスライスが初期化される。
`return 0`はプロセス終了コードとしてそのまま`ret i32 0`になり、リンク後はcrt側がexitへ引き渡す（[../../codegen-native/linking-and-runtime.md](../../codegen-native/linking-and-runtime.md)参照）。

### defer: 逆順インライン展開

```cm
#[noinline]
int with_defer(int x) {
    defer println("first deferred");
    defer println("second deferred");
    println("body: {x}");
    return x * 2;
}
```

```llvm
define i32 @with_defer(i32 %arg0) local_unnamed_addr {
entry:
  %0 = tail call ptr @cm_format_unescape_braces(...)
  %1 = tail call ptr @cm_format_replace_int(ptr %0, i32 %arg0)
  tail call void @cm_println_string(ptr %1)            ; 本体のprintln
  %mul = shl i32 %arg0, 1
  tail call void @cm_println_string(...@strh.1...)     ; "second deferred"
  tail call void @cm_println_string(...@strh.2...)     ; "first deferred"
  ret i32 %mul
}
```

deferはランタイムのクリーンアップリストを持たず、MIR loweringがreturnのlowering時にスコープのdeferスタックを逆順で取り出して本体を直接展開する（`LoweringContext::get_defer_stmts`が逆順化を担う）。
そのためIRには「本体の命令列→登録と逆順のdefer本体→`ret`」という静的な並びが現れ、実行時コストは通常の関数呼び出しと変わらない。
戻り値式（`x * 2`）の評価はdefer展開より前に行われるため、deferが変数を書き換えても返却値には影響しない。

### 複数return地点への複製

早期returnを持つ関数では、defer本体が各return地点に個別に複製される。

```cm
#[noinline]
int guarded(int x) {
    defer println("cleanup");
    if (x < 0) {
        println("negative");
        return -1;
    }
    println("positive");
    return x;
}
```

```llvm
define i32 @guarded(i32 %arg0) local_unnamed_addr {
entry:
  %lt = icmp slt i32 %arg0, 0
  %. = select i1 %lt, ptr ...@strh.3..., ptr ...@strh.4...   ; "negative" / "positive"
  %.arg0 = select i1 %lt, i32 -1, i32 %arg0
  tail call void @cm_println_string(ptr nonnull %.)
  tail call void @cm_println_string(...)                     ; どちらの経路でも"cleanup"
  ret i32 %.arg0
}
```

この例ではO3が両経路の共通部分（cleanup出力と`ret`）を統合し、経路差分をメッセージと戻り値の`select`2つへ縮退させている。
break/continueによるループ脱出でも同じ機構でスコープのdeferが`Goto`の前に展開される（[loops.md](loops.md)参照）。
なおdefer展開はデストラクタ（drop）呼び出しより前に置かれるため、リソース解放順は「defer→drop」になる（[../../memory/drop-and-ownership.md](../../memory/drop-and-ownership.md)参照）。

### must: MIR最適化の抑止

```cm
#[noinline]
int with_must(int x) {
    int probe = 0;
    must {
        probe = x * 2;
    }
    return x + 1;
}
```

mustブロック内の各文にはMIRの`no_opt`フラグが立ち、SCCP（定数伝播）・コピー伝播・DSE（デッドストア除去）がその文の書き換えと削除をスキップする。
その結果、O3指定時でもLLVMへ渡る直前のIR（`CM_DUMP_IR=1`）には`%mul = mul i32 %load, %load1`と後続の`store`が保持されたまま現れる。
ただし`no_opt`が保護するのはCm自身のMIRパスまでであり、結果がどこからも観測されない純粋な計算はその後のLLVM O3パイプラインで除去されうる。
LLVMレベルまで確実に残す必要がある文は、次のインラインasm機構と組み合わせる。

### must + インラインasm: volatile扱いの実装

```cm
#[noinline]
int spin_hint(int x) {
    must {
        __asm__("nop");
    }
    return x + 1;
}
```

```llvm
define i32 @spin_hint(i32 %arg0) local_unnamed_addr #0 {
entry:
  tail call void asm sideeffect "", "~{memory},~{dirflag},~{fpsr},~{flags}"() #1
  tail call void asm sideeffect "nop", "~{memory},~{dirflag},~{fpsr},~{flags}"() #1
  tail call void asm sideeffect "", "~{memory},~{dirflag},~{fpsr},~{flags}"() #1
  %add = add i32 %arg0, 1
  ret i32 %add
}
```

mustブロック内のasmは`is_must`フラグ付きでloweringされ、LLVMの`asm sideeffect`（volatile相当）とメモリクロバー`~{memory}`が付与されるため、O3でも削除・並べ替えの対象にならない。
さらにブロック境界には空のasm文がコンパイラバリアとして前後に挿入され、mustブロックを跨いだメモリアクセスの移動を禁止する。
asm入出力で参照される変数のstore/loadには`setVolatile(true)`が適用され、レジスタ昇格による観測不能化を防ぐ（実装は`src/internal/codegen/llvm/core/statement/asm.cpp`）。
インラインasmの制約文字列とオペランド変換の全体像は[../../lowering/inline-asm.md](../../lowering/inline-asm.md)を参照。

## 関連資料

- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — Return終端命令とローカル変数設計
- [../../pipeline/mir-optimization-passes.md](../../pipeline/mir-optimization-passes.md) — no_optフラグを尊重するSCCP・DSE等のパス
- [../../codegen-native/mir-to-llvm.md](../../codegen-native/mir-to-llvm.md) — 戻り値の変換とsret
- [../../codegen-native/linking-and-runtime.md](../../codegen-native/linking-and-runtime.md) — mainの終了コードとランタイム初期化
- [../../lowering/inline-asm.md](../../lowering/inline-asm.md) — must asmのsideeffect・クロバー生成
- [../../memory/drop-and-ownership.md](../../memory/drop-and-ownership.md) — defer展開とデストラクタ呼び出しの順序
