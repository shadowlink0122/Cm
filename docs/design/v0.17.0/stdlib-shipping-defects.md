# R9: stdlibの出荷不良（std::iterがコンパイル不能・コレクションのアロケータ素通し・std::io入力の再export解決不能）

**ステータス:** 未修正（構文網羅バグ調査で検出）
**重大度:** High（std::iterコンパイル不能）/ Medium（アロケータ素通し・再export）

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/stdlib/`）

### バグ1【High】`std::iter`モジュール（mod.cm）自体がコンパイル不能

`import std::iter::*;`を書くだけで失敗する。出荷されているstdlibが自身のエラーで壊れている。
```
error: function 'range' is already defined with a different signature (free-function overloading is not supported)
    --> libs/std/iter/mod.cm:102:14
error: Type mismatch in variable declaration 'value': expected 'int', got '*int'
    --> libs/std/iter/mod.cm:63:9
```
`range`の3重オーバーロード（自由関数オーバーロード未サポート）と`IntArrayIterator.next`の`int value = self.current;`（`self.current`が`*int`）の型エラーで、Range/RangeIterator/IntArrayIteratorが全て利用不能。統合テスト未カバー（tests/にimport std::iterするテストがない）。

### バグ2【Medium】コレクションがアロケータ差し替えを素通しする

`std::mem::set_allocator_fns`で差し替えたカスタムアロケータを、`Vector<T>`のpushが一切経由しない（実測: カスタム確保カウンタのデルタ=0）。原因は`libs/std/collections/vector.cm`が`use libc { malloc }`で生mallocを直呼びするため。`hashmap.cm`・`queue.cm`も同一パターン（ソース確認）。`mem/mod.cm`の「ランタイム内部確保が登録アロケータを経由する」記述とコレクション実装が乖離している。文字列連結・スライスpush・直接alloc/deallocはカスタム経由し、`reset_allocator`で復帰する（この部分は健全）。

### バグ3【Medium】`std::io`ファサードの入力API再exportが解決不能

`libs/std/io/mod.cm`は`export import std.io.console.input::{input, input_int, ...}`を宣言するが、`import std::io::{input, input_int}`も`import std::io::*`も両方失敗する（`error: 'input' is not a function`、診断位置が無関係な`output.cm:1:1`を指す）。`import std::io::console::input::{...}`と直接importすれば動くため、ドキュメントの窓口`std::io`経由で入力APIが使えない。H7の非export強制・M2/M7のimport重複排除後に残った再exportの解決漏れ。

## 修正方針

- **バグ1**: `std::iter::range`のオーバーロードを単一シグネチャ＋別名に整理（自由関数オーバーロードは未サポートの仕様）。`IntArrayIterator.next`の`self.current`デリファレンス（`*int`→`int`）を修正。`import std::iter::*`が通ることをCIゲートに追加（stdlib自身のコンパイルを常時検証）。
- **バグ2**: Vector/HashMap/Queueの`use libc { malloc/free }`を`std::mem`経由の確保（登録アロケータを通る経路）へ置き換える。M14のアロケータ到達可能化をコレクションまで延長。
- **バグ3**: 再exportされた関数シンボルの選択import解決を修正し、診断位置を再export宣言のspanへ。

## テスト計画

- `import std::iter::*`のコンパイル通過＋Range/IntArrayIteratorの基本動作テスト（tests/common/iterator/）。
- カスタムアロケータ差し替え後のVector/HashMap/Queue pushがカスタム経由することの回帰（tests/common/collections/）。
- `import std::io::{input_int}`経由の入力テスト（パイプ入力、tests/common/io/）。
- CIにlibs全モジュールの単体コンパイル検査を追加（`make test-libs`の拡張）。
