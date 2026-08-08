# R22: implブロック内メソッドの`export`修飾子がパースエラー（native::sync/io高レベルAPIが全損）

**ステータス:** 修正済み（方針1=implメソッドのexportを正式受理。native::sync/ioの高レベルAPIを実行動作まで復旧し、importゲートの既知失敗リストを空にした）
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

## 実装記録

方針1（implメソッドの`export`を正式サポート）を採用した。メソッドの既定可視性は元々Export（公開）のため、`export`は既定の明示として受理する（挙動は不変・追加受理のみで非破壊）。

- **パーサ**: `parse_impl`（interface形）と`parse_impl_ctor`（inherent形）の両メソッドループへ`export`修飾子の消費を追加した（`export static`の順序も受理）。`export`と`private`の同時指定は専用診断（PsExportAndPrivateConflict、i18n en/ja）で拒否する。文法書`cm_grammar.md`のimpl_memberへmethod_modifier（export/private/static）を明文化。
- **静的ジェネリック呼び出しの2段修正（Mutex<T>::new(v)を動作させるのに必須だった併発バグ）**:
  - checker（call/function.cpp）: パラメータ型が未置換のTのまま実引数と比較され「expected T, got int」、型引数の`make_named("int")`がStruct kindの偽intを作り「expected Mutex<int>, got Mutex<int>」の誤診断だった。型引数をプリミティブは正しいTypeKindで構築し、パラメータ・戻り値の両方へ代入してから検査するよう修正（文字列切り出しはmethod-resolution-unificationで構造化予定と注記）。
  - HIR lowering（expr.cpp）: `Box<int>::new`形の静的呼び出しが未変換のまま流れ、**callが黙って消えてゼロ値になる無言誤動作**だった（`Mutex<int>::new(10)`がゼロ初期化構造体を返し、検証中のmutex出力も誤値だった）。インスタンスメソッドと同じBaseType__Arg1__Arg2マングル（`Box__int__new`）へ変換して特殊化メソッドを呼ぶよう修正。
- **native::sync復旧（全API実行検証済み）**: パース通過後に露出した未コンパイル起因バグを修理——`Mutex<T>*`フィールドへの`.`アクセス7箇所を`->`へ、式位置の`Mutex<T> { ... }`ジェネリック構造体リテラル（パース不能）をフィールド代入形へ、AtomicBoolのboolフィールドへの32bitアトミック操作（1バイトへの4バイト書き込み）をint保持へ。Mutex/RwLock/AtomicInt/AtomicLong/AtomicBool/Once/Channelの全構造体APIがjit/nativeで正しい値を返すことを確認した。
- **native::io復旧（実行検証済み）**: 推移的に壊れていたio系を修理——`IoResult.Ok(...)`のドットenum構築を`IoResult::Ok(...)`へ、`match self { Ok(_) => return true; }`の架空match構文を実文法`match (r) { IoResult::Ok(v) => {...} }`へ、factoryメソッドをstatic化、long→intの暗黙縮小へ`as int`、mod.cmの再export名不一致（BufferedReader→BufReader）を修正。Stdout/BufWriterの書き込み・Stdinのパイプ入力read_line()まで実行確認した。
- **言語未対応と判明し見送った2件（設計文書として記録）**: (1)ジェネリックenumのimplメソッド（`impl<T> IoResult<T> { is_ok() }`）はchecker解決を配線しても特殊化メソッド本体が誤値を返す（モノモーフ化未対応）ため、解決自体を配線せず正直なUnknown method診断を維持し、IoResultはmatch直接分岐のAPIにした。(2)`impl<R : Reader> R { ... }`の境界付きブランケットimplは未対応構文のため削除（traits.cmに注記）。
- **importゲート**: `tests/libs/run.sh`のKNOWN_BROKENリストを空にし、libs全30モジュールが検証対象になった（R9で常設したゲートの完全化）。
- **テスト**: `tests/common/impl/export_method.cm`（export受理・全バックエンド）、`tests/common/errors/export_private_method.cm`（併用診断）、`tests/common/generics/static_generic_call.cm`（int/string/long型引数の静的呼び出し値検証・全バックエンド）、`tests/llvm/sync/mutex_highlevel_test.cm`（高レベルAPI一式のnative実行）。unit/regression/interpreter/llvm/wasm/js/ts/sv/libs/cm-test/i18nの全スイートPASS。
- **チュートリアル**: concurrency/mutex.mdへ高レベルMutex<T>/RwLock<T>節を新設、atomic.md/channel.mdへ構造体API節を追加。
- **教訓**: 静的ジェネリック呼び出しはcheckerを直しても不十分で、HIRの名前変換漏れが「callが消えてゼロ値」という最悪の無言誤動作になっていた（型検査が通る＝実行が正しいではない）。未コンパイル出荷のstdlibは架空構文（ドットenum構築・match旧形・ブランケットimpl）の温床で、importゲートが再発を防ぐ。
