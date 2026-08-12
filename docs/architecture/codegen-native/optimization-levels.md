# 最適化レベルO0/O3の生成過程と実IR差分

native/jitバックエンドの最適化はMIRパスパイプラインとLLVM PassBuilderの二段構成であり、`-O0` は両段を素通しして「1ローカル=1 alloca、1コピー=load/store対」の素直なLLVM IRをそのまま命令選択へ渡し、`-O3` はMIRで定数畳み込み・コピー伝播による式の整理を済ませた後にLLVMの標準O3パイプラインでmem2reg・インライン化・ループ評価までを完遂する。本書は同一Cmソースに対して各レベルが実際に出力するLLVM IRの差分を対比し、どの段のどの最適化が効いたかを実機ダンプに基づいて記述する。MIRの各パスの内部仕様は[MIR最適化パスカタログ](../pipeline/mir-optimization-passes.md)、LLVMパイプラインの構築詳細は[LLVM最適化の構成](llvm-optimization.md)が単一情報源であり、本書は過程の対比と実差分に集中する。

## 概要

最適化レベルは `-O0`〜`-O3` で指定し（`src/cmd/cm/options.cpp:201-210`）、未指定時の既定値は `run`・`compile` 共通でO3である（`src/cmd/cm/options.hpp:30`）。
`.cmconfig.yml` の `compile: optimization:` が既定値を上書きし、CLIの `-O<n>` はそれよりさらに優先される（優先順位はCLI > config > デフォルト、`src/cmd/cm/main.cpp:58-60`）。

各レベルの生成過程は次の観測手段で段階ごとにダンプできる（native/jit共通のMIR段はドライバ、LLVM段はバックエンド実装が担う）。

| 観測手段 | 出力内容 | 実装箇所 |
|---|---|---|
| `--mir` | MIR最適化前のMIR | `src/cmd/cm/build.cpp:465-468` |
| `--mir-opt` | MIR最適化後のMIR | `src/cmd/cm/build.cpp:547-552` |
| `CM_DUMP_IR=1`（compile） | LLVM最適化前のIR（MIR→LLVM変換直後）をstderrへ | `src/internal/codegen/llvm/native/codegen.cpp:67-72` |
| `CM_DUMP_IR=2`（compile） | LLVM最適化後のIRをstderrへ | `src/internal/codegen/llvm/native/codegen.cpp:91-96` |
| `--lir-opt`（compile） | LLVM最適化後のIRをstdoutへ | `src/cmd/cm/backend/llvm.cpp:201-207` |
| `CM_DUMP_IR`（run） | JIT実行直前（LLVM最適化前）のIRをstderrへ | `src/internal/codegen/llvm/jit/jit_engine.cpp:207-210` |

O0とO3の対比の要点は「O0は両段とも何もしない」ことに尽きる。
MIR段は `create_standard_passes(0)` が空のパス列を返し（`src/internal/mir/passes/core/manager.cpp:36-39`）、LLVM段は `optimize()` が冒頭で即returnする（`codegen.cpp:766-768`）ため、O0のIRはMIR→LLVM変換器の出力そのものである。
O3はMIR段で最大7回の固定点反復（O1は3回・O2は5回、`manager.cpp:90-123`）、LLVM段で `buildPerModuleDefaultPipeline(O3)` を実行する二段最適化になる。

## O0の生成過程

MIR段では最適化パスが1つも走らないが、正当性のための処理だけはレベル非依存で実行される。
文字列再代入時の旧バッファ解放（`StringReassignFree`）は最適化ではなくメモリ健全性のパスなので、ドライバがO0でも直接実行する（`build.cpp:503-511`）。
`compile` 時はさらに関数単位DCEとプログラム単位DCE（未使用の自動生成関数の削除）がレベル非依存で走る（`build.cpp:528-544`。`run` はインターフェースの動的ディスパッチがあるためスキップ）。

LLVM段は `optimize()` が即returnするためPassBuilderのパイプラインは構築すらされず、MIR→LLVM変換器（[MIR→LLVM IR変換](mir-to-llvm.md)）の出力がそのまま命令選択に渡る。
生成IRの特徴は変換器の機械的な方針をそのまま反映する。

