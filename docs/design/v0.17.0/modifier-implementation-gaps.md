# R11: 修飾子の未実装・黙殺（constexpr・inline・volatile・ufloat/udouble）

**ステータス:** 未修正（構文網羅バグ調査で検出）
**重大度:** Medium（constexpr壊れた診断・ufloat黙殺）/ Low（inline/volatile）

レクサに予約語として存在するが実装が伴っていない修飾子群。CANONICAL_SPECやチュートリアルに記載があるものは仕様乖離、黙殺されるものは誤用の温床になる。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/modifiers/`）

### C1 constexpr — 変数が未実装TODOで壊れた構文エラー、関数は黙って通常関数扱い

`constexpr int N = 10;`（トップレベル・ローカルとも）が`Expected type`/`Expected expression`で拒否される。真因は`src/internal/syntax/parser/module/toplevel.cpp`の`parse_constexpr()`がconstexpr変数を最後まで解析後`// TODO: ConstExprDeclノードを作成`で**nullptrを返す**ため、後続で無関係な診断に化ける。constexpr関数（`constexpr int sq(int x)`）は受理されるが`// TODO: constexprフラグを設定`のまま通常関数化し、`int[sq(3)] a;`（コンパイル時評価が要る文脈）では使えない（無警告）。`const int N = 10;`は配列サイズ含め正常。

### C2 inline — キーワードは黙殺、`#inline`ディレクティブは仕様記載ありなのに拒否

`inline int f() {...}`は受理・実行されるが、LLVM IR比較でinline有無が完全一致（`inlinehint`/`alwaysinline`属性なし）。パースされるだけでコード生成に伝搬しない無警告黙殺。CANONICAL_SPEC 8章記載の`#inline`ディレクティブは`Unknown or invalid directive`で拒否され、両形式とも実質記述不能（R7属性文書と関連）。

### C3 volatile — lexer予約のみでパーサ未対応

`volatile int x = 0;`がトップレベル・ローカルとも構文エラー（`Expected type`/`Expected expression`）。lexerで`KwVolatile`として予約されるがパーサに消費箇所が1つもない。どの位置でも宣言不可のため「O2/O3でvolatileアクセスが消えないか」は検証自体が不能。

### C5 ufloat/udouble — unsigned語義が完全未実装（負値が無診断）

`ufloat f = -1.0;`が全経路で`f=-1`（rc=0、`check --strict`もエラー0）。負になる演算（`a - b`で負）も無診断。型として受理・伝搬・出力されるが非負制約がどこにも強制されず、事実上float/doubleの別名。整数系（uint）ではZ5で符号変化診断が入ったが、浮動小数unsigned型には一切適用されない非対称。

## 修正方針

- constexpr: `parse_constexpr`のTODOを解消しConstExprDeclノードを実装（コンパイル時評価・配列サイズ/case文脈で使用可能に）。関数のconstexprフラグをコード生成へ伝搬。当面実装しないなら「constexpr未対応」の専用診断でnullptr返しをやめる。
- inline: `#inline`/`inline`のいずれかへ記法を一本化し、`inlinehint`属性をコード生成へ伝搬（またはドキュメントから削除）。
- volatile: パーサへ消費箇所を追加し、コード生成でvolatileロード/ストアを発行（最適化での除去を抑止）。当面未実装なら専用診断。
- ufloat/udouble: 非負制約をZ5の符号変化診断と同じ機構で代入・演算結果へ適用する。unsigned浮動小数を設計として持たないなら型自体を廃止しドキュメントから削除。

## テスト計画

各修飾子について「実装するなら動作テスト、未実装なら専用診断のエラーテスト」を用意し、`Expected type`のような無関係な汎用エラーやnullptr由来の壊れた診断を撲滅する。ufloatの負値代入・負演算の診断テスト。
