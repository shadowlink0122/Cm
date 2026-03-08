---
description: LLVM Domination Error修正ルール - パラメータ再代入によるcross-block参照バグの回避
---

# LLVM Domination Error チェックルール

## 概要

Hazard #45として記録された、パラメータ再代入時のLLVM domination errorを防止するルール。

## チェック対象

`src/codegen/llvm/core/mir_to_llvm.cpp` の `convertFunction()` 内:

### 1. パラメータマッピング

- 全パラメータが `locals[]` と `allocatedLocals` に正しくマッピングされているか
- MIRで再代入されるパラメータは必ず `alloca` 経由にすること
- **注意**: selfポインタのフィールドアクセス（projection付き代入）は再代入ではない

### 2. ローカル変数アロケーション

- `continue` でallocaをスキップするパスが安全か確認
- SSA形式で扱うローカル変数がcross-blockで参照されないことの保証

### 3. Assign処理

- `locals[x] = rvalue` （SSAパス）が別blockで使われないことの確認
- `allocatedLocals.count(x) > 0` で必ず `store` パスに到達すること

## 変更時の注意

1. `convertOperand` の `isa<AllocaInst>` チェックに依存するパスがある
2. パラメータを `&arg` でSSA設定すると、再代入時に `locals[]` が汚染される
3. projection付き代入（`_1.*.0 = ...`）をパラメータ再代入と誤判定しないこと
