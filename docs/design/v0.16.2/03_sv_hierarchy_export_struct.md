# 実装設計: SV階層化のexport struct + import移行（//! sv: hierarchy の廃止）

## 背景・課題

`//! sv: hierarchy` ディレクティブは、相対import先のソースを走査してextern structを暗黙に自動生成し、import文を置換していた。
親側で使用する型（`hier_alu` 等のモジュール名型）が宣言なしに出現するため、`cm check` / `cm lint` 単体では型解決できず、`tests/sv/hierarchy/` をlint走査から除外する運用を強いていた。
IOインスタンス契約（設計01）の導入により、モジュールのインターフェースを構造体として明示宣言できるようになったため、暗黙のディレクティブは不要になった。

## 設計

### 子モジュール: IO構造体をexportする

```cm
// hier_alu.cm
//! platform: sv

export struct HierAluIo {
    #[input] uint a;
    #[input] uint b;
    #[input] utiny op;
    #[output] uint result = 0;
};

HierAluIo io;

void alu_comb() {
    io.result = ...;
}
```

- exportされたIO構造体（方向属性フィールドを持つ構造体）が、そのモジュールの公開インターフェース契約になる
- クロックも含めて全ポートをIO構造体のフィールドで宣言する（`#[input] bool clk;`。IOインスタンス展開の `f.name == "clk"` 検出により、`async void f(posedge clk)` のトリガは従来どおり機能する）
- パラメータは `#[sv::parameter] const` 宣言（モジュール自身のparameter出力）に加え、インターフェース契約として `#[sv::param]` フィールドを構造体に宣言する（インスタンス側からの上書きを型検査可能にするため）

### 親モジュール: importして修飾名でインスタンス化する

```cm
// hier_top.cm
//! platform: sv

import ./hier_alu;

hier_alu::HierAluIo alu0 = hier_alu::HierAluIo { a: io.x, b: io.y, op: op_add, result: alu_out };
```

- `//! sv: hierarchy` ディレクティブは不要（廃止）
- 型は `<モジュール名>::<IO構造体名>` の修飾名で参照する（通常のモジュールシステムで解決されるため、`cm check` / `cm lint` が特別扱いなしで通る）
- structリテラルのフィールドはIO構造体の宣言と型検査される（`#[sv::param]` フィールドの上書き含む）

### SVコード生成（process_sv_hierarchy の判定変更）

- 発動条件: ディレクティブではなく「相対import（`import ./X;`）かつ import先が方向属性フィールドを持つ `export struct` を宣言している」ことで判定する
- import行の置換: 従来どおり `extern struct X { ... }` 1行宣言に置換する。フィールドはexportされたIO構造体から生成する（`#[sv::param]` フィールドはそのまま、方向属性フィールドは初期値を除去して写す。extern structでは出力フィールドの `= 値` が接続指定と解釈されるため）。`#[sv::parameter] const` からのパラメータ抽出も併用し、名前で重複排除する
- 親ソース中の `X::<IO構造体名>` は `X` へ置換し、既存のextern structインスタンス生成（モジュールインスタンス化・`#(.PARAM(...))` 分離・`io.field` 接続）をそのまま利用する
- サブモジュールの個別コンパイルと連結出力（append_submodules）は従来どおり
- exportされたIO構造体を持たない相対importは従来どおりフラット化される（HDMI集約モジュール等の既存動作は不変）

### 廃止

- `//! sv: hierarchy` ディレクティブの検出・発動を削除する（記述されていても単なるコメントとして無視され、importはフラット化される）
- `.cmconfig.yml` の `tests/sv/hierarchy/` 除外を削除する（lint/check単体で解決可能になるため）

## テスト計画

- `tests/sv/hierarchy/hier_alu.cm` / `hier_top.cm`: export struct + 修飾名インスタンス化へ移行（挙動・期待値は不変）
- `tests/sv/hierarchy/param_shifter.cm` / `param_top.cm`: パラメータ上書き（`WIDTH: 16` → `#(.WIDTH(16))`）を新方式で検証
- 回帰テスト: exportされたIO構造体からのextern struct生成と親ソース置換をコード生成レベルで検証
- `cm lint --strict -r .` / `cm check` が除外なしで警告・エラーゼロになること

## 将来拡張（本設計の対象外）

- 選択import・エイリアス経由の階層化
- 非相対import（ライブラリモジュール）の階層化
