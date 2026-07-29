# RAII・dropパスと所有権（native/jit）

Cmのメモリ解放は、MIR lowering時に静的へ挿入されるデストラクタ呼び出し（RAIIのdropパス）と、コンパイラ生成一時オブジェクトのエスケープ解析付き解放、型チェッカのmove semantics診断の3層で構成される。
ガベージコレクタは持たず、全ての解放位置はコンパイル時に確定し、native/jitでは挿入された`cm_string_free`/`cm_slice_free`/`<型名>__dtor`呼び出しがランタイムの実解放（[allocator.md](allocator.md)参照）に接続される。

## 概要

デストラクタはユーザー定義の`~self()`が`<型名>__dtor`という通常関数としてloweringされ、呼び出し挿入はすべてMIR loweringのスコープ管理が行う。
デストラクタ対象変数は`let`のloweringで型がデストラクタを持つ場合のみ登録され（`src/internal/mir/lowering/stmt/let.cpp:810`の`register_destructor_var`）、登録先は`LoweringContext`が持つスコープごとのスタック`destructor_vars`である（`src/internal/mir/lowering/context.cpp:133`）。
挿入位置は以下の5系統で、いずれも「defer逆順 → デストラクタ逆順 → pop_scope」という同一の順序規約に従う。

| 挿入位置 | 実装 |
|---|---|
| 明示ブロック`{}`の終端 | `src/internal/mir/lowering/stmt/control.cpp:510`（`lower_block`） |
| while/forループ本体の毎周期終端 | `src/internal/mir/lowering/stmt/control.cpp:248`（while）・`:315`（for） |
| break/continueによる途中脱出 | `src/internal/mir/lowering/stmt/lower.cpp:59`（break）・`:73`（continue） |
| return文（全スコープ分を一括） | `src/internal/mir/lowering/stmt/control.cpp:125`（`get_all_destructor_vars`） |
| return文なしの関数終端 | `src/internal/mir/lowering/impl.cpp:360`（`emit_destructors`、定義は`:410`） |

ループ本体の毎周期実行は重要な設計点であり、これが無いとループ内で束縛した`Vector`等が関数終了まで解放されず反復回数に比例してリークする（詳細は[archive設計文書](../../archive/v0.17.0/memory-drop-and-lifetime.md)）。

## データ構造とアルゴリズム

### スコープデストラクタの発行

単一スコープ分の発行は`emit_scope_destructors`（`src/internal/mir/lowering/stmt/scope.cpp:27`）に集約される。
現在スコープの登録変数を逆順に取り出し（`src/internal/mir/lowering/context.cpp:166`）、ジェネリック型名を正規化（`Vector<int>` → `Vector__int`）した上で、変数への参照を引数に`<型名>__dtor`のCall終端を生成する。

```cpp
// src/internal/mir/lowering/stmt/scope.cpp:60
std::string dtor_name = actual_type_name + "__dtor";
// デストラクタ呼び出しを生成（selfはポインタとして渡す）
LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
ctx.push_statement(
    MirStatement::assign(MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local_id}, false)));
```

ネストジェネリック（`Vector<Vector<int>>`）は、デストラクタの特殊化時に要素型のデストラクタ呼び出しループを挿入する（`src/internal/mir/lowering/mono/specialize.cpp:508`で関数名が`__dtor`かつtype_argsありの場合に`element_dtor_name`を組み立て、`:649`以降で要素ごとの呼び出しを生成）。
要素型自身がジェネリック特殊化の場合も基底のジェネリックデストラクタ存在を確認して連鎖させるため、多段ネストの内側要素まで解放が降りる。

### コンパイラ一時オブジェクトのdrop（文スコープ）

`cm_string_concat`・`cm_*_to_string`・スライスの`map`/`filter`等が返す無名の中間結果は`let`束縛ではないためデストラクタ登録経路を持たず、専用の文単位トラッキングで解放する。
文のディスパッチ（`src/internal/mir/lowering/stmt/lower.cpp:28`のlet・`:33`のassign・`:85`の式文）が`begin_stmt_temp_scope`/`end_stmt_temp_scope`で文のMIR範囲を挟み、範囲内で確保された一時をエスケープ解析して解放する（実装は`src/internal/mir/lowering/stmt/temp_drop.cpp`）。

