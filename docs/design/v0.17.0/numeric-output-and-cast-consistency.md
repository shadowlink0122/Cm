---
title: 数値出力精度とfloat→intキャスト挙動のバックエンド統一
parent: v0.17.0 Design
---

# 数値出力精度とfloat→intキャスト挙動のバックエンド統一

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| M8 | バックエンド | double出力がjit/native=6有効桁（123456789.5→123457000と桁化け）・js/ts=round-trip精度で分裂 | 未着手 |
| M9 | バックエンド | 範囲外float→intキャストがnative=飽和・他=ラップで分裂 | 未着手 |

## 背景と根本原因

double出力とfloat→intキャストは、ランタイムがバックエンド別に独立実装されているため（監査テーマ2「バックエンドごとのランタイム重複実装」）、同一プログラムがバックエンドで異なる結果になる。

### M8: double→文字列出力が3方式に分裂

println/文字列補間 `{}`/連結のdoubleは `cm_format_double` を呼び（`src/internal/codegen/llvm/core/print_codegen.cpp:98`, `src/internal/codegen/llvm/core/operators.cpp:161`, `:193`）、明示 `.to_string()` は `cm_double_to_string` を呼ぶ（`src/internal/mir/lowering/expr/cast.cpp:36-38`）。
LLVMコアは宣言のみ生成し、実体はバックエンド別の `runtime_format.c` で解決される（`src/internal/codegen/llvm/core/runtime/builtins.cpp:79-96`）。

native（jitも同一実装を共有）は precision<0 のとき有効桁を6に固定する（`src/internal/codegen/llvm/native/runtime_format.c:537-562`）。

```c
// runtime_format.c:538 — 有効桁6固定
int significant_digits = 6;
// :551-562 — 整数部が6桁超なら下位桁を丸め捨てる
double scale = cm_pow10(int_digits - significant_digits);
value = (long long)(value / scale + 0.5) * scale;
```

`123456789.5` は int_digits=9 → scale=10^3 → `(long long)(123457.2895)*1000 = 123457000` と桁化けする。
jitがnativeランタイムを使う裏付けは `src/internal/codegen/llvm/jit/jit_engine.cpp:83-97`（ホストプロセスの全シンボルを解決）である。

wasmは完全に別ロジックで、小数点以下5桁固定である（`src/internal/codegen/llvm/wasm/runtime_format.c:600-669`、特に `frac_int = (int)(frac_part * 100000)` :645）。
`123456789.5` は `123456789.5` を出力し、nativeとも異なる。

js/tsは `String(x)`（ECMAScript最短round-trip）で出力する（`src/internal/codegen/js/builtins.cpp:266-267`, `:279-280`）ため `123456789.5` をそのまま出す。

さらに wasm には `cm_double_to_string` の実体が存在しない（`grep` 0件、定義は `cm_format_double` :600 / `_precision` :671 等のみ）。
nativeは `runtime_format.c:1753` で定義する。
そのため明示stringキャスト（`cast.cpp:38` が `cm_double_to_string` を発行）がwasmでは未解決リンクになり得る（println経路は `cm_format_double` で露見しない、という非対称）。

### M9: 範囲外float→intキャストが3挙動に分裂

float/double→intの唯一の生成箇所は生の `fptosi` である（`src/internal/codegen/llvm/core/rvalue.cpp:122-124`）。

```cpp
// rvalue.cpp:122-124 — 飽和intrinsicを使わない生 fptosi
if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
    return builder->CreateFPToSI(value, targetType, "fptosi");
}
```

飽和intrinsic `llvm.fptosi.sat` は codegen配下に一切存在しない（`grep` 0件）。
生の `fptosi` は範囲外でポイズン（IR仕様上UB）であり、ターゲット命令の範囲外挙動に依存する。
x86-64 native/jit は `cvttsd2si` に落ち範囲外は INT_MIN を返すため飽和的に観測され、wasm は `i32.trunc_f64_s` 系でトラップする（同一IRでもターゲット依存で分裂）。

js/tsは `Math.trunc(operand)` で、範囲クランプなしに実数を切り捨てる（`src/internal/codegen/js/emit_expressions.cpp:422-424`）。JSのNumberは倍精度なので `1e20 → 1e20` のように飽和もラップもしない第3の挙動になる。

## 設計方針

### M8: double→string出力をround-trip精度へ統一

全バックエンドのdouble→string出力を最短round-trip表現（js/tsの現行挙動）に揃える。
round-tripを基準にする理由は、値を復元可能な最短表現であり精度喪失がないためである。

1. native/wasmの `cm_format_double` / `cm_double_to_string` を最短round-trip実装へ置換する。
   - C言語では `snprintf("%.17g", ...)` で確実にround-trip可能な表現を得て、必要に応じ最短化する（`%.17g`固定は冗長桁が出るため、Grisu/Ryu系の最短化を検討。ただし依存追加を避けるなら「桁数を17から減らして元値に戻る最小桁を探す」実装も可）。
   - native `runtime_format.c:537-562` の有効桁6固定ロジックを撤廃する。
   - wasm `runtime_format.c:600-669` の5桁固定ロジックを撤廃する。

