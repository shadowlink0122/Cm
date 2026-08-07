# R7: 属性の検証レジストリ（タイポ黙認によるテスト黙殺・未解釈属性の無警告受理・#[cfg]/#[target]の不活性）

**ステータス:** 未修正（第6ラウンド検出）
**重大度:** High

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/attrs/`）

1. **属性名のタイポが無診断で素通りし、テストが黙って実行されない。** `#[tset]`（testのタイポ）を付けた「実行されれば必ず失敗するassert入りテスト関数」は`cm test`で実行されず、スイートは`✓ 1 test(s) passed`の緑・exit=0になる。`check --strict`も0件。属性パーサ（`src/internal/syntax/parser/module/attribute.cpp`）は任意識別子の`#[名前]`を構文受理し、フィルタリング側（`target_filtering_visitor.hpp`）は`test`/`target`以外を全て無視するため、未知属性が一律黙殺される。`#[deriv(Eq)]`（deriveのタイポ）も同様に素通りする。
2. **既知のはずの属性も解釈されず黙殺される。** `#[deprecated]`付き関数の呼び出しで警告ゼロ（check/--strict/run/compile全経路）。`#[bench]`/`#[optimize]`も無診断・無効果で、`#[bench]`関数は`cm test`で実行されない。一方`#`直付け形式（`#deprecated`/`#bench`/`#optimize`）は「Directive '#X' is not yet implemented」で正しく拒否されるため、同じ語が角括弧の有無で「黙殺」と「未実装エラー」に分裂している。
3. **`inline`は両形式とも記述不能。** `#inline`は予約語のためIdentチェックを通らず「Unknown or invalid directive」、`#[inline]`は「Expected identifier, got reserved word 'inline'」でパースエラー。他ディレクティブと不整合。
4. **`#[derive(Unknown)]`の診断位置がずれる。** エラー自体は出るが位置がファイル1行目のコメントを指す（属性は2行目）。
5. **`#[cfg(...)]`が完全に不活性。** `#[cfg(never_defined_symbol)]`付き関数は未定義シンボル条件でも除去されず、無診断で呼び出せる（`run`で実行・`check --strict`で0件）。attribute.cppはcfgをパースするがフィルタリング側は`test`/`target`しか評価しない。条件コンパイルの意味が成立していない。
6. **`#[target("jss")]`等の未知ターゲット名が無診断でNative扱いに縮退する。** `string_to_target`のフォールバックがNativeのため、`#[target("jss")]`（jsのタイポ）は事実上`#[target("native")]`として動き、js限定のつもりのコードが無診断でnative限定へ意味反転する（`--target=js`では除去される）。`check --strict`でも0件。

補足: `#[target("js")]`の正常系（native/jitで除去され呼び出すと診断）と否定形`#[target(!js)]`の双方向動作は健全に機能している。

## 期待仕様（提案）

既知属性のレジストリ（test/bench/deprecated/inline/optimize/target/cfg/derive/input/output/inout/sv::*系）を単一ソース化し、(a) 未知・タイポ属性は警告（--strictでエラー）、(b) 既知だが未実装の属性は「未実装」専用診断、(c) 実装済み属性は解釈、の3値に分類する。ランタイムビルトインのレジストリ化（runtime-builtin-registry.md）と同じ「表で網羅を強制する」方針。

## 修正方針

attribute.cppのパース後、既知属性表と突き合わせる検証パスを追加する（SV系`sv::`名前空間は表の別セクション）。`#[deprecated]`は呼び出しサイト警告をcheckerへ、`#[inline]`は予約語トークンを属性名として受理する特例をパーサへ追加する。診断位置は属性トークンのspanを使う。

## テスト計画

エラーテスト: `#[tset]`警告・`#[deriv(Eq)]`警告・`#[bench]`未実装診断・`#[deprecated]`呼び出し警告・`#[inline]`受理。`cm test`でのタイポ検出（未知属性付き関数が1つでもあれば警告）のE2E。
