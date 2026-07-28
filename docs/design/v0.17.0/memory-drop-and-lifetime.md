---
title: 一時オブジェクトのdropパスとループ本体の寿命管理
parent: v0.17.0 Design
---

# 一時オブジェクトのdropパスとループ本体の寿命管理

大規模開発ボトルネック監査の所見C12/C13/H12/M15に対する実装設計である。
これらはいずれも「メモリを解放しない」という構造的テーマ（監査総括テーマ4）に属し、通常のループやコンパイラ生成の一時オブジェクトが無条件にリークする根本原因を扱う。
本文書はコンパイラ一時オブジェクトのlast-use解放パスの新設、while/forループ本体でのスコープデストラクタ実行、ネストジェネリックのデストラクタ再帰降下、寿命チェックのすり抜け解消を対象とする。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C12 | メモリ | 文字列・スライス一時オブジェクトが全バックエンドで解放されない（`cm_slice_free`の呼び出し箇所ゼロ、実測でRSS線形増加） | 文字列一時・スライス一時とも実装済み（スライスはmap/filter結果の無名一時を同じ文単位トラッキングで追跡し`cm_slice_free`を発行。map100万回ループのRSSが65.6MB→1.4MBに平坦化。条件式内一時（三項・短絡評価の腕）も腕スコープで腕内解放に対応し、三項腕連結の100万回ループRSSが49.6MB→1.4MBに平坦化。旧記述: 文字列一時は実装済み（MIR loweringの文単位トラッキング+エスケープ解析で、let/代入/式文中の`cm_string_concat`・`cm_*_to_string`結果の非エスケープ一時へ`cm_string_free`を発行。三項演算子・短絡評価の腕は未初期化free回避のため対象外。実測でconcatループのRSSが97.8MB→1.4MBに平坦化。スライス一時・条件式内一時・変数上書き時の旧バッファ解放は未着手） |
| C13 | メモリ | RAIIデストラクタがwhile/forループ本体スコープ終了で走らない（`{}`で囲んだ時のみ動作、ループ内の`Vector`使用が無条件リーク） | 実装済み（`lower_while`/`lower_for`のボディ末尾とbreak/continue経路に`emit_scope_destructors`を追加。`tests/common/basic/destructor_loop_scope.cm`で回帰固定） |
| H12 | メモリ | 所有権/寿命チェックが構文パターンマッチのみで、変数経由のreturn・構造体フィールドへの格納・文字列補間経由のmove後使用ですり抜ける | 一部実装（フィールド経由move `move obj.field` の基底変数マークと、文字列補間内のmove後使用診断を追加。use-after-moveメッセージをi18n化。あわせてMIR側で `move` された変数のデストラクタ登録を解除し、moved-out変数の二重解放を修正。フロー解析化は未着手） |
| M15 | メモリ | ネストしたジェネリックのデストラクタは1段しか降りない（`Vector<Vector<T>>`の内側がリーク、テストコメントで既知） | 実装済み（要素型がジェネリック特殊化の場合、基底のジェネリックデストラクタが存在すれば呼び出しを挿入し、不動点ループに特殊化を連鎖生成させる。`tests/common/collections/nested_generic_destructor.cm`で回帰固定） |

## 背景と根本原因

### C13: while/forループ本体でデストラクタが走らない

