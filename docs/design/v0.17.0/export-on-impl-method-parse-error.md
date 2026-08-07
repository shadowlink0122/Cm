# R22: implブロック内メソッドの`export`修飾子がパースエラー（native::sync/io高レベルAPIが全損）

**ステータス:** 未修正（第8ラウンド検出）
**重大度:** High

`impl`ブロック内のメソッドに`export`修飾子を付けるとパーサが`Expected type`で停止する。この構文を使う標準ライブラリ（`native::sync`の高レベルOOP API・`native::io`の一部）が丸ごとimport不能になっている。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt8/{concurrency,verify}/`）

最小再現:
```cm
struct Pt { int x; }
impl Pt { export int getx() { return self.x; } }   // ← preprocessor error: Expected type
int main() { Pt p = { x: 7 }; println("{p.getx()}"); return 0; }
```
実測: `export`なし版は`7`を出力（正常）、`export`付き版は`preprocessor error: syntax error in imported module '...': Expected type`（rc=1）。`impl<T>`ジェネリックの有無は無関係（非ジェネリック`impl`でも`export`メソッドがあれば失敗）。

影響（実測）:
- `import native::sync::Mutex;`（および`channel_create`等mod.cm由来の全シンボル）が同じ`Expected type`で失敗する。`libs/native/sync/mod.cm`は`export T* get()`/`export static Mutex<T> new(...)`等、**全メソッドにexportを付けており全滅**。宣伝されている高レベルAPI（`Mutex<T>::new().lock().get()`等）は一度もコンパイルできない死んだコード。
- `libs/native/sync/mutex.cm`の冒頭コメント「impl<T>ブロックを含まないため、インポート時のパーサーエラーを回避」が示す通り、メンテナ自身がこのバグを認識し、impl無しのfree-functionサブモジュール（mutex.cm/channel.cm/atomic.cm）を回避策として用意している。ユーザーは低レベルfree-function API（手動バイトオフセットでpthreadバッファを扱う）しか使えない。
- 付随: `import native::io::stream::stdout::Stdout;`も、推移的import先`libs/native/io/error.cm`の`export enum IoResult<T>`が`Expected '=>'`で失敗し連鎖する（ジェネリックenumのexport絡みの同種パーサ問題）。

正しい流儀（実働している`libs/std/iter/range.cm`）はメソッドにexportを付けず、implブロックごとexportするか、structのexportで足りる。つまり「implメソッドへの個別export」という構文自体がパーサでサポートされていない。

## 論点と方針

`export`をimplメソッドに付けられるべきか（言語仕様の確定）が先。2択:
1. **implメソッドへの`export`を正式サポート**: パーサの`parse_impl`のメソッドループ（`parser_decl.cpp`）に`export`修飾子の消費を追加する。native::sync/ioの高レベルAPIがそのまま生き返る。可視性の意味（impl単位でexportか個別か）を確定する。
2. **implメソッドへの`export`を明示的に禁止**: 「implメソッドにexportは付けられない（structまたはimplブロックをexportする）」の専用診断にし、`Expected type`の誤誘導をやめる。この場合はnative::sync/ioのmod.cmを修正（export除去）する必要がある。

いずれにせよ現状の`Expected type`は原因を示さない誤誘導診断で、標準ライブラリが自身のバグを回避策で避けている状態は解消すべき。方針1が高レベルAPIを復活させる本筋。

## テスト計画

- `tests/common/`へ: implメソッドの`export`が（方針1なら）受理され外部importできる、または（方針2なら）専用診断で拒否される負のテスト。
- `import native::sync::Mutex;`・`import native::io::stream::stdout::Stdout;`が通ることの回帰（方針決定後）。libs全モジュールの単体import検査をCIへ（R9のstdlib出荷不良と同じ「lib自身がimportできること」のゲート）。
