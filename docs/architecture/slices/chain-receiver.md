# チェーンレシーバの解決

`rows[0].push(x)`や`make_slice().len()`のように、添字式や関数呼び出しの結果をレシーバとするメソッド呼び出しは、MIR loweringの共通ヘルパ`resolve_receiver_place`がレシーバを「場所（`MirPlace`）」へ解決することで動作する。変異系メソッド（push/pop/delete/clear）の効果を格納中の実体へ確実に届けるため、固定長配列要素はindexプロジェクション、スライス要素は格納中ヘッダへの参照（`cm_slice_get_subslice_ref`）で場所化し、書き戻し（write-back）を不要にする設計を取る。場所を解決できないレシーバは黙って捨てず必ず診断で停止する。

## 概要

スライスのメソッド呼び出しはHIR段で`__builtin_slice_*`呼び出しへ脱糖され、レシーバ式は第0引数`args[0]`として任意のHIR式（`HirVarRef`/`HirMember`/`HirIndex`/`HirCall`）のまま保持される（`src/internal/hir/lowering/expr_member.cpp:589-651`）。MIR段の`try_lower_slice_builtin`（`src/internal/mir/lowering/expr_slice.cpp:19`）が各builtinを処理し、レシーバの場所解決は全builtinが単一のヘルパ`resolve_receiver_place`（`src/internal/mir/lowering/expr/access.cpp:261-380`）へ委譲する。

この設計の核心は2点ある。第一に、レシーバ解決のロジックをbuiltinごとに複製せず1箇所へ集約することで、対応可能な式種別（VarRef/Member/Index/Call）の網羅性をヘルパ1本で保証する。第二に、変異が「格納されている実体」へ届く場所化の手段を要素の格納表現ごとに選ぶことで、コピーへの変異が捨てられる（＝呼び出しが黙って効かない）ことを構造的に防ぐ。

## データ構造とアルゴリズム

### レシーバ式種別ごとの解決戦略

`resolve_receiver_place`は式種別で分岐する（`access.cpp:261-380`）。

| レシーバ式 | 例 | 解決方法 | 書き戻し |
|---|---|---|---|
| `HirVarRef` | `v.push(x)` | 変数のローカルをそのまま場所に（access.cpp:266-274） | 不要（直接場所） |
| `HirMember` | `self.items.push(x)` | `get_member_place`でフィールドプロジェクション連結（access.cpp:276-278, :382-461） | 不要（直接場所） |
| `HirIndex`（固定長配列要素） | `arr[i].push(x)` | ベースを再帰解決しindexプロジェクションを追加（access.cpp:355-376） | 不要（直接場所） |
| `HirIndex`（スライスの内側スライス要素） | `rows[0].push(x)` | `cm_slice_get_subslice_ref`で格納中ヘッダへの参照を取得（access.cpp:294-353） | 不要（参照経由でヘッダを直接変異） |
| `HirIndex`（スライスの構造体blob要素） | — | 場所化しない（値コピーになり変異が失われるため。access.cpp:301-304） | 呼び出し側が診断で停止 |
| `HirCall`（読み取り系のみ） | `make_slice().len()` | 一時ローカルへ実体化して読む（expr_slice.cpp:33-39） | 不要（読み取りのみ） |
| `HirCall`（変異系） | `make_slice().push(x)` | 解決失敗として扱う | 診断で停止（一時への変異は無意味） |

HIR脱糖の例（`src/internal/hir/lowering/expr_member.cpp:607-617`。レシーバ`obj_hir`は任意式のまま第0引数になる）:

```cpp
if (mem.member == "push") {
    auto hir = std::make_unique<HirCall>();
    hir->func_name = "__builtin_slice_push";
    hir->args.push_back(std::move(obj_hir));  // レシーバを第0引数へ
    for (auto& arg : mem.args) {
        hir->args.push_back(lower_expr(*arg));
    }
    return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
}
```

MIR段でのレシーバ解決と、場所を持たない読み取り系レシーバの一時実体化（`src/internal/mir/lowering/expr_slice.cpp:28-39`）:

