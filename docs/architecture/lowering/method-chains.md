# メソッドチェーン・式チェーンのlowering

`p.translate(1, 1).translate(2, 2).x`のようなメソッドチェーンは、専用のチェーンノードを持たず、後置式の再帰下降パースが作る左結合の入れ子ASTを各段が内側から外側へ順に処理することで動作する。構文段は`parse_postfix`の単一ループでチェーンを`MemberExpr`/`CallExpr`/`IndexExpr`の入れ子に畳み、型検査段は`infer_member`がレシーバ型を再帰推論してメソッド表から戻り値型を段ごとに伝播し、HIR段は各メソッド呼び出しを`Type__method(self, args...)`形式の`HirCall`へ脱糖し、MIR段はレシーバを「場所（`MirPlace`）」へ解決するか、場所を持たない呼び出し戻り値レシーバを一時ローカルへ実体化して繋ぐ。チェーンが生む中間一時（文字列連結結果等）は文単位の一時スコープが文末に解放する。

## 概要

チェーンは4つの段がそれぞれ独立した汎用機構で処理し、どの段にも「チェーン専用」の特別なデータ構造は存在しない。

1. 構文段: `parse_postfix`（`src/internal/syntax/parser/expr/postfix.cpp:17`）が`while (true)`ループ（postfix.cpp:22）で後置演算子を左から右へ消費し、`a.f().g()[i].h`を外側ほど後段になる入れ子ASTにする。
2. 型検査段: `infer_member`（`src/internal/types/checking/call/method.cpp:37`）が冒頭で`infer_type(*member.object)`（method.cpp:46）を再帰呼び出しするため、チェーンの型は最内レシーバから外側へ自然に伝播する。
3. HIR段: `HirLowering::lower_member`（`src/internal/hir/lowering/expr_member.cpp:44`）がメソッド呼び出しを`mangle::method_name`による`Type__method`名の`HirCall`へ脱糖し、レシーバ式を第0引数に据える（expr_member.cpp:1070-1119）。
4. MIR段: `ExprLowering::lower_call`（`src/internal/mir/lowering/expr_call.cpp:20`）が構造体レシーバへの参照渡しを構成し、`lower_member`（`src/internal/mir/lowering/expr/access.cpp:17`）とスライスbuiltin（`src/internal/mir/lowering/expr_slice.cpp:19`）がチェーン中間結果の実体化を行う。

添字式をレシーバとする`m[0].push(x)`の場所解決（`resolve_receiver_place`）の詳細は[チェーンレシーバの解決](../slices/chain-receiver.md)が担当し、本書はチェーン全体を貫く段横断の設計を扱う。

## データ構造とアルゴリズム

### 構文段: 単一ループによる左結合チェーン構築

後置式は優先順位連鎖`parse_cast_expr`→`parse_unary`→`parse_postfix`の最深部にあり（`src/internal/syntax/parser/expr/binary.cpp:328-401`）、`parse_postfix`はループ1周ごとに直前までの式`expr`を新ノードの子として包み直す。メンバアクセスで`(`が続く場合はその場で`MemberExpr`の`is_method_call = true`を立てて引数をパースする（postfix.cpp:180-202）ため、フィールドアクセスとメソッド呼び出しはAST段階で既に区別されている。

```cpp
// postfix.cpp:181-202（抜粋）
if (consume_if(TokenKind::LParen)) {
    auto mem_expr = std::make_unique<ast::MemberExpr>(std::move(expr), member);
    mem_expr->is_method_call = true;
    ...
    expr = std::make_unique<ast::Expr>(std::move(mem_expr));
}
```

関数呼び出し（postfix.cpp:75-96）・添字/スライス（postfix.cpp:99-160）・`->`（postfix.cpp:163-177、`Deref`単項を挿入して`.`へ正規化）も同じループが処理するため、`a.f()[0].g()`のような混在チェーンも追加機構なしで左結合に組み上がる。

### 型検査段: 内側から外側への型伝播とメソッド解決

