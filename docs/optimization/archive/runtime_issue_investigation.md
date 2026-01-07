[English](runtime_issue_investigation.en.html)

# 最適化による実行時問題の調査結果

## 概要
`slice_comprehensive.cm`テストで、最適化レベルO1以上で実行時に無限ループまたは意図しないIOが発生する問題を調査した。

## 調査結果

### 問題の症状
- **O0 (最適化なし)**: 正常動作
- **O1 (基本最適化)**: 実行時に無限ループ/IO待機
- **O2 (標準最適化)**: 実行時に無限ループ/IO待機
- **O3 (最大最適化)**: 実行時に無限ループ/IO待機

### テスト結果

#### 1. 簡単なbool配列テスト (`/tmp/test_bool.cm`)
```cm
bool[] bools = [true, false, true];
println("bool[]: {bools.len()} elements");
```
- **結果**: O0, O1ともに正常動作

#### 2. 包括的スライステスト (`slice_comprehensive.cm`)
- **O0 (-O0フラグ指定、出力ファイル指定)**: ✅ 正常動作
- **O0 (デフォルト、a.out出力)**: ❌ ハング
- **O1〜O3**: ❌ すべてハング

### 発見された問題

1. **出力ファイルの問題**:
   - 明示的に出力ファイルを指定した場合（`-o /tmp/slice_fresh_O0`）は正常動作
   - デフォルトの`a.out`を使用した場合、パイプやgtimeoutと組み合わせると問題が発生

2. **最適化による問題**:
   - O1以上の最適化で、スライス操作に関連するコードで実行時の無限ループが発生
   - 単純なbool配列は問題なし、複雑な操作（構造体スライス、メソッド呼び出し等）で問題発生

## 一時的な対策
現在、デフォルト最適化レベルをO0に設定済み：
```cpp
// src/codegen/llvm/native/codegen.hpp
int optimizationLevel = 0;  // デフォルトをO0に下げる（最適化による実行時無限ループを回避）
```

## 今後の対応

### 優先度高
1. スライス操作のLLVM IR生成を調査
2. O1最適化で具体的にどの最適化パスが問題を引き起こしているか特定
3. 最適化パスの修正または無効化

### 優先度中
1. a.out出力時のファイルハンドリング問題の調査
2. テストランナーの改善（明示的な出力ファイル指定）

### 優先度低
1. パフォーマンステストの追加（最適化レベル別）
2. 最適化ドキュメントの更新

## 関連ファイル
- `/Users/shadowlink/Documents/git/Cm/src/codegen/llvm/native/codegen.hpp` - 最適化レベル設定
- `/Users/shadowlink/Documents/git/Cm/src/mir/optimizations/optimization_pass.hpp` - 最適化パス管理
- `/Users/shadowlink/Documents/git/Cm/tests/test_programs/dynamic_array/slice_comprehensive.cm` - 問題のテストケース

## 調査コマンド履歴
```bash
# O0で正常動作確認
./cm compile tests/test_programs/dynamic_array/slice_comprehensive.cm -O0 -o /tmp/slice_O0
gtimeout 2 /tmp/slice_O0  # ✅ 成功

# O1で問題確認
./cm compile tests/test_programs/dynamic_array/slice_comprehensive.cm -O1 -o /tmp/slice_O1
gtimeout 2 /tmp/slice_O1  # ❌ タイムアウト
```

## 更新履歴
- 2026-01-02: 初回調査完了、O0をデフォルトに設定