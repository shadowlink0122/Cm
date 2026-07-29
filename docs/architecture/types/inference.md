# 型推論の設計

Cmの型推論は、双方向型付けや制約ソルバを持たない「式ボトムアップ＋宣言型との突き合わせ」の局所推論である。`TypeChecker::infer_type`が式ツリーを再帰的に辿って各`ast::Expr::type`に推論結果を書き込み、`auto`変数は初期化子の型をそのまま採用し、ジェネリクスは実引数型との構造照合（単一化）で型引数を決める。後段のHIR loweringは推論をやり直さず`expr.type`を信頼して読むため、「型は型検査で一度だけ決まり、全バックエンドが同じ型を見る」ことがパイプラインの不変条件になる。

## 概要

推論のエントリポイントは`TypeChecker::infer_type`（`src/internal/types/checking/expr/primary.cpp:20-299`）で、式の種類ごとに`infer_literal`/`infer_binary`/`infer_ternary`/`infer_match`/`infer_call`等へディスパッチする（メソッド一覧は`src/internal/types/checking/checker.hpp:89-121`）。推論結果は関数末尾で`expr.type`へ書き込まれ（`primary.cpp:279-296`）、パーサが仮の型を設定済みの場合は「推論型の方が情報豊富（名前や型引数を持つ）なときだけ上書きする」規則で統合される。HIR loweringは`TypePtr type = expr.type ? expr.type : make_error();`（`src/internal/hir/lowering/expr.cpp:37`）で読むだけであり、ここがAST型情報の唯一の消費境界である。

## データ構造とアルゴリズム

### リテラルの型付け

`infer_literal`（`primary.cpp:301-340`）が唯一のリテラル型決定点である。整数リテラルはi32範囲（-2147483648〜2147483647）なら`int`、超えたら`long`になり、hex/binary/octalで32bit超の符号なしリテラルは`is_unsigned_literal`フラグを見てINT64_MAX以下なら`long`、それ以上なら`ulong`になる（`primary.cpp:306-329`）。浮動小数リテラルは常に`double`、nullは`void`（ユニオン互換判定用）、文字列は`string`である。リテラルから宣言型への暗黙拡幅は`types_compatible`の「数値型どうしは互換」（`src/internal/types/checking/utils/compat.cpp:255-258`）で受理され、実際の拡幅命令は代入先型に基づいてcodegenが生成する。

### 変数宣言の初期化子からの推論

`check_let`（`src/internal/types/checking/stmt.cpp:238` 以降）がまず初期化式の型`init_type`を推論し（`stmt.cpp:283-305`）、宣言型が`TypeKind::Inferred`（`auto`）なら`init_type`をそのまま変数の型として採用してスコープに定義する（`stmt.cpp:322-330`）。初期化子が無い`auto`は「Cannot infer type for 'auto' variable ... without initializer」のエラーになる（`stmt.cpp:331-334`）。明示型がある場合は`types_compatible(resolved_type, init_type)`で突き合わせ（`stmt.cpp:355`）、例外として`Option<ulong> x = Option::None`のようなenumバリアント初期化子は宣言型へ強制する（`stmt.cpp:356-376`）。配列リテラルは宣言型が配列なら宣言型を優先して`let.init->type`へ直接設定する（`stmt.cpp:285-292`）。

```cm
auto n = 42;              // int（i32範囲の10進リテラル）
auto big = 3000000000;    // long（i32範囲外）
auto h = 0xFFFFFFFFF;     // long（32bit超のhexリテラル）
auto d = 1.5;             // double
long widened = 42;        // 暗黙拡幅（types_compatibleの数値互換）
```

### 式の型伝播と二項演算の合成型

二項演算は`infer_binary`（`src/internal/types/checking/expr/operator.cpp`）が担い、数値どうしの算術は`common_type`で合成する。`common_type`（`compat.cpp:442-457`）は「浮動小数優先（doubleがあればdouble）、整数はサイズの大きい方」という規則である。`+`は文字列連結（どちらかがstringならstring）とポインタ演算（pointer±int=pointer、pointer−pointer=long）を特別扱いする（`operator.cpp:230-276`）。

### 分岐合流の型決定（三項演算子・match式）

