# 局所処理の全体調査（正準実装の複製とドリフト）

## 目的と方法

`as (typeof(x))` の調査で見つかった「型文法が汎用の `parse_type` を通らず局所的にパースされる」問題を発端に、コンパイラ全体で同種の**局所的・特殊ケース処理**が残っていないかを網羅調査した。5系統（型文法・期待型伝播・型内省/キャスト系・メソッド/演算子ディスパッチ・バックエンド分岐/変換）を実バイナリ（v0.17.0 の `./cm`）に対する多数の `.cm` プローブで実測し、主要所見は個別に再現確認した。

## 総括（一本の筋）

見つかった非汎用箇所はほぼ全て**同じ構造**を持つ: ある概念の**正準実装が1つあるのに、その概念を消費する側が独自の複製（ホワイトリスト・サフィックス選択・部分文法）を持ち、正準実装からドリフトして一部のケースを取りこぼす**。「同じものは1箇所」の不変条件が破れた箇所が、そのまま局所処理として表面化している。取りこぼしの多くは**無診断のまま誤コンパイル/クラッシュ**する（拒否で済むものより危険）。

- 型文法の正準は `parse_type` / `parse_type_with_union` だが、呼び出し側に4つの複製（文の宣言判定・sizeof の型判定・as/is の配列サフィックス・パーサ内の2箇所の再帰）がある。
- 期待型伝播の正準は `infer_type_expecting` / `propagate_literal_expected_type` だが、透過であるべき合成ノード（三項・match・ジェネリック実引数置換）を降りない。
- 配列要素のディスパッチ正準は `slice_dispatch.hpp` だが、高階関数群がそこへ移行されず `_i32`/`_i64` の手書き選択のまま。
- 数値変換の挿入は全文脈で行われるのに、縮小**診断**は一部文脈でしか出ない。
- 値整形/型内省の一部がバックエンドごとに独自実装され、同一入力で結果が分岐する。

以下、系統別に所見を挙げる。severityは「silent=無診断の誤結果/クラッシュ（最重）」「reject=明確なエラーで停止」「diag=診断の不一致のみ」。file:lineは調査時点。

---

## A. 型文法の複製（正準 `parse_type`/`parse_type_with_union` からのドリフト）【一部処置済み】

同じ完全な型が「あらゆる型位置」で受理されるべきなのに、呼び出し側の型判定ホワイトリストや部分文法が正準からずれて一部を落とす。

- **A1 [reject] 文の宣言判定 `is_type_start()` が `typeof`/`(` を型開始と認識しない**（`src/internal/syntax/parser/parser_stmt.cpp:414`）。`typeof(x) y = ...;` と `(int) x = ...;` は宣言と認識されず式文として失敗するが、同じ型は仮引数・戻り値・フィールド・typedef・`as`（`parse_type`経由）では通る。`const (int) k` は `const` が宣言経路を起動するため通る、という位置依存の不整合。
- **A2 [silent/reject] sizeof の型判定 `could_be_type` スイッチが不完全**（`src/internal/syntax/parser/expr/primary.cpp:97-139`、`__sizeof__` 用に:166-208で二重化）。`Ident/Star/Amp/LBracket` を型扱いするため識別子始まりの式が `parse_type` に回され、`sizeof(f())`・`sizeof(p.b)`・`sizeof(x+y)` はパースエラー、`sizeof(a[0])` は **0を無診断で返す**（`check_array_suffix` がサイズ0の配列型に化ける＋checkerの救済が構造体のみ）。同じ被演算子は `typeof` では全て正しく動く。
- **A3 [silent] `as`/`is` の対象型が `[]` しか後置配列を消費しない**（`src/internal/syntax/parser/expr/binary.cpp` の `consume_slice_suffix` ラムダ、`as`/`is` 双方が使用）。`x as int[N]` は `(x as int)[N]` の添字式に**黙って化ける**。`check_array_suffix`/`parse_type_with_union` を使っていないための取りこぼし。
- **A4 [reject] 括弧の中身が union を受理しない**（`src/internal/syntax/parser/parser_type.cpp:149`）。括弧規則が `parse_type()`（union非対応）へ再帰するため `(int | string)` があらゆる位置（typedef・宣言・`as`）で失敗する。`parse_type_with_union()` へ再帰すべき。
- **A5 [reject] ジェネリック型引数が union を受理しない**（`src/internal/syntax/parser/parser_type.cpp:337`）。`Foo<int | string>` が失敗する。引数ループが `parse_type` を使うため。`V[]` やフィールド等では union が通るのと非対称。
- **A6 [silent] `__alignof__` に式経路が無い**（`src/internal/syntax/parser/expr/primary.cpp:244-251`）。`__alignof__(x)`（x:int）が未解決名の既定アラインで **8を無診断で返す**（正4）。sizeof の変数救済が alignof には無い。
- （補足）配列サイズ文法が `IntLiteral` か素の `Ident` のみを受け付け（`parser_type.cpp` `check_array_suffix`）、`int[2+1]`・`int[sizeof(int)]` 等の定数式が型位置に合成できない。

