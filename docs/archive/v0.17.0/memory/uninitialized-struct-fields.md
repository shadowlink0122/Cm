# 未初期化構造体フィールドのゼロ初期化（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H4 | バックエンド | 未初期化構造体フィールドがnative=スタックゴミ・jit=stringフィールドでクラッシュ・wasm/js/ts=ゼロ初期化の3分裂 | 実装済み（native/jitの集約ローカルをゼロ初期化して統一） |

## 背景と根本原因

構造体・固定長配列のローカル変数を初期化子なしで宣言すると、初期化されなかったフィールド・要素の値がバックエンドで分裂していた。
- native/jit: `alloca` した領域を初期化しないためスタック上のゴミがそのまま読まれる（実測で `-1342177280` 等の不定値）。string型フィールドではゴミがポインタとして解釈されデリファレンス時にクラッシュしうる。
- wasm/js/ts: 集約はゼロ初期化されるため未初期化フィールドは `0`／空になる。

LLVMコード生成のローカル割り当てループ（`src/internal/codegen/llvm/core/translate/function.cpp` の 323 行以降）では、Tagged Union 型（`__TaggedUnion_` 接頭辞）のみ `CreateMemSet` でゼロ初期化していた（旧 459-465 行）。通常の構造体・ユニオン・固定長配列の `alloca`（452 行の一般経路）はゼロ初期化されていなかった。

## 設計方針

native/jit（LLVMバックエンド）の集約型ローカルの `alloca` 直後に `CreateMemSet(alloca, 0, allocSize)` を挿入し、wasm/js のゼロ初期化に挙動を統一する。
- 対象は `Struct`・`Union`・固定長 `Array`（`array_size` あり）。動的配列（スライス）は既存の `cm_slice_new` 経路で初期化されるため対象外。
- スライスメンバの `cm_slice_new` 初期化（468 行以降）はこの `memset` の後に実行され、ゼロで潰したスライスヘッダを正しいポインタで上書きするため順序上の問題はない。
- 全要素を後で代入する構造体でも先頭で一度ゼロ埋めするが、余分なゼロストアは LLVM の mem2reg/DSE が O1 以上で除去する。巨大な固定長配列（`int[16384]` 等）でも `memset` は要素数に対し線形であり、集約コピーの二次爆発（[[aggregate-copy-lowering]] C14）とは別問題で安全。

## 構文例・出力例

```cm
struct Data { int a; int b; int c; }
Data d;
d.a = 1;                       // b, c は未初期化
println("b={d.b} c={d.c}");    // 修正後は全バックエンドで b=0 c=0
```

## 実装の段階分割

- Phase 1（実装済み）: `translate/function.cpp` の集約ローカル `alloca` にゼロ初期化 `memset` を追加。
- Phase 2（未着手）: 確定代入解析（[[definite-assignment-and-correctness-lints]] H6）を導入したうえで、「使用前に確実に代入される」と証明できる集約についてはゼロ初期化を省略してコード量を削減する最適化。ゼロ初期化はあくまで安全側の既定動作として残す。

## テスト計画

- `tests/common/structs/uninitialized_fields.cm`（追加済み）: 未初期化の構造体フィールド・ネスト構造体・固定長配列ローカルを読み、全バックエンドで `0` になることを確認する。
- 既存の構造体・配列テストが `memset` 追加後も回帰しないことを jit/native/js/wasm で確認する。

## リスクと非互換性

- 破壊的変更ではない。未初期化読み取りはこれまで未定義動作であり、ゼロ初期化はそれを安全側へ確定させる。
- ゴミの不定値に依存していたコードは存在しないはず（非決定的なため）。
- 性能影響は O1 以上の DSE でほぼ消えるため実質的に無視できる。

## 関連

- [[definite-assignment-and-correctness-lints]]（H6。未初期化読み取りの静的検出。ゼロ初期化と併用で「うっかりゼロ」も警告できる）
- [[aggregate-copy-lowering]]（C14。集約の memcpy 化。巨大構造体の別問題）
- [[collections-option-api-and-errors]]（H8。マップの未初期化値返しは本件と同系統だがAPI設計で解決）
