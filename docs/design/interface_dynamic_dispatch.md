# interface値/ポインタの動的ディスパッチ設計

作成日: 2026-07-05
対象: v0.15.1

## 背景

これまでのinterfaceはジェネリクス制約経由の**静的ディスパッチ**のみサポートし、
interface型の値への代入（`Shape sh = sq;`）やポインタ経由の呼び出しは
クラッシュまたは誤動作していた（ポインタ/構造体監査で発見）。

## 表現: fat pointer

interface型の値は **fat pointer**（2ワード構造体）で表現する:

```
%Shape_fat_ptr = { ptr data, ptr vtable }
```

- `data`: 実装構造体（例: Sq）へのポインタ（**借用**。所有権は移動しない）
- `vtable`: `<Type>_<Interface>_vtable` グローバル（関数ポインタ配列、
  インターフェース宣言のメソッド順）への参照

vtableはMIRの `generate_vtables()` が impl 情報から生成し（既存）、
LLVMの `generateVTables()` がグローバル定数として実体化する（既存）。

## 変換（coercion）点

具象構造体 → interface型 の変換は以下で発生する:

| 箇所 | 例 | 実装 |
|---|---|---|
| 変数初期化/代入 | `Shape sh = sq;` | LLVM Assign文で dest型がinterfaceかつsrc型が具象構造体のとき fat pointer を構築（本実装で追加） |
| 関数引数 | `f(sq)` where `void f(Shape s)` | 呼び出し引数の型不一致時に fat pointer を構築（既存を共通ヘルパーへ集約） |
| ポインタ代入 | `Shape* p = &sq;` | `Shape*` 自体をfat pointer値として表現。dataフィールドが実装オブジェクトを**直接**指す（隠し領域なし。`(*p)` は恒等） |

`data` は元オブジェクトを指す借用のため、**元の構造体より長く生存する
interface値は未定義動作**（Rustの `&dyn Trait` と同じ制約）。

## ディスパッチ

interface型のレシーバに対するメソッド呼び出し
（MIRでは `call fn:Shape__area(fat値)`、`is_virtual=true`）は:

1. fat pointerから `data` / `vtable` を抽出
2. インターフェース宣言でのメソッド位置 × ポインタ幅 のオフセットで
   関数ポインタをロード
3. **インターフェース宣言のシグネチャから関数型を構成**し
   （戻り値型・引数型を反映。従来はvoid・self引数のみ固定で
   戻り値が破棄されていた欠陥を修正）、`data` をselfとして間接呼び出し
4. 戻り値を宛先ローカルへ格納

レシーバがfat pointer**へのポインタ**（`Shape* p` 経由の `(*p).area()`）の
場合はロードしてから同様に処理する。

## `Shape*` の表現（重要）

interface型へのポインタは「関数ポインタ表領域へのポインタ」ではなく、
**実装オブジェクトへのポインタを含むfat pointer値そのもの**として表現する
（Rustの `&dyn Trait` と同型）。`Shape* p = &sq` の `p` のdataフィールドは
`sq` を直接指し、`(*p)` のデリファレンスは恒等（fat自体が参照）。
再代入 `p = &rc` はdataとvtableの両方を差し替える。

## MIR側の変更

- コピー伝播の `same_type` がポインタの要素型を比較しておらず、
  `*Shape = copy(*Sq)`（coercionを含むコピー）が通常コピーとして
  伝播されて型の意味が失われていた欠陥を修正（要素型の再帰比較）
- 補間内のinterfaceメソッド呼び出しは `is_virtual` + fat値のselfで
  CallDataを構成する

## バックエンド対応状況

| バックエンド | 状態 |
|---|---|
| LLVM（JIT / native / WASM） | ✅ 本実装 |
| JS / interpreter | 今後（MIRレベルのfat struct + 関数ポインタ化で対応可能） |
| SV | 対象外（ハードウェアでは静的構造のみ。合成不能として報告） |

テストは `tests/llvm/interface/` に配置（llvm/jitスイートのみが実行）。

## 将来課題

- 補間内のポインタデリファレンス呼び出し `{(*p).area()}`
  （型情報なしミニパース経路のため未対応。文形式で代用可能）
- interface値の再代入をまたぐライフタイム検査（借用チェッカ統合）
- JS/interpreterバックエンド対応（MIRレベルへの lowering 移行）
- デフォルトメソッド・スーパーインターフェース
