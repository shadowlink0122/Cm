# SV-N3: `casez`/`casex` と priority / unique0 case修飾

**分類:** idiom改善（don't-care）＋ 新機能（priority修飾）
**優先度:** High
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出）

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
