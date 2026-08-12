# インラインアセンブリのlowering

Cmのインラインアセンブリは、`__asm__("...")` に単一の文字列リテラルとしてアセンブリコードとCm独自のオペランド記法 `${制約:変数名}` を書き、HIR loweringで記法を `$N` 番号へ展開、MIRでは最適化から隔離される専用の `Asm` 文として保持し、LLVMコード生成（`convertAsmStatement`）でGCC互換の制約文字列を組み立てて `llvm::InlineAsm` の呼び出しに変換する設計である。
本書は表層構文からHIR/MIRでの表現、LLVM `InlineAsm` への変換アルゴリズム（制約の並べ替え・オペランド番号の再マッピング・clobberの自動付与・レジスタ非リマップ方針）、x86_64/arm64のアーキテクチャ差、`runtime/asm.c` が提供するランタイム関数、jitでの動作までを記述する。

## 概要

対象はnative/jit（LLVMバックエンド）のみで、js/ts/wasm/svターゲットではインラインアセンブリは使用できない（svでのエラー確定は `tests/regression/cases/sv/errors/sv007_inline_asm.cm`）。
表層構文は関数呼び出しの形をした組み込み `__asm__`（後方互換の別名 `__llvm__`）で、引数はちょうど1つの文字列リテラルでなければならない（`src/internal/types/checking/call/function.cpp:27-36` が検査する）。
複数行のコードはバッククォートのraw文字列で書ける（`tests/uefi/uefi_compile/uefi_asm_scratch_reg.cm`）。
アーキテクチャごとの命令の違いはプリプロセッサの `#ifdef __x86_64__` / `#ifdef __ARM64__` で分岐する（`tests/llvm/asm/asm_basic.cm` ほかテストスイート全体がこの形をとる）。

```cm
// tests/llvm/asm/llvm_input_output.cm より
const int val = 50;
__asm__("addl $$10, ${+r:val}");   // +r: 読み書きレジスタ、$$10 は即値10
println(val);  // 60

const int orig = 42;
const int copy = 0;
__asm__("movl ${r:orig}, ${=r:copy}");  // r: 入力、=r: 出力
println(copy);  // 42
```

パイプラインは AST（`CallExpr`）→ HIR（`HirAsm`）→ MIR（`MirStatement::Asm` + `AsmData`）→ LLVM（`InlineAsm` を呼ぶ `CallInst`）と流れ、native（オブジェクト出力+リンク）とjit（LLJIT）は同一の `MIRToLLVM::convertAsmStatement` を共有する。
MIR最適化パスでのAsm文と出力オペランド変数の隔離（`no_opt` と定数・コピー追跡からの除外）は [MIR設計](../pipeline/mir-design.md) の不変条件節を参照。

## データ構造とアルゴリズム

### HIR lowering: `${制約:変数名}` の番号化

`src/internal/hir/lowering/stmt.cpp:519-625` が `__asm__` / `__llvm__` の呼び出し式を検出し、文字列リテラルを走査して `${制約:変数名}` を `$N` へ置換しながら `HirAsm::operands` に `AsmOperand{constraint, var_name}` を積む。
同じ変数名かつ同じ制約の再出現は既存のオペランド番号を再利用し、同じ変数でも制約が異なれば別オペランドとして登録する（`${r:x}` と `${=r:x}` は別番号になる）。
コロンを含まない `${NAME}` はこの段階では素通しされ、MIR loweringでconst定数の即値へ展開される。
`HirAsm::is_must` は常にtrueに設定され、生成コードが「実行される」ことを保証する側に倒している。

### MIR: `AsmData` と定数展開

MIRの文は `MirStatement::Kind::Asm` で表され、データは `src/internal/mir/nodes.hpp:298-303` の `AsmData` が持つ。

```cpp
struct AsmData {
    std::string code;                     // $N 番号化済みのasmコード
    bool is_must;                         // must修飾（最適化抑制）
    std::vector<std::string> clobbers;
    std::vector<MirAsmOperand> operands;  // 制約 + LocalId または定数値
};
```

`MirAsmOperand` は制約文字列（`"+r"`, `"=r"`, `"r"`, `"m"`, `"i"`, `"n"` 等）と、変数参照（`local_id`）か定数（`is_constant` + `const_value`）のどちらかを持つ。
`src/internal/mir/lowering/stmt/asm.cpp` のloweringは2つの解決を行う: (1) コード文字列中の `${CONST_NAME}` をconst定数テーブルから引いて `$$0x<hex>` 形式の即値リテラルへ展開する（`tests/llvm/asm/asm_const_expand.cm` が回帰）、(2) `i`/`n` 制約のオペランドはmacro/const定数を優先解決して `const_value` 化し、それ以外は変数名を `LocalId` へ解決する。

