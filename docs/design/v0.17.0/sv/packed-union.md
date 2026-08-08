# SV-N6: packed union（ビット再解釈）

**分類:** 新機能（型）
**優先度:** Low
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出。3系統いずれもゼロ）

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