- 全MIRローカルが関数先頭で `alloca` される（構造体ローカルは `memset` によるゼロ初期化付き）
- MIRの `copy` 1つが `load`+`store` の対になり、定数もいったん一時の `alloca` へ `store` してから `load` される
- bool値は `zext i1 → i8` で格納され、条件分岐はMIRの `switchInt` を写した `switch i8` になる
- 基本ブロック構造はMIRのbb番号のまま保存される

jit（`cm run -O0`）も同じMIRから同じ変換器でIRを作り、`JITEngine::optimizeModule()` が `optLevel <= 0` で無変換のまま返す（`jit_engine.cpp:103-104`）ため、実行されるIRの形はnativeのO0と同型である。

## O3の生成過程

MIR段では標準パイプライン（SCCP・定数畳み込み・GVN・コピー伝播・デッドストア除去・制御フロー簡約・末尾呼び出しマーク・LICM・DCE、O2以上は定数畳み込み/コピー伝播/DCEの再実行を追加）を固定点まで反復する（`manager.cpp:25-77`）。
このMIR段の役割は「式の整理」であり、実機ダンプで確認できる効果は定数のオペランド埋め込み（`_6 = const 10; _7 = _5 < _6` → `_7 = _5 < const 10`）、純定数式の畳み込み（`(3 + 4) * 5` → `const 35`）、冗長な一時コピーの削除、構造体フィールドへの定数直接ストア化である。
ループ構造の変換・インライン化・alloca昇格はMIR段では行わない（`FunctionInlining` は登録されているが現行の呼び出し表現を認識せず実質休眠であり、詳細は[MIR最適化パスカタログ](../pipeline/mir-optimization-passes.md)を参照）。

LLVM段に渡す直前に `RecursionLimiter::preprocessModule` が再帰関数へ `NoInline`+`OptimizeNone` を付与し、さらにO3では50命令超・O2では100命令超の関数へ `NoInline` を付与する（`src/internal/codegen/llvm/optimizations/recursion_limiter.hpp:153-163`）。
その後 `buildPerModuleDefaultPipeline(OptimizationLevel::O3)` が走り、mem2regによるalloca昇格、インライン化、SROA、InstCombine、SCEV/indvarsによるループの閉形式評価、ループ展開、ベクトル化までを担う。
つまり役割分担は「MIRが定数と式を整理し、LLVMがメモリ形状（alloca/load/store）と制御構造（ループ・呼び出し）を最適化する」である。
jit（`cm run`）は既定でO3であり、同じMIR最適化を経た後にホストCPUのTargetMachineを渡したPassBuilderで同じO3パイプラインを実行する（`jit_engine.cpp:136-179`）ため、生成IRの差分構造はnativeと共通である。

## 出力IRの実差分

以下は3つの代表プログラムを `CM_DUMP_IR=1/2` と `--mir`/`--mir-opt` でダンプして得た実差分の要点抜粋である（宣言・attributes等は省略）。

### 定数演算とループ: 全体が定数に潰れる

```cm
int sum = 0;
for (int i = 0; i < 10; i++) { sum = sum + i * 2; }
int c = (3 + 4) * 5;
return sum + c;
```

O0のIR（抜粋）はループ判定だけでもalloca経由のload/storeが連鎖する。

```llvm
; O0: mainは27個のallocaで始まり、ループ判定は5命令+switch
bb1:
  %load2 = load i32, ptr %local_3, align 4
  store i32 %load2, ptr %local_5, align 4
  store i32 10, ptr %local_6, align 4
  %load3 = load i32, ptr %local_5, align 4
  %load4 = load i32, ptr %local_6, align 4
  %lt = icmp slt i32 %load3, %load4
  %bool_ext = zext i1 %lt to i8
  store i8 %bool_ext, ptr %local_7, align 1
  ...
  switch i8 %load5, label %bb3 [ i8 1, label %bb2 ]
```

O3ではMIR段が定数をオペランドへ埋め込み `(3+4)*5` を `const 35` に畳むが、ループとallocaはまだ残る（`CM_DUMP_IR=1` で確認できる中間状態）。
最終IR（`CM_DUMP_IR=2`）ではLLVMのmem2reg→InstCombine→indvars/SCEVがループを閉形式評価し、関数全体が定数リターンに潰れる。

