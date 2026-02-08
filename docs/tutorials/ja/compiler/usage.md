---
title: コンパイラ使い方
parent: Tutorials
---

[English](../../en/compiler/usage.html)

# コンパイラ編 - コンパイラの使い方

**難易度:** 🟡 中級  
**所要時間:** 25分

## 📚 この章で学ぶこと

- cmコマンドの基本的な使い方
- サブコマンドとオプション
- デバッグ方法
- 出力形式の指定

---

## 基本的な使い方

### JITコンパイラで実行

```bash
# 基本形
./build/bin/cm run program.cm

# 複数ファイル（将来）
./build/bin/cm run main.cm lib.cm

# 標準入力から実行
echo 'int main() { println("Hello"); return 0; }' | ./build/bin/cm run -
```

### コンパイル

```bash
# 実行ファイルを生成
./build/bin/cm compile program.cm -o program

# デフォルト出力名（a.out）
./build/bin/cm compile program.cm

# 実行
./program
```

---

## サブコマンド

### run - JITコンパイラで実行

```bash
cm run program.cm
```

**特徴:**
- ✅ 高速なJIT実行
- ✅ デバッグが簡単
- ✅ LLVM最適化が効く

### compile - コンパイル

```bash
cm compile program.cm -o output
```

**特徴:**
- ✅ 高速な実行ファイル
- ✅ 最適化が効く
- ❌ コンパイル時間がかかる

---

## コンパイラオプション

### 最適化レベル

```bash
# 最適化なし（デバッグ用）
cm compile program.cm -O0

# 基本的な最適化
cm compile program.cm -O1

# 標準的な最適化（推奨）
cm compile program.cm -O2

# 最大最適化
cm compile program.cm -O3
```

### ターゲット指定

```bash
# ネイティブコード（デフォルト）
cm compile program.cm -o program

# WebAssembly
cm compile program.cm --target=wasm -o program.wasm
```

### 出力形式

```bash
# 実行ファイル（デフォルト）
cm compile program.cm -o program

# LLVM IR
cm compile program.cm --emit-llvm -o program.ll

# アセンブリ
cm compile program.cm --emit-asm -o program.s
```

---

## デバッグオプション

### デバッグモード

```bash
# 実行時デバッグ情報を表示
cm run program.cm --debug

# またはショートオプション
cm run program.cm -d
```

### デバッグ情報付きコンパイル

```bash
# デバッグシンボル付き
cm compile program.cm -g -o program_debug

# GDBでデバッグ
gdb ./program_debug
```

---

## 次のステップ

✅ cmコマンドの基本的な使い方を理解した  
✅ デバッグ方法がわかった  
✅ 最適化オプションを知った  
⏭️ 次は [LLVMバックエンド](llvm.html) を学びましょう

---

**前の章:** [文字列操作](../advanced/strings.html)  
**次の章:** [LLVMバックエンド](llvm.html)

---

**最終更新:** 2026-02-08
