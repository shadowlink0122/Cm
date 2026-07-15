# Hazard #45: LLVM Domination Error — パラメータ再代入によるcross-block参照

## ステータス: RESOLVED

## 概要

Cmコンパイラが `LLVM module verification failed: Instruction does not dominate all uses!` エラーを生成するバグ。MIRで関数パラメータが条件分岐内で再代入される場合、SSA形式の`locals[]`マップがblock-local値に汚染され、LLVM IRの支配関係が壊れる。

## 再現条件

- 関数パラメータ（例: `_4: ulong (cnt) [const]`）が条件分岐内で再代入される
- 例: `if (off + cnt > 4096) { cnt = 4096 - off; }`
- baremetal-x86ターゲットで複雑な制御フローを持つ関数で発生

## 根本原因

`src/codegen/llvm/core/mir_to_llvm.cpp` の `convertFunction()` 内:

1. **パラメータマッピング**: 非struct/非配列パラメータは `locals[idx] = &arg`（SSA形式）で設定
2. **`allocatedLocals`未登録**: パラメータは `allocatedLocals` に含まれない
3. **MIR再代入**: `_4 = copy(_20)` がbb6で実行
4. **SSA代入パス**: `isAllocated == false` → `locals[_4] = rvalue` (= `%load11` from bb6)
5. **cross-block使用**: bb8 (preds: bb6, bb7) で `_21 = copy(_4)` → `convertOperand` が`locals[_4]` = `%load11`（bb6のload命令）を直接使用 → bb7→bb8パスで未定義

### MIRパターン

```
bb4: { switchInt(...) -> [1: bb6, otherwise: bb7]; }
bb6: { _20 = 4096 - copy(_3); _4 = copy(_20); goto -> bb8; }  // _4再代入
bb7: { goto -> bb8; }
bb8: { _21 = copy(_4); ... }  // bb7から来た時、_4の古い値が正
```

### 生成されるLLVM IR（バグあり）

```llvm
bb6:
  %load11 = load i64, ptr %local_20     ; bb6でのみ定義
  br label %bb8

bb8:                                      ; preds = bb6, bb3 (bb7省略後)
  store i64 %load11, ptr %local_21       ; bb3→bb8パスでは%load11未定義！
```

## 修正内容

### 3段階の修正

#### 1. ローカル変数のallocaスキップ条件削除

関数ポインタ型・文字列一時変数のallocaスキップを削除し、常にallocaを生成。

#### 2. MIR事前スキャンによるパラメータalloca強制

```cpp
// パラメータlocalsの再代入を事前スキャン
std::unordered_set<unsigned int> reassignedArgLocals;
for (auto& bb : func.basic_blocks) {
    for (auto& stmt : bb->statements) {
        if (stmt->kind == Assign) {
            auto& assign = ...;
            // プロジェクションなし（直接代入）かつarg_localsに含まれる場合
            if (assign.place.projections.empty() &&
                find(arg_locals, assign.place.local) != end) {
                reassignedArgLocals.insert(assign.place.local);
            }
        }
    }
}
```

再代入されるパラメータのみallocaを強制。self引数等のフィールドアクセスは除外。

#### 3. パラメータマッピングのalloca化

```cpp
if (reassignedArgLocals.count(localIdx) > 0) {
    auto alloca = builder->CreateAlloca(arg.getType(), ...);
    builder->CreateStore(&arg, alloca);
    locals[localIdx] = alloca;
    allocatedLocals.insert(localIdx);
} else {
    locals[localIdx] = &arg;  // 従来通りSSA
}
```

## 重要な注意点

- **projectionチェック必須**: `_1.*.0 = copy(_4)` はフィールド書込みであり、`_1`の再代入ではない→ `projections.empty()` チェックなしだとimplメソッドのselfフィールド更新が壊れる
- **LLVM mem2regパス**: 不要なallocaはLLVMのmem2regパスが自動的にSSA形式に最適化

## テスト結果

- Cmテストスイート: 401 PASS, 0 FAIL, 7 SKIP
- Cosmo Linux VFSビルド: 成功