- 一時の登録: 式loweringが確保サイトで`note_string_temp`/`note_slice_temp`（`src/internal/mir/lowering/context.hpp:158`・`:172`）を呼ぶ。登録サイトは文字列連結（`src/internal/mir/lowering/expr/binary.cpp:103`・`:769`）、to_string変換（`src/internal/mir/lowering/expr/cast.cpp:85`）、map/filter結果（`src/internal/mir/lowering/expr_call.cpp:365`）等
- エスケープ判定: 文の範囲をスキャンし、`Use`（ポインタコピー）・`Aggregate`（コンテナ格納）・`Cast`・`Ref`と、非ホワイトリスト呼び出しへの引き渡し・returnを所有権エスケープとみなす（`src/internal/mir/lowering/stmt/temp_drop.cpp:66`の`collect_rvalue_escapes`、`:106`の`scan_and_free_temps`）
- ホワイトリスト: 引数ポインタを保持しないことが既知のランタイム関数（print系・format系・文字列比較等）は`is_non_retaining_callee`（`src/internal/mir/lowering/stmt/temp_drop.cpp:21`）に列挙され、これらへ渡っただけの一時は解放してよい
- 解放発行: エスケープしなかった文字列一時へ`cm_string_free`、スライス一時へ`cm_slice_free`のCall終端を文末に挿入する（`src/internal/mir/lowering/stmt/temp_drop.cpp:163`）

### 条件式内一時（三項演算子・短絡評価）

三項演算子と`&&`/`||`の腕は分岐先でのみ評価されるため、文スコープでの一括解放では「評価されなかった腕の一時を解放してuse-of-uninitialized」になる。
腕ごとに`ArmTempScope`（`src/internal/mir/lowering/context.hpp:148`）を積み、腕ブロック内（merge分岐前）で腕内完結の一時を解放する（三項は`src/internal/mir/lowering/expr/basic.cpp:500`・`:509`、短絡評価は`src/internal/mir/lowering/expr/binary.cpp:432`・`:488`）。
三項の結果値そのものは所有権判定付きの`end_arm_temp_scope`（`src/internal/mir/lowering/stmt/temp_drop.cpp:229`）が「腕の値がfresh一時で、腕内使用が結果ローカルへのUseコピー1回と読み取りのみ」の場合に所有権移動と判定し、両腕がともに所有を返した場合だけ結果を外側スコープの所有一時として登録する。
片腕でも借用値（変数コピー・リテラル）なら条件依存所有となるため登録せず、誤解放より安全側（リーク側）に倒す。

### 変数上書き時の旧バッファ解放（StringReassignFree）

`s = s + "x"`のような文字列再代入は、新バッファ格納時に旧バッファが浮く。
MIRパス`StringReassignFree`（`src/internal/mir/passes/cleanup/string_reassign_free.cpp:80`）が、全定義がfresh所有（`cm_string_concat`/`cm_*_to_string`結果の単独消費一時、判定は`:23`の`is_fresh_buffer_callee`）・非エイリアス・到達定義に未初期化とリテラルを含まない、の3条件を満たすローカルに限り再代入直前へ`cm_string_free`を挿入する。
このパスはメモリ健全性のためのものであり最適化ではないため、最適化パイプラインの先頭（`src/internal/mir/passes/core/manager.cpp:44`、コピー伝播がMIR形状を書き換える前）に置かれるとともに、O0でも単独実行される（`src/cmd/cm/build.cpp:504`）。

### move semanticsとuse-after-move診断

move状態の追跡は型チェッカの線形フロー解析で行う。
`move`式の評価時に対象変数をmoved状態にマークし（`src/internal/types/checking/expr/primary.cpp:220`のMoveExpr分岐）、素の識別子（`:231`）に加えてフィールド経由の`move obj.field`も基底変数を辿ってマークする（`:254`）。
識別子の使用時に`check_use_after_move`（`src/internal/types/checking/expr/primary.cpp:436`で呼び出し、定義は`:452`）が`is_moved`フラグを検査してエラー診断を出すため、move後の変数使用は関数呼び出し引数・文字列補間・構造体リテラル格納経由を含めてコンパイルエラーになる。
借用中の変数のmoveも同分岐で拒否される（`:227`）。
分岐・ループをまたぐ厳密なCFGフロー解析ではなく同一関数の線形フロー内での診断であり、これは意図的なスコープ制限である。

move後の二重解放はMIR側でも防ぐ。
moved-out変数の読み出しをloweringする際、`is_moved_from`が立っていれば`unregister_destructor_var`（`src/internal/mir/lowering/expr/basic.cpp:454`、実装は`src/internal/mir/lowering/context.cpp:140`）で全スコープからデストラクタ登録を外し、移動先と移動元の両方でデストラクタが走ることを防ぐ。

### 未初期化フィールドの扱い