スコープデストラクタの実行本体は`emit_scope_destructors`（`src/internal/mir/lowering/stmt/scope.cpp:27`）に集約されており、登録済みデストラクタ変数を逆順に走査して各`<型名>__dtor`呼び出しを生成する。
このメソッドの呼び出し元は明示ブロック`lower_block`（`src/internal/mir/lowering/stmt/control.cpp:433`）と関数終了経路（`src/internal/mir/lowering/impl.cpp:253`の`emit_destructors`、および`lower_return`の`src/internal/mir/lowering/stmt/control.cpp:54`）に限られる。
`lower_while`（`src/internal/mir/lowering/stmt/control.cpp:147`）はループ本体で`push_scope`（167行）と`pop_scope`（175行）を行い、その間でdefer文だけを実行する（171-174行）が、`emit_scope_destructors`を呼ばない。
`lower_for`（同ファイル188行）も同様に`push_scope`（227行）・defer実行（235-238行）・`pop_scope`（240行）を行うが、デストラクタ呼び出しを生成しない。
結果として、ループ本体でスコープ内に閉じるはずの`Vector`等がループ脱出まで（実際には関数終了まで）解放されず、反復ごとに新しい割り当てが積み上がる。
監査の実測ではループ内`Vector`はN=8Mで124MB（線形リーク）、同じ本体を`{}`で囲むとNに依らず1.3MBで平坦になる。
注記: v0.17.0でwhile本体のdefer毎周期実行（M16）は`push_scope`/`pop_scope`導入で修正済みだが、デストラクタ呼び出しはdeferとは別経路であり未対応のまま残っている。

### C12: コンパイラ一時オブジェクトのdropパスが無い

`cm_slice_free`はnative（`src/internal/codegen/llvm/native/runtime_slice.c:44`）とwasm（`src/internal/codegen/llvm/wasm/runtime_slice.c:40`）の両方に定義されているが、コンパイラが生成するコードからの呼び出し箇所はゼロである。
デストラクタ変数として登録されるのはユーザーが`let`で束縛した名前付き変数だけ（`src/internal/mir/lowering/stmt/let.cpp:787`の`register_destructor_var`）であり、concat・format・map/filter/sort等が返す無名の中間結果は登録経路を持たない。
これらの一時オブジェクトはヒープ上のスライスヘッダとデータ領域を確保するが、last-useを過ぎても誰も解放しないため、式を評価するたびにリークが積み上がる。
文字列連結ループはN=200kで4.6秒（二次時間かつ全中間結果がリーク）、インタプリタではN=100kでRSS 1GBという実測が根拠である。

### H12: 寿命チェックが直接の`move ident`しか捕捉しない

move状態のマークは`infer_type`のMoveExpr分岐（`src/internal/types/checking/expr/primary.cpp:220-235`）だけで行われ、オペランドが素の`IdentExpr`のときのみ`mark_variable_moved`が呼ばれる（225-231行）。
使用後チェック`check_use_after_move`（同ファイル426行）も識別子式評価時（410行）に該当変数の`is_moved`フラグを見るだけである。
このため`move obj.field`のようなフィールド経由のmove、moveした値を関数の戻り値として返した後の元変数使用、文字列補間`{}`の内側でのmove後使用は、いずれもASTパターンにマッチせずマーク自体が付かないため無診断で通過する。
所有権追跡がフロー解析ではなく単一式の構文形にのみ依存していることが根因である。

### M15: ネストジェネリックのデストラクタが1段しか降りない

ジェネリックデストラクタの特殊化は`src/internal/mir/lowering/mono/specialize.cpp:508`で、関数名が`__dtor`で終わりtype_argsを持つとき要素型のデストラクタ呼び出しループを挿入する。
この挿入は`type_args[0]`から要素型のデストラクタ名を組み立てる（510-511行）が、要素型自身がさらにジェネリック（`Vector<Vector<int>>`の内側`Vector<int>`）である場合、その内側のデストラクタが同様に要素ループを持つよう連鎖的に生成される保証がなく、内側の要素データがリークする。
テストコメントで既知の問題として記録されている。

## 設計方針

1. **ループ本体スコープでのデストラクタ実行（C13）**: `lower_while`と`lower_for`のループ本体で、defer実行と同じ位置に`emit_scope_destructors(ctx)`呼び出しを追加し、`pop_scope`の直前で本体スコープに閉じるデストラクタ変数を回収する。
   `lower_block`と同一の順序規約（defer逆順 → デストラクタ逆順 → pop_scope）に揃え、明示ブロックとループ本体の挙動を一致させる。
   `break`/`continue`/`return`による途中脱出でもデストラクタが走るよう、脱出経路（`lower_return`は関数全体を`get_all_destructor_vars`で処理済みだが、break/continueはループ本体スコープ分だけ処理する必要がある）を検討する。