2. wasmに欠落する `cm_double_to_string`（および `cm_uint_to_string` 等）の実体を追加し、明示stringキャストのリンク切れを解消する。

3. 中長期的には native/wasm/js の挙動仕様を単一実装（共通コア）へ寄せ、重複実装による再分裂を防ぐ（監査ロードマップ第3段5「ランタイムの共通コア化」に整合）。

### M9: 範囲外float→intキャストの挙動を仕様確定

範囲外float→intの挙動を全バックエンドで統一する。
候補は「飽和（clamp）」と「ラップ（wrap）」の2択で、本設計では飽和を推奨する。
理由は、飽和はデータ型の表現可能範囲内に必ず収まり、UB/トラップを排除でき、Rust/Swift等の近代言語の `as`/変換が飽和寄りである点である（言語設計原則: 信頼性の高い挙動）。

1. native/jit/wasm（LLVMコア）
   - `rvalue.cpp:123` の `CreateFPToSI` を飽和intrinsic `llvm.fptosi.sat`（符号なしは `llvm.fptoui.sat`）へ置換する。これによりターゲット非依存で範囲外を型の最大/最小へ飽和させる（wasmのトラップも解消）。

2. js/ts
   - `emit_expressions.cpp:424` の `Math.trunc(operand)` を、対象整数型の範囲へ飽和クランプする式に置換する（例: `Math.max(min, Math.min(max, Math.trunc(x)))`、NaNは0）。

3. 仕様文書化
   - 縮小整数キャスト（監査M4）と併せ、`as` キャストのオーバーフロー/範囲外挙動を仕様として明記する。飽和かつ無警告なのか、`--strict`で診断するのかを確定する。

## 構文例・出力例

M8:

```cm
fn main() {
    double d = 123456789.5;
    println("{d}");   // 期待（全バックエンド）: 123456789.5
}
```

現状: jit/native=`123457000`、wasm=`123456789.5`、js/ts=`123456789.5`。

M9:

```cm
fn main() {
    double big = 1e20;
    int n = big as int;
    println("{n}");   // 統一後（飽和）: 2147483647（int最大）
}
```

現状: native/jit=INT_MIN相当（-2147483648）、wasm=トラップ、js/ts=`1e20`相当（範囲無制限）。

## 実装の段階分割

1. M8-1: native `runtime_format.c:537-562` の有効桁6固定を撤廃しround-trip実装へ。
2. M8-2: wasm `runtime_format.c:600-669` の5桁固定を撤廃しround-trip実装へ、`cm_double_to_string`等の欠落実体を追加。
3. M8-3: 全バックエンドdouble出力一致の回帰確立。
4. M9-1: LLVMコア `rvalue.cpp:123` を `llvm.fptosi.sat`/`llvm.fptoui.sat` へ置換。
5. M9-2: js/ts `emit_expressions.cpp:424` を範囲飽和クランプへ。
6. M9-3: `as` キャスト範囲外挙動の仕様確定と文書化（M4縮小キャストと統合検討）。

## テスト計画（tests/common/配下）

- M8: `tests/common/formatting/` に大きな整数部を持つdouble・境界値（1e15前後）・負値・非常に小さい小数のprintln/補間/`.to_string()`を追加し、jit/native/wasm/js/ts一致を期待。
- M8: wasmでのdouble明示stringキャスト（`cm_double_to_string`経路）がリンク・実行できることを確認。
- M9: `tests/common/casting/` に範囲外float→int（正の巨大値・負の巨大値・NaN・infinity・型別 tiny/short/int/long）を追加し全バックエンド一致を期待。
- 回帰: 通常域のdouble出力・通常域float→intキャストが非退行（監査で「全バックエンド一致」と確認済みの範囲を壊さない）。

## リスクと非互換性

- double出力の桁数が変わるため、既存のテスト期待値（`tests/common/formatting/` 等の.expected）を一括更新する必要がある。README等に固定の数値出力例があれば追従する。
- 最短round-trip実装をnative/wasmのC言語ランタイムに入れる際、外部依存（Ryu等）を避けるなら自前実装のコストと正確性検証が必要。`%.17g`ベースの簡易実装から始め、冗長桁の最短化を段階的に行う。
- float→intを飽和へ変更すると、これまで「ラップに依存していた」ユーザーコード（意図的な下位ビット取り出し等）の挙動が変わる。ただし範囲外は元々UB/分裂しており依存すべきでないため、非互換の実害は小さい。仕様として明示する。
- 言語構文の変更はなく後方互換。実行時挙動をバックエンド間で一致させる方向の変更である。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（M8、M9、テーマ2「ランタイム重複実装」、C15 wasm println double）
- 関連所見: C15（wasm `println(double)`切り捨て）— 同じdouble出力経路、M4（縮小整数キャスト無警告）— `as`キャスト挙動の仕様統合
- 主要ファイル: `src/internal/codegen/llvm/native/runtime_format.c`, `src/internal/codegen/llvm/wasm/runtime_format.c`, `src/internal/codegen/llvm/core/rvalue.cpp`, `src/internal/codegen/js/builtins.cpp`, `src/internal/codegen/js/emit_expressions.cpp`, `src/internal/mir/lowering/expr/cast.cpp`
