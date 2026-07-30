---
title: B6 境界経由メソッド呼び出しの戻り値型がvoid扱い
parent: v0.17.0 Design
---

# B6: インターフェイス境界経由メソッド呼び出しの戻り値型誤り

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B6 | ジェネリクス | インターフェイス境界経由の非voidメソッド呼び出しが型検査で誤拒否される | High | 修正済み |

## 再現コード

```cm
interface Shape { int area(); }
struct Sq { int s; }
impl Shape for Sq { int area() { return self.s * self.s; } }

<T: Shape> int total(T x) {
    return x.area();  // Return type mismatch: expected 'int', got 'void'
}
```

## 現象

ジェネリック境界`<T: Shape>`経由のメソッド呼び出しで、インターフェイス宣言のメソッド戻り値型が解決されずvoid扱いになり、returnや変数代入で型不一致として誤拒否される。
構文→LLVM IR対訳リファレンスのinterfaces検証で検出した（voidメソッドの呼び出しは通る）。

## 根因（確定）

2段階の欠落があった。

1. check_function / check_impl（src/internal/types/checking/decl.cpp）がジェネリック型パラメータをGenericContextへ登録する際に名前のみを渡し、`generic_params_v2`側の境界インターフェイス（`<T: Shape>`のShape）を伝搬していなかった
2. infer_member（src/internal/types/checking/call/method.cpp）のジェネリック型パラメータレシーバ分岐がメソッド解決を行わず無条件に`make_void()`を返していた

## 実装内容

- check_function / check_implで`generic_params_v2`の`type_constraint.interfaces`を引き、`add_type_param(param, bounds)`で境界をコンテキストへ伝搬
- infer_memberで境界インターフェイスの宣言シグネチャ（interface_methods_）からメソッドを解決し、引数個数・型検査のうえ戻り値型を式型として返す
- 戻り値型がインターフェイス自身の型パラメータ（Cloneのclone()等）の場合はレシーバ型で置換
- 境界にないメソッドは従来どおりvoid+遅延検査のフォールバックを維持し、具象型レシーバの既存解決は不変更

## テスト

- tests/common/generics/interface_bound_method_return.cm — 境界経由の非voidメソッドのreturn・const代入・式利用・string戻り値・複数具象型
- 検証済み: 修正前は型エラー、修正後は正値。generics+interfaceカテゴリでjit 60/60・llvm 60/60 PASS
- 関連バグB7は型検査側でなくMIR補間の別根因と確定（[interface-method-interpolation-type.md](interface-method-interpolation-type.md)）