三項演算子は`infer_ternary`（`operator.cpp:440-494`）で、両腕の互換性検査の後、整数どうしは「幅の広い方、同幅は符号なし優先」の昇格型を返す。

```cpp
// src/internal/types/checking/expr/operator.cpp:454-455
// 数値同士の腕は昇格型（幅の広い方、同幅は符号なし優先）を返す。
// then側固定だと `false ? 0 : uint値` がint扱いになり、4000000000が-294967296と表示される
```

float/double混在はdouble優先で（`operator.cpp:485-491`）、それ以外はthen側の型を返す。match式は`infer_match`（`src/internal/types/checking/expr/match.cpp:23-167`）で、式形式アームの最初の型を結果型とし、以降のアームは`types_compatible`で突き合わせて不一致をエラーにする（`match.cpp:131-146`）。ブロック形式のみ・混在のmatchは`void`である（`match.cpp:160-166`）。enumバリアント束縛パターンの束縛変数は、enum定義のフィールド型にscrutineeの型引数を代入した精密型で定義される（`match.cpp:69-123`）。

### ジェネリクスの型引数推論（実引数からの単一化）

明示型引数のないジェネリック呼び出しは`infer_generic_call`（`src/internal/types/checking/generic.cpp:16-181`）が実引数型との構造照合で型引数を推論する。照合パターンは3種で、(1)パラメータ型がそのまま型パラメータ`T`なら実引数型を採用、(2)`Box<T>`のようなジェネリック構造体なら同名の実引数型と型引数どうしを対応付け、(3)`Node<T>*`のようなポインタはelement_typeを剥がして(2)を適用する（`generic.cpp:55-119`）。推論は先勝ち（最初に束縛した型を保持）で、結果は`call.inferred_type_args`と順序付きの`call.ordered_type_args`に保存され、後段のモノモーフィゼーションが特殊化キーとして使う（`generic.cpp:121-132`、消費側は[../generics/monomorphization.md](../generics/monomorphization.md)）。推論後に`T: Ord`等の制約を`check_type_constraints`で検査し、不成立は診断になる（`generic.cpp:134-157`）。戻り値型は推論結果で置換して返す（`generic.cpp:159-181`）。

```cm
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}
const int m = max(3, 7);          // 実引数intからT=intを単一化
const double d = max(1.5, 2.5);   // T=double（呼び出しごとに独立に推論）
```

### 戻り値・for-in・ラムダへの伝播

宣言済みの関数戻り値型は`current_return_type_`として保持され、`check_return`が`return`式の推論型と突き合わせる（`src/internal/types/checking/stmt.cpp:455-462`）。ここでも`return Option::None`のようなenumバリアントは戻り値型へ強制するcoercionがある（`stmt.cpp:463` 以降）。for-in文はイテレート対象の配列要素型を取り出し、ループ変数が`auto`（`Inferred`）なら要素型をそのまま採用し、明示型なら互換検査する（`stmt.cpp:669-680`）。ラムダ式は`infer_lambda`（`src/internal/types/checking/expr/lambda.cpp:20-185`）が本体を推論して戻り値型を決める: 式本体ならその式の型（`lambda.cpp:127`）、ブロック本体なら最初の`return`文の値の型（`lambda.cpp:141`）を採用し、パラメータ型と合わせた`Function`型を返す。

```cm
int[3] xs = [1, 2, 3];
for (auto x in xs) {         // xは要素型intで推論される
    println("{x}");
}
const int*(int, int) add = (int a, int b) => { return a + b; };  // 本体のreturnからint戻りを推論
```

### 推論できない場合の診断

