---
name: debug-compile
description: コンパイルエラーのデバッグ手順
---

# コンパイルエラーデバッグ

Cmコンパイラのエラーをデバッグするためのスキルです。

## デバッグオプション

### 基本デバッグ
```bash
./cm compile <file.cm> --debug
```

### MIR出力確認
```bash
./cm compile <file.cm> --mir-opt
```

### LLVM IR出力確認
```bash
./cm compile <file.cm> --lir-opt
```

## エラー種別と対処

### パースエラー
- 構文エラー: 行番号と列を確認
- トークンエラー: 不正な文字を確認

### 型エラー
- 型不一致: 期待型と実際の型を確認
- 未定義型: import文を確認

### LLVMエラー
- 検証エラー: `--lir-opt`で生成IRを確認
- リンクエラー: シンボル名を確認

## デバッグ手順

1. **最小再現コード作成**
   - 問題を再現する最小のコード

2. **段階的確認**
   - パース → HIR → MIR → LLVM

3. **出力比較**
   - 正常動作コードとの差分確認

## ログ確認
```bash
# 詳細ログ
CM_DEBUG=1 ./cm compile <file.cm>
```