2. **コンパイラ一時オブジェクトのdropパス（C12）**: 式のloweringで新規に確保される無名スライス・文字列（concat・format・map/filter/sort結果等）を「所有する一時ローカル」として区別し、その式を含む文の評価完了直後（last-use後）に`cm_slice_free`/文字列解放を挿入する。
   ムーブされて別の場所に所有権が移った一時（戻り値・変数束縛・別コンテナへの格納）は解放対象から除外する所有権フラグを持たせ、二重解放を防ぐ。
   まずは文単位のスコープ（statement-temporary scope）で最も被害の大きいループ内一時を解放し、段階的に対象を広げる。
3. **寿命チェックのフロー化（H12）**: moveのマーク条件をMoveExprの`IdentExpr`限定から拡張し、フィールドパス（`obj.field`）と、moveした値をreturn/引数/フィールドに渡した場合の元変数マークを追加する。
   分岐・ループをまたぐ厳密なフロー解析は将来課題とし、まずは同一関数線形フロー内での主要すり抜け（return経由・フィールド格納・補間経由）を診断可能にする。
4. **ネストデストラクタの再帰降下（M15）**: 要素デストラクタループ挿入時に、要素型がさらにジェネリックである場合はその特殊化デストラクタが確実に生成・登録されるよう依存関係を辿り、多段ネストで各段の要素データが解放されることを保証する。

## 構文例・出力例

寿命チェック（H12）で新たに診断対象となる例を示す（いずれも現状は無診断で通過する）。

```cm
// フィールド経由のmove後使用
Vector<int> v = make_vec();
Wrapper w = Wrapper{ inner: move v };
print(v.len());          // エラー: move後の変数 'v' を使用しています

// return経由のmove後使用
Vector<int> a = make_vec();
return move a;           // ここで a はムーブ済み
// print(a.len());       // 後続があればエラー対象

// 文字列補間経由のmove後使用
string s = build();
consume(move s);
print("value: {s}");     // エラー: move後の変数 's' を使用しています
```

C13の期待挙動はメモリ挙動であり構文出力の変化は無い。
ループ内で束縛された`Vector`が反復ごとに解放され、N=8Mでも定常RSSが平坦になる（従来は線形増加）ことをメモリ計測で確認する。

## 実装の段階分割

- **Phase 1（C13）**: `lower_while`/`lower_for`のループ本体末尾に`emit_scope_destructors`を追加し、明示ブロックと同一順序に整える。break/continue/returnの途中脱出時のループ本体デストラクタ実行を含める。
- **Phase 2（M15）**: `specialize.cpp`のデストラクタループ挿入を多段ネスト対応にし、内側ジェネリック要素の解放を保証する。
- **Phase 3（C12）**: 文単位の一時オブジェクトスコープを導入し、所有権フラグ付きで無名スライス・文字列の一時をlast-use後に解放する。ムーブ済み一時の除外を実装する。（文字列一時・スライス一時とも実装済み: `src/internal/mir/lowering/stmt/temp_drop.cpp`。文のMIR範囲をスキャンし、Use/Aggregate/Cast/Ref・非ホワイトリスト呼び出しへの引き渡し・returnをエスケープとみなす。スライスはmap/filter結果を追跡し`cm_slice_free`を発行。条件式内一時も実装済み: 三項演算子・短絡評価の腕に腕スコープ（ArmTempScope）を導入し、腕内で確保され腕内で完結する一時を腕ブロック内（merge分岐前）でエスケープ解析して解放する。腕の結果値はresult = copy(値)のUseエスケープとして自然に保護されるため誤解放しない。三項腕の連結を含む100万回ループのRSSが49.6MB→1.4MBに平坦化。変数上書き時の旧バッファ解放も実装済み: MIRパスStringReassignFree（O1以上・パイプライン先頭）が、全定義fresh所有（concat/to_string結果の単独消費一時）・非エイリアス（コピー先一時のプロファイルを1段追跡し、引数一時経由の読み取りは許容）・到達定義に未初期化とリテラルを含まない、の3条件を満たすローカルに限り再代入直前へ旧値のcm_string_freeを挿入する。リテラル初期化のアキュムレータ（string acc = ""）やエイリアスされた変数は保守的にスキップ。fresh再代入100万回ループのRSSが33.5MB→1.3MBに平坦化。三項結果一時も実装済み: 所有権判定付きend_arm_temp_scope（腕の値が腕内登録のfresh一時で、腕内使用が読み取り・非保持呼び出しと結果ローカルへのUseコピー1回のみの場合に所有権移動と判定）を導入し、両腕がともに同種の所有権移動をした場合のみ結果を外側スコープの所有一時として登録する。片腕でも借用値（変数コピー・リテラル）なら条件依存所有のため登録しない。三項腕連結の消費100万回ループでRSS 17.4MB→1.4MB。残り: O0での再代入解放（StringReassignFreeパスはO1+のみ））
- **Phase 4（H12）**: moveマーク条件をフィールドパス・return/引数/フィールド格納へ拡張し、線形フロー内のmove後使用を診断する。