```cm
// tests/llvm/asm/asm_immediate.cm より: i制約でmacro定数を即値として埋め込む
macro int CONST_42 = 42;
const int result1 = 0;
__asm__("movl ${i:CONST_42}, ${=r:result1}");  // result1 == 42
```

`clobbers` フィールドはIR上の拡張点として存在するが、表層構文からの明示指定経路はなく、後述のコード生成段の自動付与が安全性を担う。

### LLVM変換: `convertAsmStatement`

`src/internal/codegen/llvm/core/statement/asm.cpp` の `convertAsmStatement` が `AsmData` を `llvm::InlineAsm::get(funcTy, code, constraints, hasSideEffects)` への `CreateCall` に変換する。
オペランドなしの場合は制約を `"~{memory},~{dirflag},~{fpsr},~{flags}"` 固定・`hasSideEffects=true` で発行し、`is_must` なら空asmのコンパイラバリアを前後に挿入して、`must { __asm__("hlt"); }` のような文の後続をLLVMが到達不能と誤判定してループごと消す最適化を阻止する（ハードウェアfenceでなくコンパイラバリアなのは、UEFIベアメタルで `mfence` がGPFを起こしうるため）。

オペランドありの場合のアルゴリズムは以下の順で進む。

1. **オペランド分類**: 制約の先頭文字で出力（`=`/`+`）と純粋入力に分け、`m` を含むものはメモリ制約として区別する。`+r` はtied入力として入力列にも積む。値はallocaから `volatile load` で読み、`m` 系はポインタそのものを渡す。
2. **入力列の結合順序**: 純粋入力 → tied入力 → メモリ出力ポインタの順で `CreateCall` の引数列を作り、制約文字列側も同じ順序で組み立てて対応を保つ。
3. **制約文字列のLLVM形式化**: 出力制約（`=r`、`+r` は `=r` に正規化）を先頭に置き、続けて入力制約を並べる。`+r` の入力側は出力番号を参照するtied制約（`"0"` 等）に、`m` 系は間接メモリ `"*m"` に変換する。`=m`/`+m` はLLVMでは出力でなく「ポインタを渡す入力」なので出力制約から除外して入力末尾に回す。
4. **オペランド番号の再マッピング**: 出力先行の並べ替えで `$N` の意味が変わるため、`$N → __TMP_N__ → $REMAP[N]` の2段階置換で番号を振り直す。1段階置換だと `$0→$1` と `$1→$0` の交差で衝突するための設計であり、`$$N`（即値エスケープ）はスキップする。
5. **clobberの付与**: `is_must` なら `~{memory}` を自動追加し、さらにasmコード中の `%rax` 等のハードコードレジスタ表記をx86レジスタ名テーブル（8/16/32/64bit別名を64bit名へ正規化）で検出して `~{rax}` 形式のclobberを自動追加する。これはLLVMのインライン展開時にスクラッチ利用中のレジスタ値が再利用される破壊を防ぐ。入出力オペランドと重複するclobberはLLVM側が無視するため除外処理は不要である。
6. **結果の書き戻し**: 出力が1個なら戻り値をそのまま、複数なら構造体戻り値から `extractvalue` で取り出し、各出力変数へ `volatile store` する。書き戻した変数は `allocatedLocals` に登録され、後続の読み取りが `volatile load` になることでLLVMのキャッシュを防ぐ。`*m` 制約の引数には `elementtype` 属性を付与する（LLVMの間接メモリ制約の要件）。

### レジスタ非リマップ方針

`convertAsmStatement` はasmコード中のレジスタ名を一切書き換えない。
`%rdi`/`%rsi` 等はSystem V ABIの引数レジスタであると同時に汎用スクラッチレジスタとしても使われるため、ターゲットABI（例: UEFIのMicrosoft x64）に合わせた一律の自動リマップは、スクラッチ用途のコードを壊してカーネルクラッシュを引き起こす。
ABI引数を参照したい場合はレジスタ名を直書きせず `${r:引数名}` でLLVMのレジスタアロケータに割り当てを委ねるのが正しい書き方であり、この方針の回帰テストが `tests/uefi/uefi_compile/uefi_asm_scratch_reg.cm` である。

