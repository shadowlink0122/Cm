[English](optimization_methods.en.html)

# Cm向け最適化手法の調査メモ

## 目的
- Cmの言語特性（interface, generic, slice, string interpolation, FFI）を踏まえ、
  どの最適化が有益かを整理する。
- native/wasmの両方に効く手法と、実装レイヤー（HIR/MIR/LLVM）を区別する。

## 現在の最適化（実装済み）
### MIRレベル
- SparseConditionalConstantPropagation（CFG対応）
- CopyPropagation（局所）
- ConstantFolding（局所）
- DeadCodeElimination（関数単位）
- SimplifyControlFlow（単純なブロック統合）
- ProgramDeadCodeElimination（プログラム単位 / compileのみ）

補足:
- MIR最適化はプラットフォーム共通で固定レベル（現在は2）。

### LLVMレベル
- `PassBuilder` のデフォルトパイプライン（`-O1/-O2/-O3/-Oz`）

## Cmに有益な最適化（優先度順）
### 1) デバーチャライゼーション（interface呼び出しの静的化）
**効果**: vtable経由の呼び出し削減、インライン化しやすくなる。  
**対象**: interface経由メソッド呼び出し、関数ポインタ呼び出し。  
**実装候補**:
- MIRで `is_virtual` だが型が単一に確定するケースを静的呼び出しに置換。
- monomorphization後に型が確定している箇所に限定すると安全。

### 2) インライン化（小関数/組み込み）
**効果**: 呼び出しオーバーヘッド削減 + さらなるDCE/定数畳み込みが可能。  
**対象**: 小さな関数、slice/stringのラッパー、デフォルトコンストラクタ。  
**実装候補**:
- MIRで簡易インライン化（サイズ閾値）。
- LLVMに任せる前にMIRで行うとProgramDCEと相性が良い。

### 3) SCCP / GVN / CSE
**効果**: 定数伝播の拡張と重複計算削減。  
**対象**: 分岐条件が定数化されるケース、`len()`/`cap()` の多重呼び出し。  
**実装候補**:
- MIRでSCCP（Sparse Conditional Constant Propagation）は導入済み。
- GVN/CSEで同一式の再利用。

### 4) Bounds Check Elimination（配列/スライス）
**効果**: ループ内の境界チェック削減、wasmにも有効。  
**対象**: `for` / `while` で連続アクセスする配列/スライス。  
**実装候補**:
- ループ解析（iの範囲を追跡）と `len()` の不変式化。
- MIRで `len()` をループ外にhoist。

### 5) Escape Analysis + SROA
**効果**: ヒープ確保削減、構造体コピーの縮小。  
**対象**: 一時構造体、クロージャ、短命なslice操作。  
**実装候補**:
- 逃げない構造体/配列をstackに限定。
- SROAで構造体フィールドを個別のSSA値に分解。

### 6) ループ最適化（LICM / unroll）
**効果**: ループ内の不変式を外へ、反復回数削減。  
**対象**: 数値計算/配列処理。  
**実装候補**:
- MIRで簡易LICM。
- LLVM側のLoopPassに委譲する場合は属性付与を強化。

### 7) 文字列フォーマットの簡約
**効果**: `cm_format_*` 呼び出し数の削減。  
**対象**: 静的なフォーマット文字列、単純な補間。  
**実装候補**:
- MIRで `cm_format_unescape_braces` + `cm_format_replace_*` の合成。
- 文字列定数のみの場合は `cm_println_string` に直接置換。

### 8) Tail Call Optimization
**効果**: 再帰のスタック削減、wasmでも有効。  
**対象**: 末尾再帰関数。  
**実装候補**:
- MIRで末尾呼び出し判定し `return call` に変換。
- LLVMに `musttail` を付与できる形を検討。

### 9) 属性付与（LLVM最適化支援）
**効果**: LLVM側でのDCE/CSE/LICMが効きやすくなる。  
**対象**: ランタイム関数やbuiltin呼び出し。  
**実装候補**:
- `readonly`, `readnone`, `nocapture`, `nounwind`, `noalias` などを付加。
- `cm_slice_len/cap` は `readonly` に近い性質を持つ。

## wasm特有の観点
- `call_indirect` の削減（デバーチャライゼーションが重要）。
- `i64` のコストが高い場合があるので、`i32` で足りる場面は縮小。
- `memcpy`/`memmove` 生成を抑えるためにSROAとインライン化を優先。

## native特有の観点
- `memcpy` 化されやすい構造体コピーの削減（SROA/escape）。
- `noalias`/`restrict` 相当の情報が付与できるとLLVMが最適化しやすい。

## 推奨ロードマップ（短期）
1) MIR GVN/CSE
2) 簡易インライン化（小関数/組み込み）
3) interface呼び出しの静的化
4) bounds check elimination（配列/スライス）
5) 文字列フォーマットの簡約