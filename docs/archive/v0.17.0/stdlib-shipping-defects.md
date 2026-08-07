# R9: stdlibの出荷不良（std::iterがコンパイル不能・コレクションのアロケータ素通し・std::io入力の再export解決不能）

**ステータス:** 修正済み（バグ1〜3＋libs全モジュールのimportゲート追加。掃引で発見した同類の出荷不良4件も同時修正）
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

## 実装記録

- **バグ1（std::iterコンパイル不能）**: `range`の3重オーバーロードを単一シグネチャ`range(int start, int end, int step = 1)`（デフォルト引数）+`range_to(int end)`の2名へ整理し、`IntArrayIterator.next/peek`のデリファレンス欠落（`*self.current`）を修正、未サポートの`overload self`コンストラクタを削除した。あわせて`impl Range { RangeIterator iter() }`を追加し、`for (int x in range(1, 4))`のfor-inプロトコル（Q1で確立したiter()/has_next()/next()）で範囲反復が書けるようになった。
- **バグ2（コレクションのアロケータ素通し）**: Vector/HashMap/Queueの`use libc { malloc/free }`直呼びを`import std::mem::{alloc, dealloc}`経由へ置換し、`set_allocator_fns`で登録したカスタムアロケータを通るようにした（カスタム確保カウンタで検証）。
- **バグ3（std::io入力APIの再export解決不能）**: module graphの選択的再export辺（`export import x::{items}`）が「println等のMIR組み込みをCm定義で影置換しない」ための一律素通しになっており、実Cm関数のinput系まで解決不能だった。`request_item`/`request_wildcard`を「要求名がitemsに含まれれば辿る。ただし組み込みI/O名（print/println/eprint/eprintln）は従来どおり素通し」へ修正し、`import std::io::{input_int}`と`import std::io::*`の両方で入力APIが解決される（ワイルドカードは使用箇所駆動フィルタを維持）。
- **掃引で発見した同類の出荷不良（importゲートの整備中に検出・同時修正）**:
  - `std::core`: `typedef usize/isize`が予約語化済みの型名と衝突しパース不能→削除。`abs`の4重オーバーロード→min/maxと同形のジェネリック`<T> T abs(T x)`1本へ（`std::math`の2重オーバーロードも同様に統一）。
  - `std.core.async`: モジュールパスの`async`が予約語でimport文が書けず全API到達不能→`std.core.time`へ改名（now_ms/sleep_ms/Timerの時刻・タイマーモジュール。implメソッドの`export`修飾子=R22パターンも除去）。ランタイムの`cm_now_ms`は未リンクだったため、時刻関数のみの`runtime_time.c`を新設してコアランタイム（runtime.c）へ包含した（runtime_event_loop.c全体の包含はconstructor/destructorがチャネル終了処理の潜在バグとヒープ配置の相互作用でexit時SIGSEGVを誘発したため見送り。asyncのnative対応時に再検討）。
  - `native::math`: floatリテラル接尾辞`3.14f`が未実装構文でモジュール全体がパース不能→`as float`表記へ（接尾辞の言語対応はR13の領分）。sin/cos/sqrt等11組の同名float版オーバーロード→`sin_f`等の`_f`接尾辞へ改名。
  - `std::mem`: moduleヘッダが`module mem;`で宣言名とパスが不一致→`module std.mem;`へ修正。
- **importゲート**: `tests/libs/run.sh`へ「libs全mod.cmのmoduleヘッダからimport文を生成してcm checkする」ゲートを追加した（27モジュールPASS。R22で壊れているnative/io・native/io/stream・native/syncは既知失敗として明示SKIPし、R22修正時にリストから除去する）。
- **テスト**: `tests/common/iterator/std_iter_module.cm`（range/for-in/IntArrayIterator、全バックエンド）、`tests/common/allocator/collections_use_allocator.cm`（カスタムアロケータ経由の検証、JIT/Native）、`tests/common/modules/io_reexport_input.cm`（再export解決）、`tests/common/std/core_time.cm`（now_ms/Timer）。unit/regression/interpreter/llvm/wasm/js/ts/sv/libs/cm-testの全スイートPASS。
- **チュートリアル**: stdlib/io.mdの入力API節を実シグネチャ（引数なし・プロンプトはprint併用）へ修正、stdlib/core-utils.mdへstd::iterとstd::core::timeの節を新設、stdlib/index.mdへ2モジュール追加、stdlib/mem.md（ja/en）へコレクションがアロケータを経由することを明記。
- **教訓**: 出荷済みstdlibでも「importするテストが1本もない」モジュールは壊れたまま出荷される。importゲートが常時検証になったため、以後の新モジュールは自動的に検査対象になる。