**処置記録（A2）**: sizeof/`__sizeof__` の被演算子判定を共通ヘルパ `parse_sizeof_operand` へ一本化し（両組込に重複していた `could_be_type` スイッチを解消）、識別子始まりの被演算子は型パス（名前・`::`修飾・ジェネリック引数・ポインタ/配列サフィックス）が閉じ括弧まで到達する場合のみ型として解析する非破壊判定 `sizeof_operand_ident_is_type` を追加した。これにより `sizeof(p.x)`・`sizeof(f())`・`sizeof(x+y)` は式として解析され（従来はパースエラー）、型形の `sizeof(Point)`・`sizeof(Point*)`・`sizeof(Point[2])`・`sizeof(int[3])`・`__sizeof__(T)` は従来どおり型として解析される。識別子始まりの添字 `sizeof(a[0])` は型パスとして受理したうえで、型チェッカのsizeof救済を配列型へ拡張し、最内の被要素名が型でなく変数（配列/スライス）を指す場合は添字段数だけ要素型を剥がして要素型サイズを返すようにした（従来は要素数0の配列サイズ計算で無診断の0を返していた）。回帰テスト: `tests/common/types/general/sizeof_operand.cm`。残課題: A1（`typeof`/`(` を宣言開始と認識する `is_type_start` 拡張。`typeof` 宣言型は被演算式を保持するB系の解決が前提）と補足の配列サイズ定数式（`int[2+1]`）は本処置の対象外。

**処置記録（A3〜A6）**: A3は `as`/`is` の対象型の後置サフィックス消費を独自の空`[]`限定ラムダから正準の `check_array_suffix` へ置換し（`binary.cpp`）、`x as int[N]` が `(x as int)[N]` の添字式へ黙って化ける問題を解消した。配列型への `as` は実体loweringが無く無診断のゴミ値になるため、型チェッカで配列型キャストを専用診断 `TcCastToArrayUnsupported` で停止し、暗黙変換（代入・呼び出しの固定長→スライス）へ誘導する（ユニオン変種取り出し `union as int[]` は除外し既存のZ4検査へ委ねる）。あわせて `as` の `CastExpr` にスパンが設定されておらず新診断がstdlib先頭を指していた不具合を修正した（`is` 経路と同様に `Span{operand.start, end}` を付与）。A4は括弧型の中身を `parse_type` から union対応の `parse_type_with_union` へ再帰させ `(int | string)` をあらゆる型位置で受理（`parser_type.cpp`）、A5はジェネリック型引数ループを同じく `parse_type_with_union` へ変更し `Foo<int | string>` を受理（フィールド等でunionが通るのと対称化）した。A6は `__alignof__` の被演算子が型でなく変数を指す場合に静的型へ置換する変数救済を型チェッカへ追加し、未解決名の既定アライン8を返す無診断誤値を解消した（sizeofの変数救済と同型）。回帰テスト: `tests/common/types/general/type_grammar_union.cm`（A4/A5/A6）・`tests/common/types/casting/cast_to_array.cm`（A3の診断、`.error`）。残課題は上記A1・配列サイズ定数式に加え、`Type<Args>{...}` 構築式（`<`/`>` が比較演算子に解釈される）が一般に未対応な点（「その他の位置限定の非汎用」節）。

## B. `typeof` 型のスタブ化（被演算式を捨てる）

