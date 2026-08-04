---
title: int×double混合二項演算が不正IR・SIGBUSになる（Y4）
parent: v0.17.0 Design
---

# int×double混合二項演算が不正IR・SIGBUSになる（Y4）

## 概要

整数変数と浮動小数の混合二項演算（`i + d`・`d + i`・`d > i`・`dacc += i`・`i + 1.5`）で、整数オペランドへのsitofp昇格が挿入されず、`fadd i32, double` のような型不一致のLLVM IRが発行される。
形により症状が分裂し、`d + i`（double左辺）はJITのモジュール検証エラー（明示的失敗）、`i + d`・`d > i`・`i + 1.5`は**無出力のSIGBUS（rc=138）**、nativeは全形でコンパイル失敗になる。
checkerは混合演算を無診断で受理しており（チュートリアルもint→doubleの暗黙変換を明記）、型検査の受理とcodegenの能力が乖離している。
既存スイートが通っているのは、テストが「リテラル同士（定数畳み込みで消える）」か「明示キャスト」しか使っていないためで、`double r = i + d;` という最頻出パターンが実際には全経路で壊れている。

## 再現コード

```cm
import std::io::println;
int main() {
    int i = 5;
    double d = 1.5;
    double a = d + i;
    // JIT: LLVM module verification failed (fadd double, i32)
    double b = i + d;
    // JIT: 無出力でSIGBUS
    bool c = d > i;
    // 同上（fcmp mixed）
    double acc = 0.0;
    acc += i;
    // 同上
    double e = i + 1.5;
    // 同上（リテラル混合でも発生）
    println("{a}");
    return 0;
}
```

## 原因

B2修正（整数右辺→浮動小数宛先の代入Cast挿入）は代入・let初期化の文脈に限定した`coerce_to_float_context`であり、二項演算のオペランド昇格には適用されていない。
MIRの二項演算lowering（`expr/binary.cpp`）はオペランド型の不一致を検出せず、HIR/型検査もオペランドへ昇格Castを注釈しないため、LLVM二項命令へ幅・型の異なるオペランドがそのまま渡る。
SIGBUSになる形は、検証前のcodegen段階（fcmp/fadd構築時のLLVM内部アサーション相当）で落ちているとみられ、診断すら出ない最悪の失敗様式である。

## 修正方針

1. 型検査の`infer_binary`で算術・比較の共通型（usual arithmetic conversions: int系×float系→double/float、幅の異なる整数→広い方）を確定し、必要なオペランドへCastノードを注釈する（型検査を唯一の判断点にする）。
2. MIRの二項演算loweringは「両オペランド同型」を前提にし、不一致を検出したら黙って発行せず内部エラー診断で停止する（防衛層）。
3. 複合代入（`+=`）はHIRの脱糖（inner binary + assign）を通るため、1.の昇格が自動適用されることを確認する。
4. 昇格規則はcm_grammar.md／CANONICAL_SPEC.mdへ明文化する（int×double→double、float×double→double、符号混在整数の規則を含む）。

## テスト計画

- regression: `i+d`/`d+i`/`i+リテラル`/`比較`/`+=`/`float×int`/`long×double` の各形で値検証（コンパイル可否ではなく演算結果まで確認する）。
- integration（全バックエンド）: 混合演算の結果一致。js/tsはNumber/BigInt境界（H5系）との相互作用があるため必ず含める。

## 検出経緯

v0.17.0全修正後のレイヤー別レビュー（第4ラウンド）で検出。最小再現は `.tmp/bughunt4/min_promo*.cm`、網羅は同 `c/c08_promo_mixed.cm`。
