# MIR→LLVM IR変換の構造

MIRToLLVMクラスはMIR（中位中間表現）をLLVM IRへ変換する唯一の入口であり、プログラム全体変換とモジュール分割変換の2系統のエントリポイントを持ち、関数ID生成（オーバーロードマングリング）・型マッピング・エントリポイント特別扱い・sret変換までを一貫して担当する。

## 概要

変換器の本体は `src/internal/codegen/llvm/core/mir_to_llvm.hpp:18` の `MIRToLLVM` クラスで、`LLVMContext`（`src/internal/codegen/llvm/core/context.hpp:94`）からModule/IRBuilderを借用し、ローカル変数・基本ブロック・関数・構造体型・enum型・インターフェースfat pointer・vtableのマッピングテーブルを保持する（`mir_to_llvm.hpp:29-79`）。

実装は責務ごとにファイル分割されている。

- `translate/program.cpp` — プログラム全体/モジュール単位の変換エントリ（型登録・関数宣言・DFE・関数変換の順序制御）
- `translate/signature.cpp` — 関数ID生成と関数シグネチャ変換（マングリング・sret・呼出規約）
- `translate/function.cpp` — 関数本体（alloca配置・到達可能ブロック解析・基本ブロック変換）
- `statement.cpp` → `statement/assign.cpp`・`statement/asm.cpp` — 文の変換
- `terminator.cpp` → `terminator/call.cpp`・`terminator/dispatch.cpp`・`terminator/invoke.cpp` — 終端命令の変換
- `operand.cpp`・`rvalue.cpp` — オペランド/右辺値の変換とPlaceのアドレス計算
- `types.cpp` — HIR型→LLVM型マッピングと定数変換

## データ構造とアルゴリズム

### プログラム変換の順序（translate/program.cpp）

`convert(const mir::MirProgram&)` は `src/internal/codegen/llvm/core/translate/program.cpp:25` で、次の順に処理する。

1. アドレス取得された関数の収集（sret変換の除外判定用、`program.cpp:34-35`）とtypedef定義マップの取り込み（`program.cpp:38`）
2. ターゲット判定のキャッシュ（tripleに`wasm`/`windows`を含むかで `isWasmTarget`/`isUefiTarget` を設定、`program.cpp:41-43`）
3. 構造体型の2パス登録（パス1で全構造体をopaque型として作成し、パス2でフィールド型をsetBodyする。相互参照構造体を成立させるため、`program.cpp:64-90`）
4. インポートモジュール由来で定義が欠落したstruct型を、メソッド関数（`StructName__method`）本体のフィールドプロジェクションから推論して登録（`program.cpp:92-213`）
5. enum登録（タグ付きユニオンは `{i32 tag, [N x i8] payload}` 構造体を生成、`program.cpp:222-240`）とインターフェースfat pointer型・グローバル変数の生成
6. 全関数のシグネチャ宣言（`generateFunctionId` で重複排除、`program.cpp:315-326`）とvtable生成
7. Dead Function Elimination: main・export・extern・`__lambda_*`・`#[test]` 関数とvtableエントリを起点にBFSで到達可能関数を求め、到達不能関数は宣言のみ残して本体変換をスキップする（`program.cpp:332-483`）

モジュール分割変換 `convert(const mir::ModuleProgram&)` は `program.cpp:498` で、自モジュール関数を完全変換し、extern関数は宣言のみを生成する。このとき他モジュール関数も正しいMIRシグネチャで事前宣言しておく（`program.cpp:710-725`）。宣言収集が漏れると呼び出し時の `declareExternalFunction` フォールバックが `ptr` 等の誤ったシグネチャを推測し、構造体値渡しABIが崩れるためである。本体付きのimport先export関数は `linkonce_odr` で定義し、モジュール間の重複定義をリンク時にマージする（`program.cpp:744-758`）。

### 関数ID生成とextern/cm_*/ラムダの扱い（translate/signature.cpp）

Cmは引数型オーバーロードを許すため、LLVMシンボル名は `generateFunctionId`（`src/internal/codegen/llvm/core/translate/signature.cpp:25`）で引数型サフィックスを付けてユニーク化する。ただし次の4カテゴリはマングリングせず名前をそのまま使う。

```cpp
// translate/signature.cpp:27-44
if (func.name == "main" || func.name == "efi_main") {
    return func.name;
}
if (func.name.find("__lambda_") == 0) {
    return func.name;
}
if (func.name.find("cm_") == 0) {
    return func.name;
}
if (func.is_extern) {
    return func.name;
}
```