推論不能は無音のフォールバックではなく診断になる。代表例は、初期化子なし`auto`（`stmt.cpp:331-334`）、match scrutineeの型不明（`match.cpp:25-28`）、ジェネリック型を型引数なしで宣言に使う`Pair p;`（推論の材料が無いためエラー。`stmt.cpp:253-259`、体系は[../generics/instantiation-diagnostics.md](../generics/instantiation-diagnostics.md)）、三項演算子の腕の非互換（`operator.cpp:450-452`）である。また推論自体は成功するが値が落ちるケースは警告で拾う（整数リテラルの縮小キャスト警告 `primary.cpp:179-218`）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/types/checking/expr/primary.cpp:20-299` | `infer_type`ディスパッチと`expr.type`書き込み規則 |
| `src/internal/types/checking/expr/primary.cpp:301-340` | リテラルの型付け（int/long/ulong判定・double・null） |
| `src/internal/types/checking/stmt.cpp:238-380` | `check_let`（auto推論・宣言型との突き合わせ・enumバリアント強制） |
| `src/internal/types/checking/expr/operator.cpp` | 二項・単項・三項の型合成（`infer_ternary`:440-494） |
| `src/internal/types/checking/expr/match.cpp:23-167` | match式の合流型とアーム互換検査 |
| `src/internal/types/checking/utils/compat.cpp:83-335`・`:442-457` | `types_compatible`（暗黙互換）と`common_type`（数値合成） |
| `src/internal/types/checking/generic.cpp:16-181` | 実引数からの型引数推論（単一化）と制約検査 |
| `src/internal/types/checking/expr/lambda.cpp:20-185` | ラムダ本体からの戻り値型推論と`Function`型の構築 |
| `src/internal/types/checking/stmt.cpp:455-462`・`:669-680` | return値の突き合わせとfor-inループ変数の要素型推論 |
| `src/internal/hir/lowering/expr.cpp:37` | HIR loweringによる`expr.type`の参照（消費境界） |

## 落とし穴とケア

- `expr.type`の上書きは「情報豊富な方を採る」規則（`primary.cpp:281-293`）に従うこと。無条件上書きにするとパーサが設定した精密型（型引数付き）が壊れ、無条件保持にするとname空の仮型が残ってHIRがerror型を見る。
- 分岐合流の型をthen側・先頭アーム固定にしてはならない。三項演算子の昇格規則（`operator.cpp:454-484`）は「`false ? 0 : uint値`が負値表示になる」バグのクラスを防いでおり、回帰は`tests/common/casting/ternary_type_promotion.cm`が固定する。
- 整数リテラルの型判定で`is_unsigned_literal`の分岐（`primary.cpp:311-320`）を外すと、lexerがuint64→int64へbit_castした大きなhexリテラルが負のintに化ける（回帰: `tests/common/types/hex_literal_large.cm`）。
- ジェネリクスの型引数推論は先勝ちで束縛する（`generic.cpp:63-69`）ため、複数引数から矛盾する型が来ても後続束縛は黙って無視される。矛盾検出を強化する場合も`call.ordered_type_args`の順序（`type_params`宣言順）は維持すること。モノモーフィゼーションの特殊化キーがこの順序に依存する。
- 配列リテラルの要素型は先頭要素から決まり（`infer_array_literal` `primary.cpp:342-354`）、空リテラルは`int[0]`にフォールバックする。要素間の合成型計算は行わないため、混在要素の配列は宣言型を明示する運用が前提であり、この関数に合成を足す場合は`common_type`の規則と一致させること。
- HIR以降で型を「推論し直す」コードを書かないこと。型決定点を複数にすると、型検査とcodegenが別の型を見る分裂（例: リテラル幅の食い違いによる符号拡張ミス）が起きる。HIRは`expr.type`を読むだけという境界（`expr.cpp:37`）を保つ。
- 回帰テストの場所: `tests/common/auto/`（auto推論）、`tests/common/types/literal_type_check.cm`・`hex_literal_large.cm`・`mixed_int_types.cm`（リテラル型付けと混在演算）、`tests/common/casting/ternary_type_promotion.cm`・`unsigned_arith_widening.cm`（合流・演算の昇格）、`tests/common/generics/`（型引数推論）。

## 関連資料

- [`as`キャストの設計](casts.md) — 暗黙互換で吸収されない変換の明示手段
- [ユニオン型の設計](union-types.md) — ユニオンへの代入互換（`types_compatible`のユニオン分岐）
- [単相化（モノモーフィゼーション）](../generics/monomorphization.md) — `inferred_type_args`/`ordered_type_args`の消費側
- [インスタンス化の診断](../generics/instantiation-diagnostics.md) — 型引数の個数不一致・引数なし使用などの診断体系
- [コンパイルパイプライン全体像](../pipeline/overview.md) — 型検査→HIRの段構成と`expr.type`の受け渡し位置