各Phaseは独立に価値があり、被害の大きいC13を先頭に置く。

## テスト計画（tests/common/配下）

- **C13回帰**: `tests/common/`にループ内`Vector`束縛を反復させるプログラムを追加し、jit/native/wasmで定常メモリが反復回数に依存しないことを確認する。`{}`明示ブロック版と同一のメモリ挙動になることを対照する。
- **C12回帰**: 文字列連結ループ・map/filter/sortチェーンをループ内で回すプログラムを追加し、中間結果が解放されることをメモリ計測（RSSの平坦性）で検証する。
- **H12回帰**: フィールド経由move・return経由move・補間経由moveの各パターンを`tests/common/`のエラーケースとして追加し、期待診断メッセージ（`move後の変数 '{0}' を使用しています`）が出ることを確認する。move後使用が無い正常系も対にして誤検出が無いことを保証する。
- **M15回帰**: `Vector<Vector<int>>`の内側要素にデストラクタ副作用（カウンタ等）を持たせ、全段のデストラクタが呼ばれることをjit/native/wasmで確認する。
- unit/regression層では、`emit_scope_destructors`呼び出しがwhile/forのMIRに現れることを`tests/regression/`のMIR検査ケースで確認する。

## リスクと非互換性

- **二重解放（C12）**: 所有権フラグの判定を誤ると、ムーブ済み一時を解放してしまいuse-after-freeを招く。所有権が移動した一時は必ず解放対象から外す不変条件を保つ。
- **既存のリーク前提コードの挙動変化（C13/C12）**: これまでリークで延命していたオブジェクトが解放されるため、寿命を誤って前提にしていたコードが顕在化する可能性がある。ただし正しい所有権規約では問題にならない。
- **診断強化による既存コードの新規エラー（H12）**: これまで無診断で通過していたmove後使用がエラーになるため、既存テスト・サンプルの一部が失敗しうる。破壊的変更を避けるため、まず警告として導入し段階的にエラー化する選択肢を残す。
- **バックエンド差異**: wasmアロケータは従来解放を実装していなかった（H11、別文書allocator-and-temp-poolでフリーリスト化済み）ため、C12のdrop挿入だけではwasmで実メモリが返らない。dropパスとwasmアロケータ改修は補完関係にあり、片方だけでは効果が限定される点に留意する。

## 関連

- `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（監査本体、テーマ4「メモリを解放しない」）
- `docs/archive/v0.17.0/allocator-and-temp-pool.md`（H11/M14、wasmでの実解放とアロケータ差し替え。実装済み）
- `src/internal/mir/lowering/stmt/scope.cpp` / `src/internal/mir/lowering/stmt/control.cpp`（スコープ・制御フローlowering）
- `src/internal/mir/lowering/mono/specialize.cpp`（ジェネリックデストラクタ特殊化）
- `src/internal/types/checking/expr/primary.cpp` / `src/internal/types/scope.cpp`（move状態管理）