`typeof(式)` を型として解析する際、被演算式をパース後に**破棄**し、名前 `__typeof__` の未解決 `Inferred` 型を返すだけ（`src/internal/syntax/parser/parser_type.cpp` の KwTypeof 分岐）。型チェッカ/HIRで `__typeof__` は解決されない。結果:

- **B1 [silent] `sizeof(typeof(x))` が常に 8**（string値のsizeof）を返す（正は `sizeof(x)`）。型経路に `typeof` が無く式経路へ落ちるため。
- **B2 [silent] `typeof(&x)` が文字列 `"&x"`（ソース片）を返す**（正 `"*int"`）。`is_type_start()` の `case TokenKind::Amp`（`parser_stmt.cpp:463`）が先頭 `&` を無条件に参照型扱いにするため（`*` には `*Type Ident` の先読みガードがあるのに `&` には無い）。
- **B3 [reject/silent] `typeof` を宣言型/仮引数型に使うと未解決**。`typeof(j) k = i;` はA1で失敗、`int g(typeof(1) x)` は本体・呼び出し側とも `expected 'int', got '<inferred>'` になる。
- **B4 [silent] `auto k = i as typeof(j); typeof(k)` が `<inferred>`**。値としてのキャストはネイティブ系では効くがJSでは解決されない（G2）。
- 恒久対応には `ast::Type` に typeof の被演算式（または解決済み型）を保持させ、型チェッカで具体型へ解決する必要がある（[parenthesized-type-and-typeof-cast](parenthesized-type-and-typeof-cast.md) に既述）。

## C. 期待型伝播の複製（透過ノードを降りない）【処置済み】

`propagate_literal_expected_type` は `StructLiteralExpr`/`ArrayLiteralExpr` にしか降りず、期待型に対して**透過であるべき合成ノード**を扱わない。消費サイト個別パッチをAPI化した経緯があるが、以下が取り残されている。

- **C1 [silent] 三項の枝に期待型が伝播しない**。`Point p = c ? {x:1,y:2} : {x:3,y:4};`（括弧版）がネイティブで `2,0`、JSで `undefined,undefined`（正 `1,2`）。`infer_ternary`（`src/internal/types/checking/expr/operator.cpp:548` 付近）が両枝を `infer_type` で型付けし、`propagate_literal_expected_type`（`primary.cpp:425-488`）が `TernaryExpr` を降りないため、無名リテラルが `type_name` 無しでゼロblob化する。
- **C2 [silent] ジェネリック関数の明示型引数が実引数の期待型に置換されない**。`Point p = id<Point>({x:1,y:2});` がネイティブ `2,0`・JS `undefined`（正 `1,2`）。自由関数の引数経路（`src/internal/types/checking/call/function.cpp:646` 付近）が未置換の仮引数型 `T` のまま `infer_type_expecting` を呼ぶ（静的メソッド経路は `substituted_param` で置換しているのと非対称）。
- **C3 [reject] match のアーム値に期待型が伝播しない**。括弧付き無名構造体アームが `expected 'Point', got 'void'`。
- **C4 [reject] 素の `{...}`/`[...]` リテラルが三項枝・matchアームでパースできない**（ブロック/添字と曖昧）。他位置では通るのに位置限定で失敗する。

**処置記録（C1〜C4）**: `propagate_literal_expected_type` へ透過ノード分岐を追加し、三項の両枝とmatchの式形式アームへ構造体期待型を再帰伝播するようにした（`primary.cpp`）。C2は `infer_generic_call` の実引数推論を「明示型引数で仮引数型を置換→`infer_type_expecting`」の順へ変更（静的メソッド経路の `substituted_param` と同型、`generic.cpp`）。C4はパーサ2箇所——`?` エラー伝播演算子の三項判別ホワイトリストへ `[` と `{ ident :` 先読みを追加（`expr/postfix.cpp`）、matchアームの `=>` 直後 `{` を `{ ident :` 先読みで式形式へ分岐（`expr/match.cpp`）——で解消。配列期待型の三項/match伝播は、枝の配列リテラルへ動的スライス型を強制すると三項loweringの固定長前提と食い違い無診断で壊れるため対象外とし、従来どおり枝ごとの固定長型で検査する（サイズ不一致は明示診断のまま）。回帰テスト: `tests/common/structs/literal/anon-transparent-nodes.cm`。

