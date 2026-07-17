# 実装設計: SVモジュールIOの明示的構造体宣言（IOインスタンス）

## 背景・課題

従来の `//! sv: hierarchy` は、import先モジュールのポート宣言（`#[input]` 等の個別宣言）をソース走査で抽出し、extern struct を暗黙に自動生成してimport文を置換していた。
親側で使用する型が宣言なしに出現するため、静的型付け言語としてインターフェースの明示性を欠いていた。

## 設計

### IOインスタンス（構造体宣言＋インスタンス使用）

モジュールのIOは、C/C++スタイルの構造体宣言とそのインスタンスで定義する。

```cm
// alu.cm
struct alu_io {
    #[input] uint a;
    #[input] uint b;
    #[output] uint result = 0;
};

alu_io io;

void alu_comb() {
    io.result = io.a + io.b;
}
```

- 構造体宣言はC/C++の世界標準に合わせ、末尾セミコロン（`};`）を許容する（省略も可）
- `#[input]`/`#[output]`/`#[inout]` 属性付きフィールドを持つ構造体のグローバル変数（IOインスタンス）は、フィールドがそのままモジュールポートへ展開される（ポート名 = フィールド名）
- IOインスタンスを使う場合、個別のポート宣言（`#[input] uint a;` 等）は不要
- モジュール内のアクセスは `io.field` で行い、SV出力ではポート名へフラット化される
- `#[output]` フィールドの既定値（`= 0` 等）はポートの電源投入時初期値になる（IOフィールドとextern structフィールドのみ `= expr` を許容）
- IO構造体はデータ型（`typedef struct packed`）としては出力されない
- クロック（`#[input] posedge clk;`）等の直接ポート宣言はIOインスタンスと併用できる

### 階層化（//! sv: hierarchy）との連携

- import先のポート抽出はIOインスタンス（構造体フィールド）と直接ポート宣言の両方を対象にする
- 親側のextern struct生成・インスタンス化は従来どおり（`alu alu0 = alu { a: x, ... };`）

### 後方互換

- 個別ポート宣言のみのモジュールは従来どおり動作する（破壊的変更なし）
- 既存の構文への追加のみで、新しいキーワードは導入しない

## テスト計画

- `tests/sv/basic/io_instance.cm`: 組み合わせ回路のIOインスタンス（エンドツーエンド）
- `tests/sv/basic/io_instance_clocked.cm`: クロック併用とoutputフィールド既定値
- `tests/sv/hierarchy/hier_alu.cm`: 階層化の子モジュールをIOインスタンス方式へ移行
- 回帰テスト `IoInstanceExpandsPortsAndFlattensAccess`: ポート展開・アクセスのフラット化・typedef非出力をコード生成レベルで検証

## 追加対応（実装済み）

- `#[test]` 関数内の `io.field` 参照はテストベンチ生成時にフラットなポート名へ写像される（代入先の入力/出力/内部信号の検証も `io.field` 経由で機能する）
- インスタンス接続の値として `io.field` を指定可能（`hier_alu { a: io.x, ... }` → `.a(x)`）
- IOインスタンスのフィールドに付与した `#[sv::pin]` / `iostandard` 属性はピン制約（.cst/.xdc）へ反映される

## 将来拡張（本設計の対象外）

- 方向の静的検査（`#[input]` フィールドへの代入をコンパイルエラーにする）
- ソフトウェア系バックエンドでのIOインスタンスの意味論整備（現状はSVターゲット向け機能）