```llvm
; O3: ループも変数も消え、答えの125だけが残る
define i32 @main(i32 %0, ptr %1) local_unnamed_addr #0 {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  ret i32 125
}
```

この畳み込み自体はO1でも成立し、レベル差は反復回数とインライン閾値・ベクトル化の積極度に現れる。

### 構造体と関数呼び出し: MIRの整理とLLVMのインライン化

```cm
struct Point { int x; int y; }
int dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
int main() { Point p = {x: 3, y: 4}; Point q = {x: 5, y: 6}; return dot(p, q); }
```

O0では `%Point` のallocaと `memset` ゼロ初期化、フィールドごとのGEP+store、構造体全体のload/storeコピー（`p = _t1000` のコピーが実体化）を経て `call @dot` に至る。
O3のMIR段（`--mir-opt`）はフィールドへ定数を直接ストアし、`p`/`q` への集約コピーを消して一時をそのまま実引数にするところまで整理するが、`dot` の呼び出しは残る（MIRインライナは休眠のため）。

```text
bb0: {
    _2.0 = const 3;
    _2.1 = const 4;
    _6.0 = const 5;
    _6.1 = const 6;
    _11 = call fn:dot(copy(_2), copy(_6)) -> bb1;
}
```

最終IRではLLVMのインライナ+SCCPが呼び出しごと定数評価し、`main` は `ret i32 39` になる。
`dot` 本体は外部可視シンボルとして残るが、SROAにより引数の構造体はextractvalueベースのレジスタ演算になり、`memory(none)` 等の属性が推論される。

```llvm
; O3: dotはSROA済みの純関数として残り、mainからの呼び出しは消える
define i32 @dot(%Point %arg0, %Point %arg1) local_unnamed_addr #0 {
entry:
  %arg0.fca.0.extract = extractvalue %Point %arg0, 0
  ...
  %add = add i32 %mul, %mul10
  ret i32 %add
}
define i32 @main(i32 %0, ptr %1) local_unnamed_addr {
entry:
  tail call void @cm_args_init(i32 %0, ptr %1)
  ret i32 39
}
```

### スライスpush: 最適化の境界はランタイム呼び出し

```cm
int[] v = [];
for (int i = 0; i < 5; i++) { v.push(i * i); }
return v[3] + v[4];
```

O0ではループ構造の中で `cm_slice_new`/`cm_slice_push_i32`/`cm_slice_get_i32` の呼び出しがalloca経由の引数準備を伴って現れる。
O3ではループが完全展開されて `i * i` が定数畳み込みされるが、スライス操作はLLVMから見て不透明な外部関数のため呼び出し自体は消えず、最適化はランタイムAPIの境界で止まる。

```llvm
; O3: ループは展開・定数化されるが、ランタイム呼び出しは残る
  %2 = tail call ptr @cm_slice_new(i64 4, i64 0)
  tail call void @cm_slice_push_i32(ptr %2, i32 0)
  tail call void @cm_slice_push_i32(ptr %2, i32 1)
  tail call void @cm_slice_push_i32(ptr %2, i32 4)
  tail call void @cm_slice_push_i32(ptr %2, i32 9)
  tail call void @cm_slice_push_i32(ptr %2, i32 16)
  %3 = tail call i32 @cm_slice_get_i32(ptr %2, i64 3)
  %4 = tail call i32 @cm_slice_get_i32(ptr %2, i64 4)
  %add15 = add i32 %4, %3
  ret i32 %add15
```

### 実行時間・コードサイズへの一般的な影響

O0のIRは命令数がO3の数倍〜数十倍になり（上記の定数ループ例では本体約100命令が2命令へ）、実行時間もload/storeの連鎖分だけ遅いが、MIR/IRとソースの対応が素直でデバッグ・変換器のバグ調査に向く。
O3は計算の定数化と呼び出し除去でホットパスを縮める一方、ループ展開やインライン化で関数あたりのコードは膨らみうるため、コードサイズ最優先のターゲット（wasm=Oz・baremetal=Os）ではレベル写像自体が上書きされる（[LLVM最適化の構成](llvm-optimization.md)参照）。
`cm run`（jit）の既定もO3であり、コンパイル時間（=起動までの時間）を縮めたい場合に `-O0`〜`-O1` を明示する使い方になる。