```cpp
// スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。H10）
MirPlace slice_place{0};
hir::TypePtr slice_type = nullptr;
bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

// 場所を持たない式（make_slice().len() 等の呼び出し戻り値）は一時ローカルへ
// 実体化して読み取る（H10: 従来は診断なしで空tempを返し黙って欠落していた）
if (!resolved) {
    LocalId materialized = lower_expression(*slice_expr, ctx);
    slice_place = MirPlace{materialized};
    resolved = true;
}
```

### 混合チェーンと再帰

`HirIndex`分岐はベースオブジェクトを`resolve_receiver_place`自身で再帰的に解決するため（`access.cpp:280-284`）、`g.cells[i].push(v)`のような「メンバ→添字→メソッド」の混合チェーンは、`get_member_place`が作ったフィールド場所を基点に添字を連結して解決される。ネストした添字（`rows[i][j].push(x)`）は`indices`または単一`index`を順に辿るループで処理される（`access.cpp:306-349`）。

### スライス要素レシーバの参照場所化

多次元スライスでは内側スライスの`CmSlice`ヘッダが外側スライスのデータバッファへインライン格納されている（[ランタイム表現](runtime-representation.md)参照）。そこで`cm_slice_get_subslice_ref`（`src/internal/codegen/llvm/native/runtime_slice.c:792-800`）は格納中ヘッダのアドレスをそのまま返す:

```c
// 内側スライスヘッダへの参照を返す（コピーしない）。
// 添字レシーバ（rows[0].push(x)等）の変異を格納中のヘッダへ反映するために使う。
// 返したポインタは外側スライスのdataバッファ内を指すため、外側のpush/growで無効化される。
// 取得直後のメソッド呼び出しにのみ使用し、保持しないこと
void* cm_slice_get_subslice_ref(void* slice_ptr, int64_t index) {
    ...
    CmSlice* slice_array = (CmSlice*)slice->data;
    return &slice_array[index];
}
```

この参照をレシーバ場所とすることで、push/pop/delete/clear/len/capの変異・読みが格納中のヘッダへ直接作用し、明示的な書き戻し処理が不要になる（`access.cpp:295-298`のコメント）。読み取り専用の添字アクセス（`rows[0][1]`のような値の取り出し）は逆にコピー版の`cm_slice_get_subslice`（`runtime_slice.c:802-827`）を使い、`lower_index`が中間レベルをsubslice連鎖で辿って単一添字読みへ還元する（`access.cpp:608-659`）。

### 呼び出し戻り値レシーバと単一評価

`make_slice().len()`のような呼び出し戻り値レシーバは場所を持たないため、`lower_expression`で一時ローカルへ実体化してから読む（`expr_slice.cpp:33-39`）。これはlen/cap（読み取り系）に限った救済であり、変異系builtinでは一時への変異が捨てられるだけなので実体化せず診断で停止する。

組み込みResult/Optionメソッドのチェーン（`map.get(k).is_none()`等）は別の経路でケアされる。これらの脱糖はレシーバをAST複製するため、呼び出しを含むレシーバをそのまま複製すると多重評価になる。型チェック前のASTプリパス`match_hoist`（`src/internal/types/checking/match_hoist.cpp:1-16`）が呼び出しを含むレシーバを文直前の一時変数へ退避し、単一評価を保証する。ループ条件・短絡演算の右辺・deferなど評価回数や評価タイミングが変わる位置では退避しない（`match_hoist.cpp:12-16`）。

### 解決失敗時の診断（黙殺禁止）

