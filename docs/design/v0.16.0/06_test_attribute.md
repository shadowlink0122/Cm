# v0.16.0 実装設計 6: `#[test]` 統一テスト属性と `cm test` コマンド

優先度: 追加（ユーザー要望 2026-07-10）目的: 設計05の `#[sv::testbench]` を **バックエンド中立の `#[test]` 属性**へ再設計し、どのバックエンドでもテストを実行できる統一モデルにする。

## 再設計の背景（4つの指摘）

1. テスト属性は `#[test]` が良い（`sv::` 名前空間はSV専用を意味してしまう）
2. テスト属性はどのバックエンドでも実行可能にすべき
3. `#end` は不要で、`#[test]` の直後の関数をテスト時の実行対象にする（現状は `#ifdef SIM ... #end` でテスト関数を囲う運用が必要だった）
4. `//! platform:` 指定がある限り、どのバックエンドでテストを実行するかは自明

4指摘は矛盾なく合成できる: **`#[test]` = バックエンド中立のテストマーカー + テストビルド限定コンパイル**、**`//! platform:` = 実行バックエンドのディスパッチ**という2層の役割分担になる。

## 構文

```cm
//! platform: sv
#[input] posedge clk;
#[input] uint din;
#[output] uint dout = 0;

async void ff(posedge clk) {
    dout = din;
}

import std::debug::assert;

// #end も #ifdef SIM も不要。#[test] 直後の関数がテスト対象になる
#[test]
void latch_first() {
    din = 5;
    step(1);
    assert(dout == 5, "first value latched");
}

#[test]
void latch_second() {
    din = 7;
    step(2);
    assert(dout == 7, "second value latched");
}
```

- `#[test]` は**直後の関数宣言のみ**に付く（属性の既存セマンティクスそのまま）。旧運用の `#end` はもともと `#ifdef` の終端であり、属性には元々不要
- テスト関数は **引数なし・戻り値 `void`** であること

## セマンティクス

### 1. テストビルド限定コンパイル（Rustの `#[cfg(test)]` + `#[test]` に相当）

- **通常ビルド**（`cm run` / `cm compile` / `cm check`）: `#[test]` 関数は構文解析後・型チェック前にASTから除去される（既存の `#[target(...)]` フィルタ`TargetFilteringVisitor` に統合）。テスト関数が `#ifdef TEST` 配下のシンボルを参照していても通常ビルドを壊さない
- **テストモード**（`cm test` / `--test` フラグ）: `#[test]` 関数を含めてコンパイル。併せてプリプロセッサ定義 `TEST` を自動定義する（テスト補助コード・シミュレーション代替はこれまで通り `#ifdef TEST` で書ける。既存の `-D SIM` 等のユーザー定義との併用も可能）

### 2. バックエンド別のテスト実行

| バックエンド | 実行方法 | 実行モデル |
|---|---|---|
| SV (`--target=sv`) | テストベンチ生成 → iverilog + vvp | 全 `#[test]` 関数を**宣言順に同一initialブロックで逐次実行**（DUT状態を共有） |
| native / JIT | `cm test` が各テスト関数をJITで直接実行 | **関数ごとに独立実行**（グローバル状態も毎回初期化＝テスト隔離） |
| js / wasm 等 | v0.16.0では未対応（明示エラー） | — |

- SV: 設計05のTB生成機構をそのまま使用（`step`→`repeat @(posedge clk)`、`assert`→PASS/FAIL表示+`$fatal`、`println`→`$display`）。複数 `#[test]` 関数は `// ---- test: <関数名> ----` コメント区切りで連結する
- native: テスト成功 = 関数が正常リターン。`assert` 失敗は"assertion failed: msg" を表示して exit(1)（最初の失敗で停止）。各テストの完了時に `[PASS] <関数名>` を表示する

### 3. `cm test` コマンドのディスパッチ（指摘4）

```
cm test <file> [-D NAME ...]
  1. //! platform: ディレクティブを読む
  2. sv|verilog|systemverilog → SVフロー:
       compile --target=sv --test で .tmp/test/<stem>.sv + _tb.sv を生成し、
       iverilog -g2012 → vvp を実行（未インストールなら導入ヒントを表示してエラー）
       exit code = vvp の exit code（$fatal で非0）
  3. それ以外（native/指定なし）→ JITフロー:
       テストモードでコンパイルし、各 #[test] 関数を宣言順に実行
```

既存フロー（CmCPU等）との共存: `cm compile --target=sv --test -D SIM ...` でSV+TBだけ生成し、シミュレータ実行は外部スクリプトが担う構成も引き続き可能。

## デザインパターン上の乖離点（指摘への回答として明記）

1. **`step()` はSVプラットフォームのテスト専用**。クロックという概念は実行系バックエンドに存在しないため、nativeテストで `step()` を使うと`cm test` が親切なエラーで拒否する。「どのバックエンドでも実行可能」なのは属性と実行基盤であり、テスト本文の表現力はプラットフォームに依存する（設計05 Layer 2 のサイクル精度MIRインタプリタ導入で解消予定）。これは指摘2と矛盾せず、指摘4の platform ディスパッチが吸収する
2. **実行モデルの差**: SVは1つのTB initialブロックで逐次実行（状態共有）、nativeは関数ごとに独立実行（状態隔離）。回路テストは時系列の連続性が本質なのでSV側は共有が自然、ソフトウェアテストは隔離が自然、と整理する
3. **`#end` について**: `#end` は条件付きコンパイル（`#ifdef`）の終端であり属性の構文には元々含まれない。指摘3の本質は「テスト関数を `#ifdef SIM ...
   #end` で囲う運用の廃止」であり、`#[test]` のテストビルド限定コンパイル化で実現する

## 実装

- `cli/options`: `Command::Test` と `--test` フラグ（`test_mode`）を追加
- `main.cpp`:
  - `cm test` を platform ディレクティブで SVフロー / JITフロー へ変換
  - テストモード時に `ConditionalPreprocessor` へ `TEST` を自動定義
  - `TargetFilteringVisitor` に `include_tests` を追加し、非テストモードで `#[test]` 宣言を除去
  - JITフロー: MIRから `test` 属性関数を列挙し、関数ごとに `JITEngine` で実行。実行前に `step` 呼び出しを検査（`MirOperand::FunctionRef == "step"`）
  - SVフロー: 生成後に iverilog + vvp を起動（生成物ディレクトリをCWDに実行、`$readmemh` の相対パス解決のため）
- `frontend/types`: `#[test]` 関数のシグネチャ検証（引数なし・void）
- `mir/lowering`: HIR文保持の判定を `sv::testbench` → `test` に変更
- `codegen/sv`: `testbench_fn_`（単一）→ `testbench_fns_`（複数、宣言順）に拡張し、モジュール本体からの除外判定も `test` に変更
- 旧 `#[sv::testbench]` / `#[verilog::testbench]` は **削除**（v0.16.0内の未リリース機能のため互換エイリアスは設けない）

## テスト計画

- `tests/sv/simulation/tb_function.cm` / `tb_function_fail.cm`: `#[test]` へ更新
- `tests/sv/simulation/tb_multi.cm`: 複数 `#[test]` 関数の宣言順逐次実行
- `tests/sv/errors/step_arg_type.cm`: `#[test]` へ更新
- native: `#[test]` 関数が通常ビルド（`cm run`）で除去されること（`#ifdef TEST` 配下のシンボルを参照するテスト関数を含むファイルが通常ビルドで成功する）
- native: `cm test` で `#[test]` 関数が実行され `[PASS]` が表示されること
- native: `step()` を含むテストを `cm test`（native）した場合のエラー
