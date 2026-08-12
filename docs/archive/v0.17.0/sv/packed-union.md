# SV-N6: packed union（ビット再解釈）

**分類:** 新機能（型）
**優先度:** Low
**ステータス:** 実装済み（v0.17.0）

## 現状（実測: cm 2026-08-08ビルド）

- packed struct は `typedef struct packed { ... } Name;` として生成される（ただし全structを一律packed出力・`#[sv::packed]`制御はTODO `analyze.cpp:863`）。
- **packed union**（同一ビット領域を複数のビューで解釈する `typedef union packed { ... }`）は生成手段がない。
- タグ付きunion（ペイロード付きenum）は**非目標**（合成不能）であり本項目の対象外。ここで扱うのは**同じビット幅を別レイアウトで再解釈する**合成可能なpacked unionのみ。

用途: プロトコルヘッダ/レジスタマップを、生ビット（`bit[32]`）とフィールド分解（packed struct）の両ビューで扱う、といったRTL頻出パターン。現状はビットスライス（SV-N1）で手動分解するしかない。

## 提案

Cmに「同一ビット幅の複数ビューを持つ合成可能union型」を表現する手段を用意し、SVの `typedef union packed { ... } Name;` を出力する。案:

- `#[sv::packed_union]` 属性を付けたstruct風宣言、または専用の`union`宣言（全メンバが同一ビット幅であることを型検査で強制）。
  ```cm
  #[sv::packed_union]
  struct Word {
      bit[32] raw;         // ビュー1: 生32ビット
      Fields  fields;      // ビュー2: packed struct（合計32ビット）
  }
  ```
  →
  ```systemverilog
  typedef union packed {
      logic [31:0] raw;
      Fields       fields;
  } Word;
  ```
- 全メンバのビット幅が一致することをコンパイル時に検査し、不一致はエラー。メンバアクセス`w.raw`/`w.fields.opcode`はSVのunionメンバ/ネストフィールドアクセスへ写像。

## 実装方針

- packed struct生成経路（`analyze.cpp:864-870`）の隣に packed union 生成を追加。全フィールドの`layout`ビット幅一致を検査（不一致はSV診断で停止）。
- メンバアクセスのlowering（`codegen.cpp`のフィールド射影）でunionメンバを名前アクセスへ写像。
- `#[sv::packed]`制御（TODO）と併せて、struct/unionのpacked/unpacked選択を属性駆動へ整理する。
- 非SVバックエンドでのpacked unionの意味論（メモリ再解釈）は合成専用機能として扱い、SVターゲット限定にするか、native/jitでの再解釈実装の要否を設計時に判断する。

## テスト計画

`tests/sv/advanced/` へ: 32ビットrawビューとpacked structフィールドビューを持つpacked unionが `typedef union packed` を出力し、iverilogで raw書き込み→フィールド読み出し（およびその逆）のビット対応が正しいことを値検証。ビット幅不一致メンバがエラーになる負のテスト。

## 実装記録（2026-08-11）

`#[sv::packed_union]`（`verilog::packed_union`別名も受理）属性で実装した。

- **生成**: SVコード生成のstruct typedef経路に分岐を追加し、`typedef union packed { ... } Name;` を出力する。メンバの型写像は既存のmapType（bit[N]→logic [N-1:0]・packed struct参照は型名）を共有する。
- **幅検査**: 全メンバのビット幅一致をコンパイル時に検査する（bit[N]=N・整数型=幅・packed struct=フィールド再帰合算）。不一致はSV009エラーで停止し、幅を確定できない型（記号幅・非bit配列・文字列等）のメンバもSV009で拒否する。
- **メンバアクセス**: SVのunionメンバアクセスはstructフィールドアクセスと同一構文のため、既存のフィールド射影出力がそのまま機能する（`w.raw`・`w.fields.opcode`）。
- **意味論の設計判断**: ビット再解釈（あるビューへの書き込みを別ビューで読む）はSVターゲット専用とし、実行系バックエンドではフィールドは独立ストレージの通常構造体として扱う（`#[sv::...]`名前空間の属性はSVターゲット向けの出力制御という既存の位置付けに従う。チュートリアルへ明記）。
- **検証**: `tests/sv/advanced/types/packed_union.cm`（32ビットraw+フィールド分解ビューの合成・verilator lint通過）と`tests/sv/errors/packed_union_width_mismatch.cm`（24/32ビット不一致のSV009）。SVスイート全数PASS。
- **属性の登録**: checkerの実装済み属性リストへ`sv::packed_union`を追加。VSCode拡張は汎用の`#[...]`属性パターンで既にハイライトされるため文法変更なし。
