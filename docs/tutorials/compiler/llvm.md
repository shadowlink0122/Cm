---
layout: default
title: LLVMバックエンド
parent: Tutorials
nav_order: 31
---

# コンパイラ編 - コンパイラの使い方

**難易度:** 🟡 中級  
**所要時間:** 20分

## 基本的な使い方

```bash
# インタプリタで実行
./build/bin/cm run program.cm

# LLVMコンパイル
./build/bin/cm compile program.cm -o program

# WASMコンパイル
./build/bin/cm compile program.cm --target=wasm -o program.wasm

# デバッグモード
./build/bin/cm run program.cm --debug
```

## コンパイラオプション

```bash
# 最適化レベル
./build/bin/cm compile program.cm -O0
./build/bin/cm compile program.cm -O2
./build/bin/cm compile program.cm -O3

# LLVM IR出力
./build/bin/cm compile program.cm --emit-llvm

# アセンブリ出力
./build/bin/cm compile program.cm --emit-asm
```

## サブコマンド

```bash
# run - インタプリタで実行
cm run program.cm

# compile - コンパイル
cm compile program.cm -o output

# check - 構文チェックのみ
cm check program.cm

# format - コードフォーマット（将来）
cm format program.cm

# test - テスト実行（将来）
cm test
```

---

**前の章:** [文字列操作](../advanced/strings.md)  
**次の章:** [LLVMバックエンド](llvm.md)
USAGE
# コンパイラ編 - LLVMバックエンド

**難易度:** 🟡 中級  
**所要時間:** 25分

## 特徴

- ネイティブコード生成
- 複数プラットフォーム対応
- 高度な最適化
- デバッグ情報生成

## サポート状況（v0.10.0）

| 機能 | 状態 |
|------|------|
| プリミティブ型 | ✅ 完全対応 |
| 構造体 | ✅ 完全対応 |
| 配列 | ✅ 完全対応 |
| ポインタ | ✅ 完全対応 |
| ジェネリクス | ✅ 完全対応 |
| インターフェース | ✅ 完全対応 |
| match式 | ✅ 完全対応 |
| with自動実装 | ✅ 完全対応 |
| typedef型ポインタ | ⚠️ v0.10.0予定 |

## コンパイル例

```bash
# 基本的なコンパイル
cm compile hello.cm -o hello

# 最適化レベル指定
cm compile program.cm -O3 -o program

# LLVM IR を確認
cm compile program.cm --emit-llvm -o program.ll
cat program.ll

# デバッグ情報付き
cm compile program.cm -g -o program
```

## 最適化レベル

- `-O0`: 最適化なし（デバッグ用）
- `-O1`: 基本的な最適化
- `-O2`: 標準的な最適化（推奨）
- `-O3`: 最大最適化

---

**前の章:** [コンパイラの使い方](usage.md)  
**次の章:** [WASMバックエンド](wasm.md)