サフィックスは引数型kindごとの短縮名（`i`/`u`/`i64`/`d`/`s`/`p`/`S構造体名` 等）をアンダースコアで連結する（`signature.cpp:52-118`）。typedefは `resolveTypeAlias`（`mir_to_llvm.cpp:28-54`）で基底型へ解決してからマングリングするため、`typedef MemAddr = ulong` を挟んでも同一IDになる。呼び出し側は `generateCallFunctionId`（`signature.cpp:122-241`）が同じ規則でIDを構成し、見つからない場合はベース名→インターフェース型サフィックスのパターンマッチへフォールバックする。

`convertFunctionSignature`（`signature.cpp:243`）でのシグネチャ確定には次の規則がある。

- `cm_*` ランタイム関数は既存宣言を再利用し、なければ `declareExternalFunction` に委譲する（`signature.cpp:245-252`）
- インターフェース型引数はfat pointer構造体 `{ptr data, ptr vtable}` の値渡し（`signature.cpp:262-266`）
- 構造体引数はDataLayout実測16バイト以下なら値渡し、超なら `ptr` 渡し（`signature.cpp:269-279`、判定は `isSmallStruct` `mir_to_llvm.cpp:58-77`）
- 16バイト超の構造体戻り値は非extern・非main・アドレス未取得の関数に限りsret（先頭の隠し出力ポインタ+`StructRet`/`NoAlias`属性）へ変換する（`signature.cpp:293-299`・`337-343`、判定は `needsSretReturn` `mir_to_llvm.cpp:83-109`）
- ASM文を含む関数は `NoInline`、ret/iretのみの純ASM関数は `Naked` を付与する（`signature.cpp:353-410`）

### エントリポイントの特別扱い

`main` はC標準準拠で常に `i32` を返す。シグネチャ（`signature.cpp:295-297`）・retval allocaのi32強制と0初期化（`translate/function.cpp:588-595`）・Return終端でのi32ロード（`terminator.cpp:54-70`）の3箇所で一貫させる。`noMain` ターゲットでは `setupEntryPoint` がmainを `_start` にリネームする（`context.cpp:254-266`）。UEFIターゲットでは全関数にWin64呼出規約と `NoInline`・スタックプローブ無効化を適用し、`efi_main` にはさらに `DLLExport` と `OptimizeNone` を与えて最適化除去を防ぐ（`signature.cpp:415-429`）。

### 型マッピング（types.cpp）

`convertType`（`src/internal/codegen/llvm/core/types.cpp:16`）はまずtypedefを解決してからkindでディスパッチする。

| HIR型 | LLVM型 | 備考 |
|---|---|---|
| bool / tiny / utiny / char | i8 | boolはメモリ上i8、演算時のみi1（`types.cpp:32-38`） |
| short / ushort | i16 | |
| int / uint | i32 | |
| long / ulong / isize / usize | i64 | ポインタサイズ整数は64ビット想定（`types.cpp:45-51`） |
| float / double | float / double | |
| string / cstring / Pointer / Reference | ptr | opaque pointer、element_typeはHIR側で保持（`types.cpp:58-72`） |
| インターフェースへのポインタ | fat pointer構造体 | `{ptr, ptr}` 値として表現（`types.cpp:63-67`） |
| 固定長配列 | `[N x elem]` | 多次元はネスト構造を保持しGEP複数インデックスとベクトル化を効かせる（`types.cpp:79-83`） |
| スライス（サイズ未定配列） | ptr | ランタイムのCmSliceハンドル（`types.cpp:74-77`） |
| 構造体 | 名前付きStructType | `Vector<int>` → `Vector__int` へジェネリック名を正規化して検索（`types.cpp:98-231`） |

### 関数本体・文・終端の変換

`convertFunction`（`translate/function.cpp:25`）はローカルごとにallocaを配置し、スライス型は `cm_slice_new` 呼び出しで初期化（`function.cpp:392-436`）、構造体・ユニオン・固定長配列のallocaはmemsetでゼロ初期化して全バックエンドの未初期化挙動を統一する（`function.cpp:496-517`）。その後エントリブロックからの到達可能性をBFSで解析し、到達可能ブロックのみをLLVM基本ブロック化する（到達不能ブロックがO3で `unreachable`→`ud2` に化けてSIGILLになるのを防ぐ、`function.cpp:612-691`）。

文の変換は `convertStatement`（`statement.cpp:24`）がAssign/Asmを `statement/assign.cpp`・`statement/asm.cpp` へ委譲し、終端は `convertTerminator`（`terminator.cpp:13`）がGoto/SwitchInt/Return/Unreachable/Callを処理する。sret関数のReturnはretval allocaの内容を第1引数の呼び出し元バッファへmemcpyして `ret void` する（`terminator.cpp:38-52`）。Call終端は `terminator/call.cpp` を入口に、インターフェース/プリミティブimplのメソッドディスパッチを `terminator/dispatch.cpp`、通常の直接/間接呼び出しと高階クロージャの環境化を `terminator/invoke.cpp` が担当する（宣言は `mir_to_llvm.hpp:128-144`）。

