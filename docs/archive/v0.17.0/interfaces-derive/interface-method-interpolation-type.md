---
title: B7 インターフェイスメソッド戻り値の補間型取り違え
parent: v0.17.0 Design
---

# B7: インターフェイスメソッド戻り値の直接補間の型取り違え

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B7 | 補間 | インターフェイスメソッドの戻り値を直接補間するとstringがintとして書式化される | High | 修正済み |

## 再現コード

```cm
interface Animal { string name(); }
struct Dog {}
impl Animal for Dog { string name() { return "dog"; } }

int main() {
    Animal a = Dog {};
    println("{a.name()}");  // stringがintとして書式化される
    return 0;
}
```

## 現象

補間内の動的ディスパッチ呼び出しの結果型がstringと解決されず、整数用のcm_format_replace_*が選択されてポインタ値が整数として出力される。
変数に束縛してから補間すれば正常のため、補間内での式型解決の欠落が原因。
構文→LLVM IR対訳リファレンスのinterfaces検証で検出した。

## 根因（確定）

型検査は従来から正しくstring型を返しており、B6と同根ではない。
補間プレースホルダはMIRのlower_interp_expression（src/internal/mir/lowering/expr_interp.cpp）が文字列から再パースするミニパイプラインで処理され、型チェッカーを通らない。
戻り値型の補完処理はマングル名`Animal__name`を`ctx.hir_func_defs`から探すが、implは具象名`Dog__name`で登録され、インターフェイス宣言のシグネチャはLoweringContextに存在しないため解決不能→null型→int書式化でゴミ値になる。
同じ欠落により、モノモーフ化された関数内の`"{x.label()}"`（callee `T__label`）も文字列がゴミ値になる。

## 実装内容

- HIRのインターフェイス宣言から`Iface__method`→戻り値型のマップをregister_interfaceで構築し（MirLoweringBase::interface_method_returns_、src/internal/mir/lowering/lowering.cpp・base.hpp）、LoweringContextへシードして補間の戻り値型補完（expr_interp.cpp）で参照する
- モノモーフ化前のジェネリック関数本体でcalleeが`T__method`となるケースは、メソッド名の境界つき末尾一致で任意のインターフェイス宣言から戻り値型を引くフォールバックを追加
- 回避経路（変数束縛経由・println直接引数）の既存動作は不変

## テスト

- tests/common/interface/method_interp_direct.cm — インターフェイス型変数のstring/int/double戻り値メソッドの直接補間・混在プレースホルダ・具象型差し替えの動的ディスパッチ
- tests/common/interface/method_interp_generic_bound.cm — 境界`<T: Shape>`経由ジェネリック関数内でのstring/int/double直接補間（2特殊化）
- 検証済み: 修正前のゴミ値出力が解消し、jit/llvm両バックエンドO0/O2で正値、interface/generics/formattingスイート78/78 PASS

## テスト計画

- tests/common/interface/へインターフェイス型変数・境界経由の両方でstring/int/double戻り値メソッドを直接補間するケースを追加し、出力文字列を検証する