## D. 文字列補間の制限された部分文法【処置済み】

補間プレースホルダが本物の式文法ではなく専用の文字列処理を通るため、外では通る式が中で壊れる。

- **D1 [silent] 先頭が数字のプレースホルダを黙って捨てる**（`src/internal/hir/string_interpolation.cpp:63` の `!std::isdigit(var.name[0])`）。`"{2 + 3}"` が `{2 + 3}` とリテラル出力される。`{(2+3)}` は通る。
- **D2 [silent/reject] 最初の `:` で書式指定に分割する**（`string_interpolation.cpp:53-59` の `content.find(':')`）。三項 `"{c ? a : b}"`（警告＋切り詰め）や `"{Color::Red}"`（**0を誤出力**）が壊れる。
- **D3 [silent] `{` 直後が配列リテラル `[` の呼び出しが解析されない**。`"{[1,2,3].len()}"` がリテラル出力。`{xs.map(f).len()}` 等は通る。

**処置記録（D1〜D3）**: 真因はMIRの補間展開 `extract_named_placeholders`（`mir/lowering/expr_interp.cpp`）がプレースホルダ内容の**先頭文字ホワイトリスト**（識別子・`_`・`!`・`~`・`-`・`*`・`(`・`self.`・`::`のみ許可）で受理を判定し、数値始まり `{2 + 3}`・配列リテラル始まり `{[1,2,3].len()}`・文字列リテラル始まり `{"s".len()}` を弾いて（1件でも弾くと文字列全体を未変換で返す）リテラル出力していたこと（D1/D3）。checkerは既に本物の式パーサでプレースホルダを検証・脱糖済み（`types/checking/utils/interp.cpp` の `parse_interp_content`）で無診断に受理していたため、両者のドリフトがそのまま表面化していた。ホワイトリストを撤廃し任意の非空内容を受理する（無効な内容はcheckerが警告済みで、値解決は識別子直接参照→`{内容}`リテラルへフォールバックする）ようにしてMIRをcheckerへ揃えた。D2はcheckerとMIR双方のコロン走査が三項の `:` をフォーマット指定子と誤認していた（`content.find(':')`相当）ため、両走査に三項 `?` の保留カウンタを追加し、対応するコロンはフォーマット区切りとみなさないようにした（`{c ? a : b}` が値へ、`{c ? a : b:x}` は末尾の `:x` のみ指定子）。あわせてMIR側で終端走査とコロン走査が二重化していたのを、コロン位置を終端走査の結果から導出する形へ統合し、両者の乖離余地を排した（`::`はパス区切りとして両側で維持。`{Color::Red}` 等のenum値は従来どおり。D1参照の `string_interpolation.cpp` は既に未使用のデッドコードで実経路ではない）。回帰テスト: `tests/common/strings/formatting/interpolation/expr-placeholder.cm`（全バックエンド一致）。

## E. 配列高階関数の要素型ディスパッチ複製（`slice_dispatch.hpp` 未移行）【処置済み】

要素型クラス分け（幅別スカラ/Ptr/Blob/内側スライス）の正準は `slice_dispatch.hpp` で、`push/pop/get/set` と `indexOf/includes`（`array_search_suffix`）は正しく消費している。しかし**高階関数群が未移行**で、`src/internal/hir/lowering/expr_member.cpp` の各サイトが `elem_is_i64 ? "_i64" : "_i32"` の手書き選択を行い、ランタイム（`runtime_hof_core.inc`）も `_i32`/`_i64` しか実装していない。型チェッカ（`method.cpp`）も `map/filter/reduce/first/last/sort/sortBy` に要素型ゲートを設けないため、`double[]`/`string[]`/`Point[]` レシーバは**型検査を通過してからネイティブで無診断に誤コンパイル/クラッシュ**する（JSは構造的lowerで正しい）。