```cm
// %rdiをABI引数でなくスクラッチとして使う（リマップされないことが前提）
export ulong read_byte(ulong addr) {
    const ulong result = 0;
    __asm__(`
        movq ${r:addr}, %rdi;
        xorq %rax, %rax;
        movb (%rdi), %al;
        movq %rax, ${=r:result}
    `);
    return result;
}
```

### アーキテクチャ差（x86_64 / arm64）

- asmコードはターゲットのGAS構文をそのまま書く: x86_64はAT&T構文でレジスタに `%`・即値に `$$`（LLVMの `$` エスケープ）、arm64はレジスタ名を素で書き即値は `#` を使う（`addl $$10, ${+r:x}` vs `add ${+r:x}, ${+r:x}, #10`）。
- AArch64ターゲットでは、32bit以下の整数型のレジスタオペランドに `:w` 修飾子（`${N:w}`）を自動付与し、LLVMが64bitのxレジスタでなくwレジスタを割り当てるよう強制する。x86_64ではこの処理は行わない。
- ハードコードレジスタの自動clobber検出テーブルはx86系レジスタ名のみを対象とし、arm64で `w1` 等を直書きした場合の自動保護はない（arm64ではオペランド記法での記述が前提）。
- `${CONST_NAME}` 展開は `$$0x<hex>` 形式を生成するため、x86_64（およびUEFI）向けの即値記法である。
- ターゲット判定は `module->getTargetTriple()` に `aarch64`/`arm64` が含まれるかで行い、native/jit/クロスコンパイルのいずれでも同じ分岐が働く。

## 実装箇所

| 役割 | ファイル |
|---|---|
| `__asm__` 引数の型検査・オペランド変数の使用マーク | `src/internal/types/checking/call/function.cpp:24-60` |
| HIR文ノード（`HirAsm`: code/is_must/clobbers/operands） | `src/internal/hir/nodes.hpp:334-338` |
| `${制約:変数}` → `$N` 展開とオペランド重複排除 | `src/internal/hir/lowering/stmt.cpp:519-625` |
| MIR文表現（`AsmData`, `MirAsmOperand`, ファクトリ `asm_stmt`） | `src/internal/mir/nodes.hpp:280-318`, `nodes.cpp:207-217` |
| MIR lowering（`${CONST}` 即値展開・i/n制約の定数解決・LocalId解決） | `src/internal/mir/lowering/stmt/asm.cpp` |
| Asm文を保持したまま複製するパス（インライン化・ループ展開・単相化） | `src/internal/mir/passes/interprocedural/inlining.cpp:250`, `passes/loop/const_unroll.cpp:88`, `lowering/monomorphization_utils.cpp:107` |
| LLVM `InlineAsm` 変換本体 | `src/internal/codegen/llvm/core/statement/asm.cpp` |
| 変換ディスパッチ（`MirStatement::Asm` ケース） | `src/internal/codegen/llvm/core/statement.cpp:59` |
| ランタイム補助関数（`cm_asm_*`） | `src/internal/codegen/llvm/native/runtime/asm.c`（`runtime.c` 経由で `cm_runtime.o` に含まれる） |
| jit実行エンジン（同一IRをLLJITで実行、AsmParser初期化） | `src/internal/codegen/llvm/jit/jit_engine.cpp:41-42,198` |
| MIRダンプでのAsm文表示 | `src/internal/mir/printer.cpp:290-320` |

### runtime/asm.c が提供するもの

`runtime/asm.c` はコンパイラが生成する `InlineAsm` とは独立の、C側で `__asm__ volatile` を使って実装されたランタイム関数群である。
`nop`/`pause`(`yield`)/`mfence`(`dmb sy`)/`sfence`/`lfence` に対応する `cm_asm_nop`・`cm_asm_pause`・`cm_asm_barrier`・`cm_asm_store_barrier`・`cm_asm_load_barrier`、算術検証用の `cm_asm_add`・`cm_asm_mul`、タイムスタンプの `cm_asm_rdtsc_low`（arm64では `cntvct_el0` を読む）、アーキテクチャ検出の `cm_asm_is_x86`・`cm_asm_is_arm64`、ポインタ入出力の `cm_asm_ptr`/`cm_asm_val`/`cm_asm_inout_*` 系を含む。
各関数はx86_64/arm64それぞれのアセンブリ実装と、その他アーキテクチャ向けのC実装フォールバックを持つため、どの環境でもリンク可能である。
これらは `extern "C"` 宣言によるFFIで呼び出す通常のランタイムシンボルであり、nativeでは `cm_runtime.o` のリンクで、jitではホストプロセス（cm本体にリンク済み）のシンボル解決で束縛される（解決経路は [FFIとextern宣言のlowering](ffi-extern.md) と同一）。