`infer_type`のディスパッチ（`src/internal/types/checking/expr/primary.cpp:38-39`）が`MemberExpr`を`infer_member`へ委譲し、`infer_member`はレシーバ型の文字列名で`type_methods_`表を引いてメソッドを解決する（method.cpp:86-131）。解決結果の`method_info.return_type`がこの段の式型となり（method.cpp:128）、それが次段のレシーバ型になることでチェーン全体の型が確定する。引数は`param_types`との互換検査（method.cpp:111-121）、引数個数はデフォルト引数を考慮した`method_arity_error`（method.cpp:21-33）で検査する。配列・文字列のビルトインメソッドは`infer_array_method`/`infer_string_method`（method.cpp:336, :624）へ分岐し、ジェネリック構造体は戻り値型へ型引数を代入してから返す（method.cpp:134-164）。

### HIR段: `Type__method(self, args...)`への脱糖

`lower_member`はまず`lower_expr(*mem.object)`でレシーバをHIR化し（expr_member.cpp:52）、レシーバの静的型名から`mangle::method_name(method_type_name, mem.member)`で呼び出し名を作る（expr_member.cpp:1071、`src/internal/base/mangle.hpp:15-17`）。レシーバ式は形を問わず（変数・フィールド・添字・別のメソッド呼び出しの戻り値）そのまま第0引数へ入る（expr_member.cpp:1112）。

```cpp
// expr_member.cpp:1070-1119（抜粋）
auto hir = std::make_unique<HirCall>();
hir->func_name = mangle::method_name(method_type_name, mem.member);
...
hir->args.push_back(std::move(obj_hir));   // self（任意のレシーバ式）
for (auto& arg : mem.args) {
    hir->args.push_back(lower_expr(*arg));
}
return std::make_unique<HirExpr>(std::move(hir), type);  // typeは型検査済みの戻り値型
```

スライスのメソッドは`__builtin_slice_*`呼び出しへ（expr_member.cpp:589-651）、`Result`/`Option`のメソッドはタグ比較・ペイロード抽出式へ（expr_member.cpp:96-160）、ジェネリック型は`Vector<int>`→`Vector__int`のマングリングへ（expr_member.cpp:951-1006）それぞれ脱糖されるが、いずれも「戻り値型を持つHIR式」になるためチェーンの次段からは同じに見える。

### MIR段: レシーバの参照渡しと呼び出し戻り値レシーバの一時実体化

`lower_call`は関数名の`Type__`接頭辞と`struct_defs`でメソッド呼び出しを判定し（expr_call.cpp:43-51）、構造体レシーバを値コピーではなく参照で渡す。レシーバ式の形で3経路に分かれる。

- 変数レシーバ（`sb.append(x)`）: 元の変数ローカルへの`Ref`を作って渡す（expr_call.cpp:167-179）。コピーが発生しないため、メソッド内の`self`への変異は呼び出し元の変数へ直接届く。
- `->`レシーバ（`ptr->method()`）: ポインタをデリファレンスした一時コピーへの参照を渡し、呼び出し後に`*ptr`へ書き戻す（expr_call.cpp:106-164, :368-380）。
- それ以外（チェーン中間の呼び出し戻り値等）: `lower_expression`で式を評価して一時ローカルへ実体化し、その一時への`Ref`を渡す（expr_call.cpp:181-186）。これが「呼び出し戻り値レシーバの一時実体化」であり、`p.translate(5,5).translate(10,10)`の2段目はこの経路を通る。

フィールドアクセスがチェーン末尾に来る場合（`p.translate(100, 200).y`）は、MIR側`lower_member`がベース式を`lower_expression`で一時へ実体化してから（access.cpp:46）フィールドプロジェクションで読み出す（access.cpp:250-255）。スライスbuiltinのレシーバは`resolve_receiver_place`で場所解決を試み、場所を持たない`make_slice().len()`は同様に一時へ実体化して読む（expr_slice.cpp:31-39）。

### `return self`ビルダーチェーンが同一オブジェクトへ作用する仕組み