- **E1 [silent/crash] `reduce`**: `double[].reduce(...)` がネイティブでLLVM検証失敗（`i32` reduce に `double` を渡す）。加えてchecker（`method.cpp:581` 付近）が戻り型を常に `int` に固定するため `string cat = ss.reduce(...)` は両backendで拒否。
- **E2 [silent/crash] `first`/`last`/`find`**: `Point[].last()` が4バイトだけ読み `(x,0)` かつ要素誤り、`string[].first()` は無出力abort、`double[].first()` は `0`（`expr_member.cpp:587/620/637` 付近）。
- **E3 [silent] `map`**: `Point[].map(p=>p.y)` がネイティブで `25560128,...`（i32ストライドで8バイト構造体を跨ぐ）、`double[].map` も誤値、`string[].map` はabort（`expr_member.cpp:494` 付近）。
- **E4 [silent] `filter`**: 非int要素で件数・要素が壊れる（`Point[]` が1件・y消失、`double[]` が0件）（`expr_member.cpp:510` 付近）。
- **E5 [silent] `sortBy`**: 構造体要素で比較関数が効かない（`__builtin_array_sortBy` がi32形状）。`sort()`/`reverse()` はスライスヘッダの `elem_size` を読むため構造体/doubleでも正しく、これが本来あるべき汎用形。
- **E6 [reject/非対称] `some`/`every`/`findIndex`** はchecker（`method.cpp:542-580`）で整数要素限定。`filter`/`map` は同じ `string[]` レシーバを受理して誤コンパイルするのに、`some` は拒否する、という能力面の不整合。
- （補足）`.map((int[] row)=>{...})` のように**ラムダ仮引数に配列型**が書けない（`Expected expression`）ため `int[][]` の行コールバックが書けない。

**処置記録（E1〜E6）**: HIR loweringの各HOFサイト（map/filter/reduce/forEach/some/every/findIndex/first/last/find/sortBy、スライスのfirst/last）の手書きi32/i64二択を、`slice_dispatch.hpp` の `slice_scalar_info` から導出する正準ヘルパ `array_hof_suffix` へ一本化し、ランタイム（`runtime_hof_core.inc`/`runtime_slice_core.inc`）へi8/i16/f32/f64変種（クロージャ版含む80関数）と `builtin_registry.hpp` の宣言行を追加した。固定長配列のsort/reverseは `cm_array_to_slice` でスライスへ変換し、符号・浮動小数・文字列をヘッダのelem_sizeで正しく扱うスライス汎用ランタイム（`cm_slice_sort_*`/`cm_slice_reverse`）へ相乗りさせ、i32形状の `__builtin_array_sort/reverse` 依存を廃止した（スライスsortの手書きswitchも `slice_scalar_sort_suffix` へ置換）。checkerに要素型ゲートを追加し、非スカラ要素（構造体・文字列等）のHOFは構造的loweringを行うjs/ts系ターゲット（`set_structural_array_lowering`）でのみ許可、native/jit/wasmでは `TcArraySearchUnsupportedElem` 診断で停止する（E6の能力非対称も同判定へ統一）。E1のreduce戻り型はコールバックのアキュムレータ型（無ければ初期値型→要素型）に修正し、要素×アキュムレータの未提供な幅組み合わせは専用診断 `TcArrayReduceUnsupportedAcc` で停止する。クロージャ正規化（`normalizeHofClosureArgs`）の要素型二択も幅サフィックス導出へ拡張し、`_i32_acc64` のサンクacc幅誤り（i32サンクと(i64,i32)コールバックの食い違い）も同時修正。あわせて調査で発覚した2件——ラムダキャプチャ解析が `as` キャスト・構造体/配列リテラル内の識別子を走査せず、キャスト内でだけ参照される外側変数が無診断ゼロ値になる漏れ（`lambda.cpp`）と、jsの `cm_slice_get_f32` 未対応（float要素の添字読みがundefined）——も修正した。回帰テスト: `tests/common/arrays/higher-order/{elem_widths,sort_widths,closure_widths,unsupported_elem}`。ラムダ仮引数の配列型（補足）は構文未対応のため未処置。

## F. 数値縮小診断の文脈差【処置済み】

同一の縮小変換（例 `int`→`tiny`。変換の**挿入**は全文脈で行われ値は切り詰まる）に対し、縮小**警告**が文脈で出たり出なかったりする。checker側の問題で全backend共通。