## 実装箇所

| 役割 | 実装箇所 |
|---|---|
| `-O<n>` のパース・範囲検証 | `src/cmd/cm/options.cpp:201-210` |
| 既定レベルO3の定義 | `src/cmd/cm/options.hpp:30` |
| `.cmconfig.yml` `compile: optimization:` の適用 | `src/cmd/cm/main.cpp:58-60`（読み込みは `src/internal/lint/config.cpp`） |
| MIR最適化の起動ゲートとO0健全性パス | `src/cmd/cm/build.cpp:479-511` |
| レベル別パス構成と反復回数 | `src/internal/mir/passes/core/manager.cpp:25-77,90-123` |
| native LLVM最適化（O0即return含む） | `src/internal/codegen/llvm/native/codegen.cpp:759-922` |
| インライン閾値のレベル別前置 | `src/internal/codegen/llvm/optimizations/recursion_limiter.hpp:153-163` |
| jit LLVM最適化（O0無変換含む） | `src/internal/codegen/llvm/jit/jit_engine.cpp:102-180` |
| IRダンプ（`CM_DUMP_IR`/`--lir-opt`） | `codegen.cpp:67-96`、`jit_engine.cpp:207-210`、`src/cmd/cm/backend/llvm.cpp:201-207` |

## 落とし穴とケア

- **`CM_DUMP_IR=1` はO0相当のIRではない**: このダンプはMIR→LLVM変換直後だが、O3指定時はMIR最適化が済んだ後のMIRから変換されている。MIR段とLLVM段の寄与を切り分けるには `--mir`/`--mir-opt` の対と `CM_DUMP_IR=1`/`2` の対を組み合わせて4点で観測する。
- **`-O0 --mir-opt` は観測がパイプラインを変える**: `--mir-opt` 指定は最適化ゲート（`build.cpp:479`）を通す側に倒すため、O0で本来ドライバが直接実行する `StringReassignFree` 健全性パスが走らなくなる。O0の真の生成過程を観測するなら `--mir` と `CM_DUMP_IR` を使う。
- **`--emit-llvm` はnative経路で未配線**: `opts.emit_llvm` はパースされるが参照箇所がなく（`options.cpp:140`）、IRの取得手段は `--lir-opt` と `CM_DUMP_IR` である。
- **インライン化の差分をMIRに帰属させない**: MIRの `FunctionInlining` は実質休眠であり、O0/O3間の呼び出し消失はすべてLLVMインライナの効果である。
- **O3のダンプに現れる `noinline` 属性**: 50命令超の関数には `RecursionLimiter` がO3で `NoInline` を付与するため、O2より大きい関数のインライン化がむしろ抑止される逆転がある。ダンプ中の見慣れない属性はパイプライン前置処理の痕跡であり、詳細は[LLVM最適化の構成](llvm-optimization.md)を参照。
- **jitの `CM_DUMP_IR` は最適化前のみ**: JIT経路は値にかかわらずLLVM最適化前のモジュールしかダンプしない。最適化後IRを見たい場合は同じレベルで `compile --lir-opt` を使う（パイプライン構成は共通）。
- **パターン検出は情報提供のみ**: `MIRPatternDetector`/`OptimizationPassLimiter` の `adjustOptimizationLevel` はクロージャ等の複雑度を分析するが暗黙のレベルダウングレードは行わず、ユーザー指定レベルが常に維持される。

## 関連資料

- [MIR最適化パスカタログ](../pipeline/mir-optimization-passes.md) — 各MIRパスの目的・アルゴリズム・実行条件の網羅カタログ
- [MIRの設計](../pipeline/mir-design.md) — 固定点反復の実行機構とO0/O1/O2の反復回数差
- [LLVM最適化の構成](llvm-optimization.md) — PassBuilderパイプライン構築・レベル写像・ICF・暴走防御の詳細
- [MIR→LLVM IR変換](mir-to-llvm.md) — O0出力の形を決める変換器の方針
- [LLJITエンジン](../codegen-jit/lljit-engine.md) — jit実行系の全体像