implメソッド内の`self`は、呼び出し側が参照を渡す前提の値型ローカルとして登録され（`src/internal/mir/lowering/impl.cpp:99-103`）、実行時にはレシーバの場所を指すポインタが入っている。この非対称を繋ぐのが2つの機構である。

1. メソッド内の`self.field`アクセスは、MIRローカル型がポインタであることを検出して`Deref`プロジェクションを自動で挟む（access.cpp:56-61, :94-96）。したがって`self`経由の変異は呼び出し元の実体へ直接届く。
2. 戻り値型が構造体のメソッドで`return self`すると、戻り値ローカルはポインタ・戻り値型は構造体という不一致が生じるため、`lower_return`がpointee一致を確認して`Deref`を挟み、変異反映済みの構造体「値」を返す（`src/internal/mir/lowering/stmt/control.cpp:25-42`）。この検査がないとポインタのビット列がそのまま構造体値として返る誤コンパイルになる。

チェーン2段目以降のレシーバはこの戻り値を一時実体化したコピーだが、`StringBuilder`のように状態をランタイムハンドル（`libs/std/strings/builder.cm:18-20`の`long handle`）で持つビルダーでは、コピーもハンドルを共有するため全段が同一のランタイムオブジェクトへ作用する。一方、状態を構造体フィールドに直接持つビルダー（`libs/web/html.cm:56-89`の`Html`）は`self`を変異させず毎段新しい値を返す純値スタイルを取っており、どちらのスタイルでもチェーンの意味論が壊れない。

### MIR段: 文字列連結チェーンの平坦化

`a + b + c + d`のような`string`の`Add`チェーンは、`lower_binary`冒頭で左結合の`HirBinary`木を再帰的に平坦列へ展開し（`src/internal/mir/lowering/expr/binary.cpp:17-39`）、3要素以上なら`cm_string_concat3`/`cm_string_concat4`へ畳み込む（binary.cpp:44-108）。素朴に2項ずつ連結すると3要素で2回・4要素で3回の中間バッファ確保が起きるところを、1回の確保にまとめる。5要素以上は「前結果+3要素」単位で反復し（binary.cpp:73-106）、非文字列オペランドは`convert_to_string`で文字列化してから列に加える（binary.cpp:60-67）。2要素のみの連結は従来どおり`cm_string_concat`1回で処理する（binary.cpp:724-772）。

```cm
// 3要素以上の連結は1回のランタイム呼び出しに畳まれる
println(a + b + c);          // cm_string_concat3(a, b, c)
println(a + b + c + d);      // cm_string_concat4(a, b, c, d)
println(a + (b + c) + d);    // 括弧の内側も再帰展開されて同じく1回
```

### 一時オブジェクトの寿命: 文一時スコープとの関係

チェーンが生む中間結果（concat結果・`cm_*_to_string`結果・map/filter結果スライス）は、生成時に`note_string_temp`/`note_slice_temp`で文単位一時スコープへ登録される（binary.cpp:101-103, :768-769、`src/internal/mir/lowering/context.hpp:158-183`）。スコープはlet/assign/式文のloweringを囲んで開始・終了し（`src/internal/mir/lowering/stmt/lower.cpp:24-35, :81-88`）、終了時に文のMIR範囲をスキャンするエスケープ解析（`src/internal/mir/lowering/stmt/temp_drop.cpp:106-195`）が、所有権が移動しなかった一時へ`cm_string_free`/`cm_slice_free`を発行する。`cm_string_concat3/4`自体は引数を保持しない呼び出し先のホワイトリストに載っているため（temp_drop.cpp:21-37）、連結チェーンの中間一時は次の連結に消費されても文末で正しく解放される。三項演算子や短絡評価の右辺など条件付きに実行される腕の内側は腕スコープで別管理し、未実行経路の一時を文末で解放してしまう事故を防ぐ（binary.cpp:428-438, :484-494、temp_drop.cpp:199-223）。ユーザー定義デストラクタを持つ値のスコープ終端dropはこの仕組みとは別系統で、[dropパスと所有権](../memory/drop-and-ownership.md)が扱う。

## 実装箇所