未解決の関数名は `declareExternalFunction`（`utils.cpp:14`）が、functionsテーブル完全一致→ベース名前方一致（一意な場合のみ）→組み込みランタイム宣言（`runtime/builtins.cpp:13`）→システムランタイム宣言（`runtime/system.cpp:13`）→MIRプログラム/allModuleFunctionsからのシグネチャ復元、の順で解決する。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/mir_to_llvm.hpp` | MIRToLLVMクラス定義（状態テーブル・メソッド宣言の全体像） |
| `src/internal/codegen/llvm/core/mir_to_llvm.cpp` | typedef解決・小型構造体判定・sret判定・アドレス取得関数の収集 |
| `src/internal/codegen/llvm/core/translate/program.cpp` | プログラム/モジュール変換エントリ、型登録2パス、DFE |
| `src/internal/codegen/llvm/core/translate/signature.cpp` | 関数ID生成（マングリング）とシグネチャ変換、属性付与 |
| `src/internal/codegen/llvm/core/translate/function.cpp` | 関数本体変換（alloca・ゼロ初期化・到達可能ブロック解析） |
| `src/internal/codegen/llvm/core/statement.cpp` / `statement/` | 文のディスパッチとAssign/Asm変換 |
| `src/internal/codegen/llvm/core/terminator.cpp` / `terminator/` | 終端のディスパッチとCall変換（dispatch/invoke分離） |
| `src/internal/codegen/llvm/core/operand.cpp` | オペランド変換（`convertOperand`:24）とPlaceアドレス計算（`convertPlaceToAddress`:520） |
| `src/internal/codegen/llvm/core/rvalue.cpp` | 右辺値変換（Use/Ref/Cast/BinaryOp/Aggregate） |
| `src/internal/codegen/llvm/core/types.cpp` | 型マッピング・ジェネリック名正規化・定数変換（`convertConstant`:689） |
| `src/internal/codegen/llvm/core/utils.cpp` | 外部関数宣言の入口・パニック生成・オペランド型取得 |
| `src/internal/codegen/llvm/core/context.cpp` / `context.hpp` | LLVMContext（Module/IRBuilder/基本型キャッシュ・std/no_stdセットアップ） |

## 落とし穴とケア

- 構造体引数の大小判定は必ずDataLayout実測で行う（`mir_to_llvm.cpp:58-77`）。フィールド数ベースの手計算は `int[16384]` のような大配列フィールドを8バイトと見積もって値渡しにしてしまい、SROA全展開でO2/Ozのコンパイル時間が爆発するバグのクラスを生む。
- sret変換の対象からextern関数（FFIのABI互換）とアドレス取得された関数（間接呼び出しのシグネチャ不一致）を除外する不変条件を維持すること（`mir_to_llvm.cpp:83-109`）。除外判定はプログラム全体から計算し、モジュール分割時も全モジュールで同一判定にする（`program.cpp:511-514`）。
- モジュール分割時のextern宣言はMIR由来の正しいシグネチャで事前登録する（`program.cpp:710-725`）。漏れると `declareExternalFunction` のフォールバックが誤推測し、構造体値渡しの呼び出しが壊れる。
- `main` のi32はシグネチャ・alloca・Returnの3箇所で強制しており、片側だけ変更すると型不一致のIRになる。
- 到達不能ブロックをLLVMへ渡さないこと（`function.cpp:612-691`）。渡すとO3が `unreachable` を実コードパスへ伝播させ、x86_64で `ud2`（SIGILL）が実行される。
- 回帰テスト: バックエンドスイート（`make test-llvm` 等、`tests/common/` 配下を unified_test_runner.sh で実行）が変換全体の回帰を担い、プロセス内のコード生成回帰は `tests/regression/` のgtestが担う。

## 関連資料

- [オブジェクトファイル出力](object-emission.md)
- [リンクとランタイム解決](linking-and-runtime.md)
- [数値出力とキャストの一貫性](numeric-and-casts.md)
- 大構造体のsret化・ポインタ渡し統一の経緯: [../../archive/v0.17.0/aggregate-copy-lowering.md](../../archive/v0.17.0/aggregate-copy-lowering.md)
- 大規模監査での背景所見: [../../archive/v0.17.0/large-scale-bottleneck-audit.md](../../archive/v0.17.0/large-scale-bottleneck-audit.md)