- 警告が出る: let初期化・代入・return・三項の結果。
- 警告が出ない（silent）: **関数引数・配列要素リテラル・構造体フィールド初期化**。
- 「ある文脈では黙って受理し別文脈では警告」という典型的な非汎用。[coercion-driver-unification](coercion-driver-unification.md) の方針4（受理と挿入の同表化）に一部関連するが、この文脈別の警告欠落は現状の欠陥として未記載。

**処置記録（F系）**: 正準の `check_numeric_conversion_policy`（Z5）を関数引数（`call/function.cpp`）・配列要素リテラル（`stmt.cpp` のlet初期化要素ループ）・構造体フィールド初期化（`expr/primary.cpp`）の3文脈へ適用し、let/代入/return/三項と同一規則（適合リテラル免除・`uint/usize→int` 免除・`--strict` でエラー昇格）で診断するようにした。掃引で発覚した既存の暗黙縮小4箇所——stdlibの `vector.cm` の `memcpy(…, long)`・`io/console/input.cm` の `malloc(long)`・テスト2件の同型 `malloc(long)`——は従来挙動と同じ切り詰めを `as int` で明示化した。回帰テスト: `tests/regression/narrowing_diag_test.cpp`（cases/narrowing_diag/、3文脈の警告発火と適合リテラル無診断）。

## G. バックエンド分岐（同一入力・異結果）

フロントで一本化されず各backendが独自実装するため、同じプログラムが結果分岐する。

- **G1 [silent] JSで `ulong`>2^63 の補間が符号付きになる**（`src/internal/codegen/js/runtime.cpp:221` の `String(val)`）。`ulong a=1e19; println(a)` は正しいが `println("{a}")` がJSで `-8446744073709551616`、`{a/b}` も `14223372036854775808` と誤る。radix指定時のみ `BigInt.asUintN(64,...)` を適用しており、無指定の補間分岐が符号正規化を欠く。補間の値リストが符号情報を持たないため（native/interpは型付きフォーマッタを選ぶ）。→ **解消済みを確認**: 補間・書式指定子の全バックエンド統一（R20）以降、`{a}`・`{a/b}`・`ulong`最大値・`0-1` ラップ・`{a:x}` のいずれもJSとnativeが一致することを実測で再確認した（追加修正は不要だった）。
- **G2 [silent] JSが `x as typeof(intVar)` を解決しない**（native/wasmは切り詰める）。B4の裏返し。
- **G3 [diag/silent] 集約の補間フォーマッタが未統一**。`int[3]` の `"{a}"` が interp/native `{}`・JS `1,2,3`、構造体は interp/native 空・JS `[object Object]`。共有規則が無く各backendが即興。
- **G4 [reject] 間接呼び出しのコード生成欠落**。`getf()(2,3)`（即時に返した関数ポインタの呼び出し）や `fs[0](2,3)`（関数ポインタ配列の呼び出し）がネイティブで `Symbols not found: [ _<indirect> ]`。型は通る。一旦変数に束ねれば動く。既知の関数ポインタ配列非対応（[array-literal-element-type-checking](array-literal-element-type-checking.md) のReq5）と同族。

## その他の位置限定の非汎用

- ~~`for-in` が配列/スライス限定で `string` を拒否（`for (char ch in s)` がエラー）。範囲は `range(a,b)`。~~ → **処置済み**: 型チェッカ（`check_for_in`）で `string` を反復対象として受理し要素型を `char` にした。HIR loweringは配列と同じインデックスループへ脱糖し、境界は `__builtin_string_len`（バイト長。添字 `s[i]` がバイト単位のため）、要素取得は `s[i]` の正準lowering `__builtin_string_charAt` を用いる（`for (char ch in s)` が `s[i]` と同じバイト意味論で反復する。全バックエンド一致）。回帰テスト: `tests/common/control-flow/loops/forin_string.cm`。
- ジェネリック型引数に関数ポインタ型を渡すと（`Box<int*(int,int)>`）型は通るがフィールド解決で `Unknown method 'v'`（モノモーフ化/フィールド解決層）。
- 関数ポインタを直接呼ぶ式（G4）と、`Type<Args>{...}` 構築式（`<`/`>`が比較演算子に解釈される）は一般に未対応。

---

## 既存の統一努力との関係

本調査の多くは、既に進行中の「複製排除」系設計文書と同じ精神の**未カバー箇所**である。重複を避けるため対応関係を記す。