| 役割 | ファイル |
|---|---|
| 後置式パース（チェーンのAST構築） | `src/internal/syntax/parser/expr/postfix.cpp:17-267` |
| 優先順位連鎖（unary→postfix） | `src/internal/syntax/parser/expr/binary.cpp:328-401` |
| メソッド解決と段ごとの型伝播 | `src/internal/types/checking/call/method.cpp:37-334` |
| 配列・文字列ビルトインの型推論 | `src/internal/types/checking/call/method.cpp:336, :624` |
| メソッド呼び出しのHIR脱糖（`Type__method`） | `src/internal/hir/lowering/expr_member.cpp:44-1120` |
| マングル規則の一元化 | `src/internal/base/mangle.hpp:15-17` |
| self参照渡し・戻り値レシーバの一時実体化 | `src/internal/mir/lowering/expr_call.cpp:93-190` |
| チェーン末尾フィールドアクセスの実体化 | `src/internal/mir/lowering/expr/access.cpp:17-256` |
| レシーバの場所解決（添字レシーバ含む） | `src/internal/mir/lowering/expr/access.cpp:261-380` |
| `return self`のデリファレンス | `src/internal/mir/lowering/stmt/control.cpp:25-42` |
| 文字列連結チェーンの平坦化 | `src/internal/mir/lowering/expr/binary.cpp:17-110` |
| 文一時スコープとdropパス | `src/internal/mir/lowering/stmt/temp_drop.cpp`・`src/internal/mir/lowering/context.hpp:131-183` |

## 落とし穴とケア

- ポインタ値を構造体値として返すバグ: `return self`の戻り値ローカルはポインタなので、デリファレンスなしで返すとポインタのビット列が構造体値になる。`lower_return`のpointee一致検査（control.cpp:25-42）が防いでおり、self相当のポインタを返す新しい経路を足すときは同じ検査を通すこと。
- チェーンレシーバの黙った欠落: 場所解決できないレシーバを空の一時で代替すると、`make_slice().len()`が常に0を返す誤コンパイルになる。読み取り系は一時実体化（expr_slice.cpp:33-39）、変異系は診断で停止という区別を維持すること（詳細は[チェーンレシーバの解決](../slices/chain-receiver.md)）。
- `ptr->method()`の変異消失: デリファレンスの一時コピーへ変異した結果を書き戻さないと変更が消える。`pending_writeback`（expr_call.cpp:33-39, :368-380）が対で維持すべき不変条件である。
- 条件腕の一時の早期解放: 短絡評価・三項の腕内で生成した文字列一時を文スコープへ登録すると、腕が実行されなかった経路で未初期化ポインタをfreeする。`conditional_expr_depth`と腕スコープ（context.hpp:141-155）の区別を崩さないこと。
- 連結平坦化の意味論維持: `cm_string_concat3/4`への畳み込みは左から右の評価順と結果の同一性を変えてはならず、非保持呼び出しホワイトリスト（temp_drop.cpp:34-36）へ登録しないと中間一時がリークする。
- 回帰テスト: チェーン全般は`tests/common/chaining/`（`true_method_chain.cm`・`call_return_receiver.cm`・`composite_chain.cm`・`mixed_chain_receiver_test.cm`・`index_receiver_method_test.cm`）、連結平坦化は`tests/common/strings/concat_chain_test.cm`、`->`書き戻しは`tests/common/impl/impl_ptr_writeback.cm`が担保する。

## 関連資料

- [チェーンレシーバの解決](../slices/chain-receiver.md) — 添字レシーバ`m[0].push(x)`の場所化と`resolve_receiver_place`の詳細
- [dropパスと所有権](../memory/drop-and-ownership.md) — ユーザー定義デストラクタのスコープ終端dropと一時解放の全体像
- [文字列のランタイム表現](../strings/representation.md) — `cm_string_concat`系が扱うSDSヘッダ方式の文字列表現
- [StringBuilder](../strings/stringbuilder.md) — ハンドル方式ビルダーのABI設計と`+`連結との使い分け
- [シンボルマングリング](../generics/mangling.md) — `Type__method`キー空間と衝突検出
