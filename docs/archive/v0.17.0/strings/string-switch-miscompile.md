---
title: 文字列switchのLLVM検証失敗とjsの誤分岐
parent: v0.17.0 Design
---

# 文字列switchのLLVM検証失敗とjsの誤分岐

## 概要

switch文のスクルーチニに文字列を使うと、native/jitはLLVMモジュール検証エラー（`Switch constants must all be same type as switch value!`）で実行不能になり、jsはコンパイルが通るうえで常にelse分岐へ落ちる（文字列caseが一致しない）。
文字列switchを言語としてサポートするなら内容比較への脱糖が必要で、サポートしないなら型検査で明示エラーにすべきところ、現状はLLVM検証エラー（内部エラー相当）と黙った誤分岐という最悪の形で分裂している。

## 再現コード

```cm
import std::io::println;

int main() {
    string s = "beta";
    switch (s) {
        case("alpha") {
            println("a");
        }
        case("beta") {
            println("b");
        }
        else {
            println("other");
        }
    }
    return 0;
}
```

## 現象

| バックエンド | 結果 |
|---|---|
| jit | `JIT execution error: LLVM module verification failed: Switch constants must all be same type as switch value!`（`switch ptr ... [ i0 0, ... ]`という不正IR） |
| native | 同様の検証失敗でコンパイル不能 |
| js | コンパイル成功、実行すると常に`other`（文字列一致が判定されない） |

## 根因候補

switchのMIR loweringが整数スクルーチニ前提でLLVMのswitch命令へ直行しており、文字列（ポインタ）スクルーチニでもcase定数を整数として発行するため型不一致の不正IRになる。
jsバックエンドはswitchをそのままJSのswitchへ写像しているとみられるが、Cmの文字列はヘッダ付きポインタ/オブジェクト表現のため、case文字列リテラルとの`===`比較が内容一致にならず常に不一致になる。

## 修正方針

文字列switchを言語仕様としてサポートする方針を採り、MIR loweringでスクルーチニが文字列型の場合はswitch命令ではなくif-elseチェーン（cm_strcmp==0の逐次比較）へ脱糖する。
case値は文字列リテラルのみ許可し、非リテラルcaseは型検査エラーにする（整数switchと同じ定数制約）。
jsバックエンドはMIRの脱糖結果（比較チェーン）をそのまま受けるため個別対応は不要になる見込みだが、内容比較（==演算子のlowering）経由になることを回帰で固定する。
サポートしない判断になった場合は、型検査で「switchのスクルーチニは整数・char・enumのみ」を明示エラーにし、チュートリアルへ制約を記載する。

## テスト計画

- 文字列switch（一致・不一致・else落ち・空文字列case・同値異インスタンス）の回帰テストを全バックエンドで追加する
- charスクルーチニ・enumスクルーチニの既存動作が退行しないことを確認する
- 非リテラルcase（変数case）が明示エラーになることをerrorsカテゴリで固定する

## 解決記録（実装済み）

MIR loweringのlower_switch（stmt/control.cpp）で文字列スクルーチニを検出し、switch命令ではなく文字列Eq二項演算（C2/C3で全バックエンド対応済み）の逐次比較チェーンへ脱糖するようにした。
caseごとに`cmp = (scrutinee == "リテラル")`を発行してswitch_int(cmp, {{1, case}}, 次判定)で分岐し、全不一致でelse/defaultへ落ちる。
非リテラル/非文字列caseは一致しない扱い（型検査での明示エラー化は将来課題）。
回帰テスト tests/common/control_flow/string_switch.cm（一致・空文字列・else落ち・実行時連結値）をjit/native/js/wasmで検証した。