全builtinで、`resolve_receiver_place`が失敗した場合はError診断を出して停止する。push: `expr_slice.cpp:159-163`、pop: `:214-217`、delete: `:254-257`、clear: `:291-294`。診断を出さずに空の一時値を返すことは禁止である。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/mir/lowering/expr/access.cpp:261-380 | `resolve_receiver_place`（レシーバ場所解決の単一ヘルパ） |
| src/internal/mir/lowering/expr/access.cpp:382-461 | `get_member_place`（フィールドチェーンの場所化、selfポインタのDeref挿入は:414-421） |
| src/internal/mir/lowering/expr/access.cpp:608-659 | 多次元スライスの多重添字読みのsubslice連鎖への還元 |
| src/internal/mir/lowering/expr_slice.cpp | スライスbuiltinのlowering（全builtinがヘルパを呼ぶ。失敗時診断） |
| src/internal/hir/lowering/expr_member.cpp:589-651 | メソッド構文から`__builtin_slice_*`への脱糖（レシーバを`args[0]`へ） |
| src/internal/codegen/llvm/native/runtime_slice.c:792-827 | `cm_slice_get_subslice_ref`（参照）と`cm_slice_get_subslice`（コピー） |
| src/internal/types/checking/match_hoist.cpp | 呼び出しを含むレシーバの一時変数退避（単一評価の保証） |
| src/internal/codegen/llvm/core/runtime/builtins.cpp:352 | `cm_slice_get_subslice(_ref)`のLLVM宣言登録 |

## 落とし穴とケア

- 防ぐバグのクラス（黙って欠落）: レシーバ解決が変数参照とメンバに限定されていた場合、`m[0].push(x)`はコンパイルは通るのに文ごと消えるという最悪の失敗様式になる。この「黙殺」を防ぐため、(1) 解決対象の式種別を1本のヘルパへ集約し、(2) 解決失敗は必ず診断でハードエラーにする、という二重の防御を維持すること。新しいbuiltinや新しいレシーバ式種別を追加するときも、builtin個別のVarRef/Member分岐を書かず`resolve_receiver_place`へ寄せること。
- 防ぐバグのクラス（コピーへの変異）: レシーバを`lower_expression`で安易に実体化すると読み取りは動くが変異が捨てられる。変異系builtinのレシーバ実体化は禁止であり、場所化できない場合は診断で止める。スライスの構造体blob要素（`points[0]`が構造体の場合）が場所化を拒否するのはこのためである（`access.cpp:301-304`）。
- 防ぐバグのクラス（多重評価）: 呼び出しを含むレシーバの脱糖でAST複製を行うと、`parse_int(s).unwrap_or(0)`のタグ比較とペイロード取得が別々に評価され誤った値を返す。呼び出しを含むレシーバは`match_hoist`の退避対象であることを保ち、退避してはいけない位置（ループ条件等）のリストも同時に維持すること。
- `cm_slice_get_subslice_ref`が返す参照は外側スライスのデータバッファ内を指すため、外側への`push`が再確保を起こすと無効化される。loweringは「参照取得→直後のメソッド呼び出し」の1ステップでのみ使用しており、この参照を複数文にまたがって保持するコードを生成してはならない（`runtime_slice.c:788-791`）。
- 固定長配列要素とスライス要素で場所化の手段が異なる（indexプロジェクション vs subslice_ref呼び出し）。判別は「`array_size`の有無と次元値」で行っており（`access.cpp:289-293`）、スライスの`dimensions`に0が入るケースがあるため両方を見る必要がある。
- 回帰テスト: `tests/common/chaining/index_receiver_method_test.cm`（添字レシーバのpushと反映確認）、`call_return_receiver.cm`（呼び出し戻り値レシーバ）、`mixed_chain_receiver_test.cm`（`g.cells[i].push(v)`混合チェーン）。マップの呼び出しレシーバチェーンは`tests/common/collections/hashmap_option_get_test.cm`等が検証する。

## 関連資料

- [スライスのランタイム表現](runtime-representation.md) — 内側スライスヘッダのインライン格納と`elem_size = sizeof(CmSlice)`
- [チェーンレシーバ解決設計文書（archive）](../../archive/v0.17.0/chain-receiver-resolution.md) — 解決対象の一般化と黙殺禁止の設計判断
- [コレクションのOption返しAPI設計文書（archive）](../../archive/v0.17.0/collections-option-api-and-errors.md) — 呼び出しレシーバの多重評価問題と`match_hoist`退避の経緯
