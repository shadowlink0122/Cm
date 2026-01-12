[English](LLVM_OPTIMIZATION.en.html)

# LLVM 最適化パイプライン

## 概要

Cm言語のLLVMバックエンドは、多段階の最適化パイプラインを使用して効率的なコードを生成します。

## 最適化レベル

| レベル | フラグ | 説明 |
|--------|--------|------|
| O0 | `-O0` | 最適化なし（デバッグ向け） |
| O1 | `-O1` | 基本最適化 |
| O2 | `-O2` | 標準最適化（デフォルト） |
| O3 | `-O3` | アグレッシブ最適化 |
| Oz | `-Os` | サイズ最適化（WASM/ベアメタル向け） |

## 最適化パス

### LLVMレベルの最適化

LLVMの`PassBuilder`を使用した標準的な最適化パイプラインが適用されます：

1. **定数畳み込み (Constant Folding)**
   - コンパイル時に計算可能な式を評価
   - 例: `10 + 20` → `30`

2. **デッドコード削除 (Dead Code Elimination)**
   - 未使用の変数、関数宣言を削除
   - 到達不能なコードブロックを削除

3. **メモリ最適化 (Memory Optimization)**
   - 不要な`alloca`の削除
   - `load`/`store`の最適化（mem2reg）
   - スタックからレジスタへの昇格

4. **関数最適化 (Function Optimization)**
   - インライン展開
   - Tail call最適化
   - 関数の属性（`local_unnamed_addr`など）の追加

5. **ループ最適化 (Loop Optimization)**
   - ループ不変コードの移動
   - ループアンローリング
   - ループベクトル化（O3のみ）

### MIRレベルの最適化

MIR（中間表現）レベルでも以下の最適化が適用されます：

1. **定数畳み込み** - 定数式の事前評価
2. **デッドコード削除** - 未使用変数の削除
3. **コピー伝播** - 冗長なコピーの削除

## 最適化結果の例

### 入力コード

```cm
int main() {
    int x = 10;
    int y = 20;
    int z = x + y;
    println("Result: {}", z);
    return 0;
}
```

### 最適化前LLVM IR

```llvm
define i32 @main() {
entry:
  %local_1 = alloca i32, align 4
  %local_2 = alloca i32, align 4
  ; ... 8つのalloca ...
  store i32 10, i32* %local_2, align 4
  %load = load i32, i32* %local_2, align 4
  store i32 %load, i32* %local_1, align 4
  ; ... 多数のload/store ...
  %add = add i32 %load2, %load3
  ; ...
}
```

### 最適化後LLVM IR

```llvm
define i32 @main() local_unnamed_addr {
entry:
  %0 = tail call i8* @cm_format_replace_int(i8* ..., i32 30)
  tail call void @cm_println_string(i8* %0)
  ret i32 0
}
```

**最適化効果:**
- allocaが8個 → 0個に削減
- `10 + 20` → `30`に定数畳み込み
- load/store → 直接値使用
- 関数呼び出し → tail call最適化

## 遅延宣言（オンデマンド宣言）

効率的なコード生成のため、組み込み関数（intrinsic）は使用時にのみ宣言されます：

**改善前:**
- sqrt, sin, cos, tan, log, exp, pow, abs, bswap, ctpop, ctlz, cttzなど30以上の宣言

**改善後:**
- 実際に使用される関数のみ宣言
- 最適化パスにより未使用宣言は自動削除

## ターゲット別最適化

### ネイティブ (x86_64, ARM64)
- 指定された最適化レベル（デフォルトO2）を使用
- CPU固有の最適化

### WebAssembly
- サイズ優先最適化（Oz）
- WASM固有の制約を考慮

### ベアメタル
- サイズ優先最適化（Os）
- スタートアップコード生成

## 設定

### コマンドライン

```bash
# 最適化レベル指定
cm compile program.cm --emit-llvm -O2

# 最適化なし（デバッグ向け）
cm compile program.cm --emit-llvm -O0

# verbose出力で最適化前後のIRを確認
cm compile program.cm --emit-llvm -v
```

### 実装場所

- **最適化パイプライン**: `src/codegen/llvm/llvm_codegen.hpp` (`optimize()`)
- **MIR最適化**: `src/mir/optimizations/`
  - `constant_folding.hpp` - 定数畳み込み
  - `dead_code_elimination.hpp` - デッドコード削除
  - `copy_propagation.hpp` - コピー伝播

## 将来の改善予定

1. **MIRレベルの定数伝播強化**
   - 変数間の定数伝播
   - より積極的なデッドコード削除

2. **プロファイルガイド最適化（PGO）**
   - 実行プロファイルに基づく最適化

3. **リンク時最適化（LTO）**
   - モジュール間の最適化

## 関連ドキュメント

- [LLVMバックエンド実装](./llvm_backend_implementation.html)
- [LLVMランタイムライブラリ](./LLVM_RUNTIME_LIBRARY.html)
- [MIR設計](./design/mir.html)