### jitでの動作

jit（`cm run`）はnativeと同一の `MIRToLLVM` でIRを生成するため、インラインアセンブリの変換経路・制約生成・リマップ処理に差はなく、機能制限もない。
LLJITがホストトリプル向けに機械語化する際にasm文字列をアセンブルするため、`jit_engine.cpp` の初期化で `InitializeNativeTargetAsmParser()` を呼んでいることが動作の前提である（エンジン全体は [LLJITエンジン](../codegen-jit/lljit-engine.md) を参照）。
ターゲットは常にホストなので、`#ifdef __x86_64__` / `#ifdef __ARM64__` の分岐は実行マシンのアーキテクチャで選択される。

## 落とし穴とケア

- **番号再マッピングの交差**: 制約並べ替え後の `$N` 置換を1段階で行うと `$0→$1` と `$1→$0` が衝突して誤ったオペランドを参照する。2段階置換（一時プレースホルダー経由）と `$$N` スキップを維持すること。
- **制約順序と引数順序の一致**: LLVMの制約文字列（出力→tied→入力→間接メモリ→clobber）と `CreateCall` の引数列（純粋入力→tied入力→メモリ出力ポインタ）の対応がずれると、コンパイルは通るのに別の変数へ読み書きするサイレント破壊になる。順序を変える変更は `tests/llvm/asm/llvm_constraints.cm`・`llvm_input_output.cm`・`llvm_plusm.cm`・`llvm_m_input.cm` で必ず検証する。
- **volatileの一貫性**: asmの入力ロード・出力ストアと、出力変数の後続読み取り（`allocatedLocals` 経由）はすべてvolatileでなければ、LLVMがasmを跨いで値をキャッシュし古い値を読む（回帰: `tests/llvm/asm/asm_volatile_opt.cm`、ループ不変扱いの誤りは `asm_licm_regression.cm`）。
- **自動リマップの禁止**: レジスタ名の自動書き換えを再導入すると、スクラッチ用途の `%rdi` 等を使うベアメタルコードが壊れる。ABI参照はオペランド記法で書かせる方針を崩さないこと（回帰: `tests/uefi/uefi_compile/uefi_asm_scratch_reg.cm`）。
- **制御フロー最適化との衝突**: `hlt` のような復帰しない命令を含むasmの後続コードをLLVMが削除しないよう、無オペランドasmの `hasSideEffects=true`・フラグclobber・`is_must` 時のコンパイラバリアを維持する（回帰: `tests/uefi/uefi_compile/uefi_bug7_must_hlt.cm`、`uefi_bug7_compiler_barrier.cm`）。
- **`*m` 制約の `elementtype`**: 間接メモリ制約の引数に `elementtype` 属性を付け忘れるとLLVMのIR検証で落ちる。メモリ制約の追加箇所（純粋入力・tied入力・メモリ出力の3系統）すべてで属性付与が必要である。
- **MIRパスでの隔離**: 新しい最適化パスはAsm文の出力オペランド変数を値追跡から除外し、文複製時に `AsmData` 全体と `no_opt` を引き継ぐこと（詳細は [MIR設計](../pipeline/mir-design.md)）。
- **回帰テストの場所**: 統合テストは `tests/llvm/asm/`（`make test-llvm`、jit側は `cm run` 経由の共通スイート）、ベアメタルは `tests/uefi/uefi_compile/`、非対応ターゲットのエラー確定は `tests/regression/cases/sv/errors/sv007_inline_asm.cm` にある。

## 関連資料

- [MIR設計](../pipeline/mir-design.md) — Asm文の `no_opt` 不変条件と最適化パスからの隔離
- [FFIとextern宣言のlowering](ffi-extern.md) — `cm_asm_*` を含むランタイムシンボルのnative/jit解決経路
- [LLJITエンジン](../codegen-jit/lljit-engine.md) — jit実行の初期化とシンボル解決
- [コンパイルパイプライン全体像](../pipeline/overview.md)
- チュートリアル: `docs/tutorials/ja/advanced/inline-asm.md`
