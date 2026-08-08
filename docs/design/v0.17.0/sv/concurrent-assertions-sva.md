# SV-N7: 並行アサーション（SVA: assert property / sequence / property）

**分類:** 検証機能
**優先度:** Medium
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出。3系統いずれもゼロ）

## 現状（実測: cm 2026-08-08ビルド）

- 即時アサーション（`assert(cond, msg)`）は対応済み。本体内では `assert (cond) else $error("...")`、テストベンチ内では `if ((cond) !== 1'b1) $fatal(...)` を出力する。
- **並行アサーション（SVA）** — `assert property`・`sequence`・`property`・時相演算子（`|->`・`|=>`・`##N`・`$rose`/`$fell`/`$stable`・`$past`）は**生成手段がない**。

即時アサーションは「その瞬間の値」しか検査できず、「reqの2サイクル後に必ずackが立つ」「あるステートからは必ず別ステートへ遷移する」といった**時相的性質**を検証できない。FPGA/RTL開発では並行アサーションがバグ検出の主力であり、テストベンチ自動生成（`//! test:`）の検証力を大きく高める。

## 提案

Cmに時相的性質を記述する手段を用意し、SVの並行アサーションへ出力する。合成対象ではなくシミュレーション/検証（テストベンチ・`#[test]`文脈またはクロック付き本体）向け。案:

- 時相性質を記述する組み込み群を用意（クロック文脈で評価）:
  ```cm
  // req が立ったら 2 サイクル後に ack が立つ
  sv_assert_property(clk, implies(req, after(ack, 2)));
  ```
  →
  ```systemverilog
  property p_req_ack;
      @(posedge clk) req |-> ##2 ack;
  endproperty
  assert property (p_req_ack);
  ```
- 最小セット: `|->`（overlapped implication）・`|=>`（non-overlapped）・`##N`（Nサイクル遅延）・`$rose`/`$fell`/`$stable`・`$past(x, N)`。sequence合成（`and`/`or`/`throughout`）は将来拡張。

構文の与え方（組み込み関数 vs 専用ブロック）は設計判断が要る。まずは「クロック＋implication＋固定遅延」の基本形に絞り、`property`/`assert property`を生成できるようにする。

## 実装方針

- テストベンチ生成（`testbench.cpp`）とクロック付き本体の両文脈で、時相性質記述を`property ... endproperty` + `assert property (...)`へlowerする経路を追加。
- クロックは対象の`posedge`ポートから解決（既存のクロック検出を流用）。
- 即時アサーション（現状）と並行アサーションを明確に区別し、時相演算子を含む式は並行アサーション経路へ振り分ける。
- SV007の「テスト関数内で許可される呼び出し」ホワイトリスト（`testbench.cpp:433`）へ並行アサーション記述を追加する。

## テスト計画

`tests/sv/simulation/` へ: 実装（implication+##N）が `assert property` を出力し、性質を満たす回路でPASS・破る回路でiverilogが並行アサーション失敗を報告することを検証（negative check必須。x楽観性を避け確定値で）。`$past`/`$rose`の基本動作テスト。
