# SV-N3: `casez`/`casex` と priority / unique0 case修飾

**分類:** idiom改善（don't-care）＋ 新機能（priority修飾）
**優先度:** High
**ステータス:** 実装済み（casez自動出力＋priority/unique0属性）

## 現状（実測: cm 2026-08-08ビルド）

### don't-careパターンがnative `casez`でなくmask三項へ降下

`match`のdon't-careビットパターン（`0b1?00`等）は `(x & mask) == value` のif-else/三項チェーンへ脱糖される（`tests/sv/control/masked_match.cm`でSIM検証済み・値は正しい）。実測:

```
match (op) { 0b1?00 => { kind = 1; } 0b0?1? => { kind = 2; } _ => {...} }
→ kind <= ((op & 32'd11) == 32'd8) ? 32'sd1 : ((op & 32'd10) == 32'd2) ? 32'sd2 : ... ;
```

SVには don't-care を直接表現する `casez`（`?`/`z`をワイルドカード）・`casex`（`x`も含む）があり、デコーダ/命令デコード等では native casez の方が可読・合成意図が明確。現状は生成されない（3系統調査で`casez`/`casex`ともゼロ）。

### `priority` case修飾が出力されない

通常の`switch`/`match`は `unique case (...) ... default: ... endcase` を出力する（実測確認済み）。しかし優先順位付き分岐（先に書いたcaseが優先、重複許容）を示す `priority case` は出力手段がない。`unique`（相互排他を表明）と`priority`（順序優先を表明）は合成/検証で意味が異なる。

## 提案

1. **native casez の出力**: don't-careビットパターンを含む`match`/`switch`を、mask三項でなく `casez` へ出力する（`?`をSVのワイルドカードビットとして各caseアイテムに埋め込む）。
   ```systemverilog
   casez (op)
       4'b1?00: kind <= 1;
       4'b0?1?: kind <= 2;
       default: kind <= 0;
   endcase
   ```
   `casex`（`x`も無視）は入力にxが載る可能性のある検証向けで合成では非推奨のため、既定は`casez`とする。
2. **priority修飾**: 分岐の意味論を選べる属性を用意する（案: `#[sv::priority]` を`match`/`switch`文へ付与、または関数属性）。付与時は `priority case` を出力、既定は現状どおり `unique case`。重複caseを許容する`priority`の意味論を型検査で緩める。unique0（該当なしでもエラーにしない）も同属性系で。

## 実装方針

1. don't-care脱糖（match lowering）で、SVターゲットかつパターンにdon't-careビットがある場合はmask三項でなくcasez出力経路へ分岐する。パターンの`?`ビットを`casez`アイテムのワイルドカードへ変換（`emit_control.cpp`のcase出力＝236-277付近を拡張）。
2. case修飾子（unique/priority/unique0）を選ぶ属性を追加し、`emit_control.cpp:236`の`unique case`固定出力を属性駆動にする。
3. 非SVバックエンドはdon't-careを従来のmask比較で維持（casezはSV専用出力）。

## テスト計画

`tests/sv/control/` へ: don't-careパターンが`casez`を出力し、iverilogシミュレーションで`masked_match.cm`と同じ真理値表（op=8→1・7→2・0→3・15→0）を返す回帰。`priority`属性付きmatchが`priority case`を出力し、重複case（先勝ち）が意図どおり動くことの検証。既存のmask三項テストは（非SVまたは互換のため）別途維持。

## 実装記録

提案1（native casez）と提案2（case修飾属性）の両方を実装した。実装方針の「match loweringでSVターゲット分岐」はHIR層で行い、mask三項の再構成でなくMIRのSwitchInt終端へマスクを保持させる構造的な方式を採った。

- **HIR lowering**（`hir/lowering/stmt.cpp` の `try_lower_match_as_masked_switch`）: SVターゲット（`HirLowering::set_sv_target`。SV-N2と共通のフラグ）でdon't-careビットパターンを含む整数matchを、if-elseチェーンでなく `HirSwitch` へ脱糖する。変換対象はガード・束縛の無い Masked/整数リテラル/末尾ワイルドカード/それらのOr のみで、ガード・enum・途中のワイルドカード等を含むmatchは従来のif-elseチェーンへフォールバックする（意味論は同一）。`HirSwitchPattern` へ `Masked` 種別（比較値＋有効ビットマスク）を追加した。
- **MIR**（`mir/nodes.hpp` の `SwitchIntData`）: `target_masks`（targetsと同順のdon't-careマスク。空なら全件完全一致）と `sv_case_modifier`（0=既定・1=priority・2=unique0）を追加した。判定規則は**先頭から順に** `(discriminant & mask) == value`（matchの先勝ち意味論）。定数評価するパス（SCCP 2箇所・ConstantFolding）をこの規則へ更新し、SwitchIntDataを個別フィールドで複製する3箇所（const_unroll・inlining・monomorphization_utils）へ新フィールドの複製を追加した。CFG形状のみを扱う他のパス（simplify_cfg等）は値を解釈しないため変更不要。
- **SVコード生成**（`codegen/sv/emit_control.cpp`）: マスク付きSwitchIntは `casez` を出力する。パターンはスクルーチニの型幅Wの2進リテラル（マスクの0ビットを `?` に置換。`bit[4]` なら `4'b1?00`）で、先勝ち保存のためcase項の順序はMIRエントリ順を維持し、連続する同一遷移先のみカンマでまとめる。case修飾は属性指定が最優先、無指定のcasezはパターンの重なり（`(v_i & m_i & m_j) == (v_j & m_i & m_j)`）で自動選択する: 互いに素→`unique casez`・重なりあり→`priority casez`。マスク無しは従来どおり `unique case`（既存出力は不変）。
- **case修飾属性**（パーサ `parser_stmt.cpp`）: switch/match文の直前の `#[sv::priority]` / `#[sv::unique0]` を受理し、AST（`SwitchStmt`/`MatchExpr` の `sv_case_modifier`）→HIR→MIR→SVエミッタへ伝搬する。他の属性名・switch/match以外の直前は専用診断 `PsStmtAttributeOnlyCaseModifier` で停止する（文位置の属性はこれが初導入。宣言・関数の属性は従来経路のまま）。非SVターゲットでは属性は受理されるが無視され、実行意味論は変わらない。
- **casexは非出力**: 提案どおり合成で非推奨のため既定はcasezとし、casexの出力手段は設けない。
- **VSCode拡張**: 属性は汎用の `#[...]` 文法規則でハイライトされるため文法ソースの変更は不要だった。
- **テスト**: `tests/regression/sv_codegen_test.cpp` に生成SVの構文検証3件（`unique casez (op)`＋`4'b1?00`項・重なりパターンの `priority casez` とアーム順維持・`#[sv::priority]` の `priority case`）。`tests/sv/control/masked_priority.cm`（重なりパターンの先勝ちSIM検証）・`tests/sv/control/priority_switch.cm`（属性付きswitchのSIM検証）。既存の `masked_match.cm` はcasez出力へ切り替わったうえで同一真理値表をSIMで再検証済み。非SVのdon't-care match（`tests/common/control-flow/match/masked_pattern.cm`）は従来経路のまま全バックエンド一致を確認した。
- **判明した制約（対象外）**: `//! platform: sv` ファイルは非SVターゲットではディレクティブ不一致検査で停止するため、マスク付きSwitchIntが非SVバックエンド（LLVM/JS）へ到達する経路はない。`set_sv_target` は実際の出力ターゲット（--target=sv系）のみで判定するよう堅牢化した（`sv|js` のような複合ディレクティブの誤検知を排除）。