- A系（型文法の複製）: [type-resolution-simplification](../../archive/v0.17.0/architecture/type-resolution-simplification.md) が場所解決・スライス表引き・期待型API化・補間の脱糖を扱ったが、**型文法そのものの呼び出し側複製（is_type_start / sizeof判定 / as-is配列サフィックス / paren・generic-arg の非union再帰）は未統一**。
- B系（typeof型）: [parenthesized-type-and-typeof-cast](parenthesized-type-and-typeof-cast.md) に既述の残課題（`ast::Type` に被演算式を持たせる）。
- C系（期待型伝播）: `infer_type_expecting`/`propagate_literal_expected_type` の**透過ノード（三項・match・ジェネリック置換）への拡張が未完**。
- E系（HOFディスパッチ）: [method-resolution-unification](method-resolution-unification.md) のビルトインラダー表駆動化と方向性は同じだが、**高階関数の `slice_dispatch.hpp` 移行と要素型ゲートは別作業**。
- F/G1（変換・整形）: [coercion-driver-unification](coercion-driver-unification.md) の受理/挿入同表化に隣接するが、**文脈別警告欠落**と**JS無指定補間の符号処理**は個別対応が必要。
- G4（間接呼び出し）: 関数ポインタ呼び出し/配列のコード生成は未対応既知項目。

## 推奨する統一の方向（優先度順）

1. **[Critical] E系: 高階関数を `slice_dispatch.hpp` へ移行し、checkerに要素型ゲートを追加**（無診断の誤コンパイル/クラッシュを解消）。`sort`/`indexOf` が既にやっている汎用形へ揃える。→ **処置済み**（E節の処置記録参照）
2. **[Critical] C系: 期待型伝播を透過ノードへ拡張**（三項・matchアーム・ジェネリック実引数置換）。無名リテラルの無診断ゼロ化を解消。→ **処置済み**（C節の処置記録参照）
3. **[High] A系: 型文法の呼び出し側複製を正準へ集約**。`is_type_start` を `parse_type` と整合させる（または投機パース化）、sizeof/alignof/as-is が同じ型パーサ経路を使う、paren・generic-arg を `parse_type_with_union` に。合わせてsizeof/alignofに変数救済を通す。→ **A2〜A6は処置済み**（A節の処置記録参照。A2=sizeof被演算子判定の共通化・変数被添字の要素型サイズ救済、A3=as/is配列サフィックスの正準化と配列as診断、A4=括弧型のunion受理、A5=ジェネリック引数のunion受理、A6=alignof変数救済）。残るA1（`is_type_start`の`typeof`/`(`拡張）はB系のtypeof解決が前提のため据え置き。
4. **[High] G1: JSの無指定補間に符号情報を渡し `asUintN` を適用**（silentな値破壊）。A2/A6/B1/B2のsizeof/typeof/alignof無診断誤値もここで一掃。→ **G1は解消済みを確認**（R20の補間統一で修正済み。G節参照。sizeof/typeof/alignofの無診断誤値はA/B系の残課題）
5. **[Medium] D系: 補間を本物の式パーサへ**（digit/`:`/配列リテラル始まりの制限撤廃）。[type-resolution-simplification](../../archive/v0.17.0/architecture/type-resolution-simplification.md) の「補間のパース時脱糖」を式全域へ。→ **処置済み**（D節の処置記録参照。MIRの先頭文字ホワイトリスト撤廃でcheckerの式パーサ受理へ統一・三項コロンの誤認をカウンタで解消・MIRの二重走査を統合）。
6. **[Medium] F系: 縮小診断を全変換文脈で一律化**（引数・配列要素・フィールド初期化）。→ **処置済み**（F節の処置記録参照）
7. **[Medium] B系: `typeof` 型に被演算式を持たせて解決**（宣言型・仮引数型・キャスト結果型・JSでの解決を一括で正す）。

## 検査に使った再現プローブ

全プローブはセッションのscratch（`scratchpad/audit/{a,b,c,d,e,f,verify}`）に置いた（リポジトリ非追跡）。主要所見（A1-A6・B1-B4・C1-C2・D1-D2・E1-E5・F・G1）は本文書作成時に個別再現確認済み。