構造体・ユニオン・固定長配列のローカルは、LLVMコード生成の`alloca`直後に`CreateMemSet`でゼロ初期化される（`src/internal/codegen/llvm/core/translate/function.cpp:501`の`zeroInitAggregate`判定と`:514`のmemset発行）。
これによりnative/jitの未初期化フィールド読み取りがスタックゴミではなくゼロに確定し、もともとゼロ初期化されるwasm/js/tsと挙動が揃う（背景は[archive設計文書](../../archive/v0.17.0/uninitialized-struct-fields.md)）。
特にstring型フィールドではゴミポインタのデリファレンスによるクラッシュを防ぎ、dropパスがNULL（未設定）を安全に無視できる前提を作る。
余分なゼロストアはO1以上のmem2reg/DSEが除去する。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/mir/lowering/stmt/scope.cpp` | `emit_scope_destructors`（スコープ単位のデストラクタ発行） |
| `src/internal/mir/lowering/stmt/control.cpp` | block/while/for/returnでの発行位置、return時の全スコープ一括処理 |
| `src/internal/mir/lowering/stmt/lower.cpp` | 文ディスパッチ、break/continue脱出時の発行、文一時スコープの開閉 |
| `src/internal/mir/lowering/stmt/let.cpp` | デストラクタ対象変数の登録（`has_destructor`判定とマングル名構築） |
| `src/internal/mir/lowering/stmt/temp_drop.cpp` | 文一時・腕一時のエスケープ解析と`cm_string_free`/`cm_slice_free`発行 |
| `src/internal/mir/lowering/context.cpp` / `context.hpp` | `destructor_vars`スタック・登録/解除API・`StmtTempScope`/`ArmTempScope` |
| `src/internal/mir/lowering/impl.cpp` | 関数終端（return文なし）のデストラクタ発行 |
| `src/internal/mir/lowering/mono/specialize.cpp` | ジェネリックデストラクタ特殊化と要素デストラクタループの挿入 |
| `src/internal/mir/passes/cleanup/string_reassign_free.cpp` | 文字列再代入時の旧バッファ解放パス |
| `src/internal/mir/lowering/expr/basic.cpp` | moved-out変数のデストラクタ登録解除、三項の腕スコープ |
| `src/internal/types/checking/expr/primary.cpp` | move状態のマークとuse-after-move診断 |
| `src/internal/codegen/llvm/core/translate/function.cpp` | 集約ローカルのゼロ初期化memset |
| `src/internal/codegen/llvm/native/runtime_format.c` / `runtime_slice.c` | `cm_string_free`（`runtime_format.c:2206`）・`cm_slice_free`（`runtime_slice.c:44`）の実体 |

## 落とし穴とケア

- **二重解放の不変条件**: 所有権が移動した一時（変数束縛・コンテナ格納・return・保持呼び出しへの引き渡し）は必ず解放対象から外す。エスケープ判定を緩めると一時の解放がuse-after-freeに化けるため、判定に迷う場合はリーク側（解放しない）に倒すのが本設計の一貫した方針である
- **非保持ホワイトリストの維持**: `is_non_retaining_callee`は「引数ポインタを保持しない」ことが実装上保証された関数だけを列挙する。新しいランタイム関数が引数ポインタを構造体やグローバルに保持する場合、絶対にこのリストへ追加してはならない（追加すると渡した一時が解放されuse-after-freeになる）
- **デストラクタ順序の規約**: すべての脱出経路で「defer逆順 → デストラクタ逆順」の順序を保つ。新しい制御構文や脱出経路を追加する際は`lower_block`と同一の順序で`emit_scope_destructors`を呼ぶこと（ループ本体・break/continueにこれが漏れていたことが無条件リークの原因だった）
- **moved-out変数の登録解除**: move対応の構文を増やす場合、型チェッカのマークとMIR側の`unregister_destructor_var`を対で更新する。片方だけだと診断漏れまたは二重解放になる
- **wasmとの補完関係**: dropパスは論理的な解放呼び出しの挿入であり、実メモリが返るかはバックエンドのアロケータ次第である（native/jitはlibc malloc/freeに委譲されるため実解放される）
- **回帰テスト**: デストラクタ挿入位置は`tests/common/basic/destructor.cm`・`destructor_scope.cm`・`destructor_order.cm`・`destructor_loop_scope.cm`、一時dropは`tests/common/memory/temp_string_drop.cm`・`temp_slice_drop.cm`・`conditional_arm_temp_drop.cm`・`ternary_result_temp_drop.cm`・`string_reassign_drop.cm`、move診断は`tests/common/errors/use_after_move.cm`・`move_field_then_use.cm`・`move_then_interpolate.cm`・`borrow_then_move.cm`、ネストジェネリックは`tests/common/collections/nested_generic_destructor.cm`、未初期化フィールドは`tests/common/structs/uninitialized_fields.cm`、MIR形状の検査は`tests/regression/cases/mir_lowering/temp_string_drop_*.cm`・`temp_slice_drop_expr_stmt.cm`で固定している

## 関連資料

- [一時オブジェクトのdropパスとループ本体の寿命管理（archive設計文書）](../../archive/v0.17.0/memory-drop-and-lifetime.md)
- [未初期化構造体フィールドのゼロ初期化（archive設計文書）](../../archive/v0.17.0/uninitialized-struct-fields.md)
- [アロケータ設計](allocator.md) — 挿入された解放呼び出しが接続される先の確保・解放経路
- [集約コピーのlowering](aggregate-copy.md) — 集約を値として動かす経路の設計
