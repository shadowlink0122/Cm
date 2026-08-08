---
title: B8 構造体typedef別名のリテラル使用不可
parent: v0.17.0 Design
---

# B8: 構造体typedef別名のリテラル使用不可

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B8 | typedef | 構造体typedef別名を構造体リテラルの型に使うとUnknown struct typeエラー | Medium | 修正済み |

## 再現コード

```cm
struct Point { int x; int y; }
typedef P = Point;

int main() {
    P p = P { x: 1, y: 2 };  // Unknown struct type
    return p.x;
}
```

## 現象

構造体リテラルの型名解決がtypedefを基底型へ展開せず、別名での構造体リテラル構築が拒否される。
変数宣言の型注釈としての別名（`P p = Point {...}`）は機能する。
構文→LLVM IR対訳リファレンスのdeclarations検証で検出した。

## 根因（確定）

infer_struct_literal（src/internal/types/checking/expr/primary.cpp）が構造体表をリテラルの型名のまま引いており、typedef定義の基底解決を通していなかった。
さらにHIR/MIRの構造体リテラルloweringが型名だけから裸の型を再構築するため、ジェネリック特殊化別名では型引数が落ちていた。

## 実装内容

- infer_struct_literalで構造体表未ヒット時にtypedef連鎖を再帰解決（循環ガード付き）し、リテラルの型名を基底名へ書き換えてHIR/コード生成へ伝播
- ジェネリック特殊化別名は型引数付き基底型を推論型として返し、HIRリテラル型として保持（src/internal/hir/lowering/expr.cpp）、MIRのlower_struct_literalへ式型を引き渡して特殊化リテラルの一時変数へ型引数付き型を設定（単相化の特殊化対象になる）

## テスト

- tests/common/types/typedef_struct_literal.cm — 単純別名・ネスト構造体・別名注釈+基底名リテラル併用
- tests/common/types/typedef_generic_struct_literal.cm — `typedef IntPair = Pair<int,int>` / `typedef MixedPair = Pair<int,string>` の別名リテラル
- 検証済み: jit O0/O2・llvm O0/O2・interpreterの関連スイート全PASS
- 別名チェーン（`typedef P2 = P;`）は型注釈としても壊れている別件の既存バグ（resolve_typedefが1段のみ）でB8の対象外、要別途対応
