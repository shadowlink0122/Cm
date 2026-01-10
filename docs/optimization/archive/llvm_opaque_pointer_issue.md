[English](llvm_opaque_pointer_issue.en.html)

# LLVM Opaque Pointer Compatibility Issue

## 問題の概要

LLVM 14以降の`opaque pointer`への移行により、MIR最適化後のLLVMコード生成で型の不整合が発生し、O1以上の最適化レベルでコンパイラがクラッシュする。

## 現状 (2026-01-02)

### 修正済み
- ✅ MIR最適化の無限ループ問題
  - `OptimizationPipeline`の直接使用を`run_optimization_passes`関数に置き換え
  - 適切な反復回数制限を設定（O1=3, O2=10, O3=20）

### 未解決
- ❌ LLVM CreateLoad opaque pointer assertion失敗（O1-O3）
- ❌ Exit code 137（メモリ/無限ループ問題）の一部

## エラー詳細

```
Assertion failed: (cast<PointerType>(Ptr->getType()->getScalarType())
  ->isOpaqueOrPointeeTypeMatches(PointeeType)),
  function Create, file Instructions.h, line 963.
```

### 発生条件
- 最適化レベル: O1, O2, O3
- 最適化レベル: O0では正常動作
- MIR最適化パス実行後のLLVMコード生成時

### 影響を受けるCreateLoad呼び出し箇所
- `/src/codegen/llvm/core/mir_to_llvm.cpp`
  - Line 768: Load命令の生成
  - Line 892: Deref操作
  - Line 1157: GetElementPtr後のLoad
  - Line 1176: 配列要素アクセス
  - Line 1183: スライス要素アクセス
  - Line 1337: 関数呼び出し引数のLoad
  - Line 1407: Return文でのLoad

## 根本原因

MIR最適化パス（特に以下のパス）が型情報を変更する際に、LLVM IRレベルでの型の一貫性が失われる：
- Constant Folding
- Dead Code Elimination
- Copy Propagation
- SCCP (Sparse Conditional Constant Propagation)

## 一時的な対処

`src/main.cpp:66`でデフォルト最適化レベルをO0に設定：
```cpp
int optimization_level = 0;  // TODO: O1-O3でインポート時に無限ループ問題があるため一時的にO0
```

## 必要な修正

### 短期的修正
1. MIR最適化パスでの型情報の保持を確実にする
2. MIR→LLVM変換時の型チェックを強化
3. Opaque pointer用の型情報伝播メカニズムを実装

### 長期的改善
1. MIR型システムとLLVM型システムの完全な統合
2. 最適化パス間での型不変条件の明確化
3. Opaque pointer完全対応のためのアーキテクチャ改善

## テストケース

```cm
// 最小限の再現コード
import std::io::printf;

int main() {
    int x = 42;
    int* p = &x;
    int y = *p;  // O1以上でCreateLoad assertion失敗
    printf("Value: %d\n", y);
    return 0;
}
```

## 関連ファイル
- `src/main.cpp` - 最適化レベル設定
- `src/codegen/llvm/core/mir_to_llvm.cpp` - LLVM IR生成
- `src/mir/passes/core/manager.hpp` - MIR最適化管理
- `src/mir/passes/scalar/*.hpp` - 各種最適化パス

## 次のステップ
1. 各MIR最適化パスを個別に無効化し、問題のあるパスを特定
2. 型情報の伝播を追跡するデバッグ機能を追加
3. LLVM 14+ opaque pointer移行ガイドラインに従った修正