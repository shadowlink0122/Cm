---
title: B5 キャスト付きnull比較が文字列比較になる
parent: v0.17.0 Design
---

# B5: キャスト付きnull比較が文字列比較になる

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B5 | 比較 | キャスト付きnull比較がポインタ比較でなく文字列比較（cm_strcmp）に落ちる | High | 修正済み |

## 再現コード

```cm
use libc { void* malloc(long size); }
struct Node { int v; Node* next; }

int main() {
    Node* p = null;
    if (p == null as Node*) { return 1; }  // cm_strcmp呼び出しに落ちる
    if (p == null) { return 2; }           // 正常にicmp eq ptr
    return 0;
}
```

## 現象

比較オペランドの一方がキャスト式（`null as Node*`）に包まれていると、Eq/Ne loweringの文字列判定がポインタでなく文字列型と誤判定し、cm_strcmp呼び出しを発行する。
nullポインタに対するstrcmpは未定義動作でクラッシュまたは誤結果になる。
構文→LLVM IR対訳リファレンスのpointers-ffi検証で検出した（素の`p == null`は正常）。

## 根因（確定）

convertBinaryOp（src/internal/codegen/llvm/core/operators.cpp）のEq/Ne loweringが、LLVM表面型がptr同士なら無条件にcm_strcmpを発行していた。
素の`p == null`はnullが整数0でloweringされるためptr-int分岐でicmpになるが、`null as Node*`はキャストでptr型の値になるためptr-ptr分岐に入り、nullポインタへのstrcmpに落ちていた。
なお派生Eq（generate_builtin_eq_operator）は構造体型フィールドを生のMirBinaryOp::Eqへ落とすため、残りのptr-ptr比較のstrcmp挙動には実際に依存があり、単純なstring限定化は退行することを実測で確認した。

## 実装内容

- オペランドのHIR型がポインタ型かの判定を追加し、ポインタ型のptr-ptr比較は最優先でicmp eq/neを発行する
- string/cstringおよび派生Eqの構造体フィールド等の残りptr-ptr比較は既存のstrcmp挙動を維持する

## テスト

- tests/common/pointer/null_cast_comparison.cm — `== null as T*`・`!= null as T*`をif条件・変数代入の両方で、null/非null双方について素のnull比較と併せて検証
- 検証済み: icmp発行とcm_strcmp消失をIRで確認、O0/O2/jit PASS、pointer/string/strings/casting/structs/interface（derive_complex_fields含む）/function_ptr/memory/basicのO0/O2全件失敗ゼロ
- 補足: `Node* p = null;`（素のnull初期化）が現行ツリーの型検査で「expected '*Node', got 'void'」と拒否される別件を確認、要別途調査
