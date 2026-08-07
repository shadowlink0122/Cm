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

v0.17.0全修正後のレイヤー別レビューで検出。最小再現は `.tmp/bughunt4/min_promo*.cm`、網羅は同 `c/c08_promo_mixed.cm`。

## 実装記録（2026-08-05）

- 型検査`infer_binary`（`src/internal/types/checking/expr/operator.cpp`）のtypedef解決直後に昇格挿入を実装した。算術・比較は`common_type`の共通型へ両オペランドをCastノードで包み、複合代入（`+= -= *= /= %=`）は宛先型（左辺）へ右辺を揃える。浮動小数が絡む混合のみ対象とし、整数同士の幅混在は既存のコード生成幅合わせを変えない。
- MIR lowering（`src/internal/mir/lowering/expr/binary.cpp`）に防衛層を追加した。算術・比較で浮動小数×整数の混合オペランドが到達した場合は不正IRを発行せず診断で停止する（型検査を唯一の判断点とする設計の維持）。
- 昇格規則をCANONICAL_SPEC.md 10.2へ明文化し、回帰テスト`tests/common/types/mixed_numeric_promotion.cm`（両方向・リテラル混合・比較・複合代入・float×double・long×double・float剰余・intのみ複合代入の非影響）を追加した。
- 検証: 全再現（`d+i`/`i+d`/`d>i`/`acc+=i`/`i+1.5`）が正値化し、unit・regression・interpreter・llvm・llvm-wasm・js・svの全スイート通過。wasmでもc08バッテリーの値一致を確認した。
- 残課題はZ5（implicit-explicit-cast-design.md）が引き継ぐ: 縮小変換の段階的エラー化・double→int暗黙代入の変換命令欠落・stdlibの回避策as削減。

## 追補: O0での浮動小数`%`のlibmリンク欠落（2026-08-05）

本修正の回帰テストが露出させた別問題として、浮動小数の`%`（LLVMの`frem`）はO0で`fmod`/`fmodf`のライブラリコールに残る（O2以上は定数畳み込み・インライン化で消えるためCIのO3ジョブでは未露見）。
LinuxネイティブはリンクコマンドにlibmがなくCIのllvm O0ジョブがリンクエラー（`undefined reference to fmod`）、wasmは`-nostdlib`でlibmが存在せず`unknown import: env::fmod`のインスタンス化失敗になっていた（macOSネイティブはlibSystemにlibm同梱のため通過）。
対処: Linuxネイティブのリンクへ`-lm`を常時付与（noStdターゲット除く）し、wasmランタイムに`fmod`/`fmodf`のビット操作による正確な剰余実装（musl方式）を追加した（`src/internal/codegen/llvm/wasm/runtime_math.c`）。
`x - trunc(x/y)*y`の近似は商が大きい場合（例: `1e18 % 3`）に誤るため採用せず、大値・負値・非正規化数・floatの各ケースでjit/native/wasmの値一致を確認した。
