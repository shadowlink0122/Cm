# スライスと固定長配列のLLVMコード生成

スライス（動的配列 `T[]`）と固定長配列（`T[N]`）はMIRからLLVM IRへ対照的な方式で降ろされる。スライスはランタイムヘッダ`CmSlice`への不透明ポインタ（opaque `ptr`）として表現され、全操作がMIR段階で`cm_slice_*`関数への`Call`ターミネータに変換済みのため、LLVM層は宣言済みシグネチャでのcall発行だけを行う。固定長配列は`[N x elem]`のLLVM第一級配列型として表現され、添字アクセスは`getelementptr`+load/storeのインラインIRへ展開される。native/jitはこの変換経路を完全に共有する。

## 概要

方式の二分は「要素サイズのディスパッチをどの段階で確定するか」で決まっている。スライスはデータバッファの確保幅（elem_size）がランタイムヘッダに記録され、アクセス幅はMIR loweringが[slice_dispatch.hppの対応表](../slices/runtime-representation.md)から選ぶ関数名（`cm_slice_push_i32`等）に固定されるため、LLVM層に型ディスパッチのロジックを持ち込む必要がない。関数呼び出し境界がABIとして安定しているため、ランタイム実装（成長戦略・ヘッダレイアウト）を変えてもコード生成側の変更が不要で、[境界検査計装](../slices/bounds-checking.md)もMIRの`Call`ターミネータを対象に一様に挿入できる。一方、固定長配列はコンパイル時にサイズと要素型が確定しているため、GEPのインライン展開でLLVMのSROA・ベクトル化最適化をそのまま効かせる。

| 操作 | 方式 | 発行されるIR |
|---|---|---|
| スライス生成・push/pop/len/cap/delete/clear | ランタイム呼び出し | `call @cm_slice_new` / `@cm_slice_push_*` 等 |
| スライス添字読み（スカラ/ポインタ要素） | ランタイム呼び出し | `call @cm_slice_get_<width>` / `@cm_slice_get_ptr` |
| スライス添字読み（構造体/ユニオン要素） | 呼び出し+インライン | `call @cm_slice_get_element_ptr` + デリファレンスload |
| スライス添字書き | 呼び出し+インライン | `call @cm_slice_get_element_ptr` + store |
| 多次元スライスの中間段 | ランタイム呼び出し | `call @cm_slice_get_subslice` / `_subslice_ref` |
| 固定長配列の添字読み書き | インラインIR | `getelementptr inbounds [N x elem]` + load/store |
| 固定長配列→スライス変換 | ランタイム呼び出し | `call @cm_array_to_slice(ptr, len, elem_size)` |
| 部分配列式（`arr[a..b]`） | ランタイム呼び出し | `call @__builtin_array_slice`（`terminator/call.cpp:152`） |

## データ構造とアルゴリズム

### 型表現（types.cpp）

`convertType`は`hir::TypeKind::Array`を`array_size`の有無で分岐する（`src/internal/codegen/llvm/core/types.cpp:73-84`）。サイズなし（スライス）は`ctx.getPtrType()`すなわちopaque `ptr`で、値の実体はランタイムの`CmSlice*`ハンドルである。サイズあり（固定長配列）は`llvm::ArrayType::get(elem, N)`で、多次元はネスト構造（`int[D1][D2]` → `[D1 x [D2 x i32]]`）を保持して複数インデックスGEPとベクトル化を効かせる。スライス型の定数はnullポインタとして変換される（`types.cpp:712-717`）。

### ランタイム関数の宣言（runtime/builtins.cpp）

`cm_slice_*`のLLVM宣言は`declareBuiltinFunction`（`src/internal/codegen/llvm/core/runtime/builtins.cpp:230-354`）が関数名ごとの固定シグネチャで`module->getOrInsertFunction`し、`declareExternalFunction`（`utils.cpp:14`）経由で解決される。push/popは幅ごとに第2引数型/戻り値型（i8/i16/i32/i64/f32/f64/ptr）が異なり、`cm_slice_push_blob`はデータ先頭ポインタを受ける。MIR側が選んだ関数名とここで宣言する型幅は1対1に対応しており、シグネチャの型ディスパッチはこの表が唯一の定義点になる。

### ローカルスライスのalloca初期化（translate/function.cpp）

サイズなしArray型のローカルは`ptr`のallocaを作り、要素サイズを計算して`cm_slice_new(elem_size, 4)`で初期化した結果をstoreする（`src/internal/codegen/llvm/core/translate/function.cpp:385-444`）。グローバルスライス変数はLLVMグローバル（`ptr`）へ写像し、実体の`cm_slice_new`はmainエントリのMIRで一度だけ実行する（`function.cpp:388-398`）。構造体ローカルはmemsetゼロ初期化の後、スライス型フィールドを`CreateStructGEP`+`cm_slice_new`で個別初期化する（`function.cpp:526-585`）。この段の要素サイズ計算はMIR側`slice_dispatch.hpp`とは独立の分岐で、構造体/ユニオン要素はDataLayout実測、多次元は`CmSlice`ヘッダの32バイトを用いる（`function.cpp:403-429`）。

### スライスリテラルの生成（MIR lowering → call列）

`int[] xs = [1, 2, 3]`はMIR loweringの`stmt/let.cpp:336-399`が`cm_slice_new(elem_size, 要素数)`を発行し、要素ごとに`cm_slice_push_*`の`Call`ターミネータを連ねる。空リテラルは容量0の`cm_slice_new`のみ、初期値なし宣言も同様に`cm_slice_new(elem_size, 0)`である（`let.cpp:219-278`）。`CM_DUMP_IR=1`での実際の出力は次の通りで、alloca段の既定初期化とリテラル初期化の2回の`cm_slice_new`が見える（後者のstoreが前者を上書きする）。

```llvm
entry:
  %slice_1 = alloca ptr, align 8
  %slice_ptr = call ptr @cm_slice_new(i64 4, i64 4)   ; alloca段の既定初期化
  store ptr %slice_ptr, ptr %slice_1, align 8
  ...
bb0:
  %2 = call ptr @cm_slice_new(i64 4, i64 3)           ; リテラル [1,2,3] の初期化
  store ptr %2, ptr %slice_1, align 8
  br label %bb1
bb1:
  %load = load ptr, ptr %slice_1, align 8
  call void @cm_slice_push_i32(ptr %load, i32 1)      ; 以降、要素ごとにpush
```

文字列スライス（`string[]`）はelem_size=8で確保し、各要素を`cm_slice_push_ptr`で格納する。文字列リテラルは`createHeaderedStringLiteral`（`types.cpp:670-687`）がマジック・バイト長付きヘッダ構造体のグローバル定数`@strh`を作り、データ先頭（ヘッダ16バイト直後）へのGEP定数を渡す。

```llvm
call void @cm_slice_push_ptr(ptr %load, ptr getelementptr inbounds (i8, ptr @strh, i64 16))
```

### 添字アクセス: スライス（call）と固定長配列（GEP）

スライスの添字読みはMIR loweringの`expr/access.cpp:661-716`が要素型で関数を選ぶ。スカラは`cm_slice_get_<width>`、ポインタ/文字列は`cm_slice_get_ptr`、構造体/ユニオンは`cm_slice_get_element_ptr`で要素先頭ポインタを取得してからDerefプロジェクションでロードする。添字書き込みは要素型を問わず`cm_slice_get_element_ptr`+Deref格納へ正規化され（`expr/binary.cpp:316-366`）、LLVM層では通常のcall+storeとして現れる。

```llvm
  %3 = call i32 @cm_slice_get_i32(ptr %load5, i64 1)   ; v = xs[1]
  ...
  %4 = call ptr @cm_slice_get_element_ptr(ptr %load7, i64 2)
  store ptr %4, ptr %local_15, align 8
bb7:
  %5 = load ptr, ptr %local_15, align 8
  store i32 9, ptr %5, align 4                          ; xs[2] = 9
```

固定長配列の添字はMIRの`Index`プロジェクションのまま届き、`convertPlaceToAddress`（`src/internal/codegen/llvm/core/operand.cpp:747-1060`）がGEPへ展開する。連続する`Index`プロジェクションをまとめて読み、alloca型がネスト配列なら`[0, i, j, ...]`の多次元`CreateInBoundsGEP`を1命令で発行する（`operand.cpp:1001-1016`）。i32のインデックス値は`sext`でi64へ拡張してからGEPに渡す。ベースがポインタ値（Deref後・ポインタ引数・ロード結果）の場合は要素型ストライドの単一インデックスGEP（`ptr_elem`）に切り替わる（`operand.cpp:756-853`）。

```llvm
  %idx_ext = sext i32 %idx_load to i64
  %elem_ptr = getelementptr inbounds [4 x i32], ptr %local_17, i64 0, i64 %idx_ext
  store i32 10, ptr %elem_ptr, align 4                  ; arr[0] = 10
```

要素が集約型の場合、代入の右辺がallocaアドレスならロードしてstoreするが、DataLayout実測サイズが閾値（128バイト）以上のときは第一級集約コピーを避けてmemcpyへ切り替える（`statement/assign.cpp:441-451`、背景は[集約コピーのlowering](../memory/aggregate-copy.md)）。

### 固定長配列の初期化・グローバル・decay

固定長配列のallocaは`llvm.memset`でゼロ初期化され、未初期化要素の値が全バックエンドで一致する（`translate/function.cpp:503-524`）。配列リテラルはMIR loweringが要素ごとの`Index`プロジェクション代入へ分解する（`expr/construct.cpp:236-318`）ため、LLVM層ではGEP+storeの列になる。`MirRvalue::Aggregate`のArray種が値として届いた場合は一時alloca`arr_temp`にGEP+storeし配列値をロードして返す（`rvalue.cpp:584-616`）。グローバル・static変数の固定長配列は`Constant::getNullValue`（`zeroinitializer`）で定義され、非ゼロ初期値はmainエントリのMIRが代入する（`translate/program.cpp:303-310`・`function.cpp:482-496`）。配列→ポインタdecayはMIR段階で処理され、関数実引数の配列変数は自動的にRef化（`expr_call.cpp:194-196`）、ポインタ型変数への束縛は`&arr[0]`のRefへ（`stmt/let.cpp:283-310`）、`as`キャストにも暗黙Refが挿入される（`expr/cast.cpp:113`）。いずれもLLVM層ではallocaアドレス（`ptr`）がそのまま渡る。

### メンバスライス・サブスライス参照・多次元

多次元スライスはelem_size=32（`CmSlice`ヘッダのインライン格納）で外側を確保し、内側は`cm_slice_push_slice`でヘッダごとコピーする。ネストした配列リテラルは内側を固定長配列として構築してから`cm_array_to_slice(ptr, len, elem_size)`でスライスへ実体化する（`stmt/let.cpp:418-460`）。`m[0].push(x)`のようなスライス要素レシーバへの変異は、`cm_slice_get_subslice_ref`で外側バッファに格納中の内側ヘッダへの参照を取得してからレシーバとして使うため書き戻しが不要で（`expr/access.cpp:280-353`）、読み取りの`rows[0][2]`は中間段を`cm_slice_get_subslice`（ヘッダのコピー）で辿って最終段のみ`cm_slice_get_*`を呼ぶ（`access.cpp:608-716`）。

```llvm
  %6 = call ptr @cm_slice_get_subslice_ref(ptr %load19, i64 0)   ; rows[0] を参照で場所化
  store ptr %6, ptr %slice_26, align 8
bb9:
  %load20 = load ptr, ptr %slice_26, align 8
  call void @cm_slice_push_i32(ptr %load20, i32 5)               ; rows[0].push(5)
```

### for-inループの展開

for-inはHIR loweringの段階でインデックスベースのforへ脱糖される（`src/internal/hir/lowering/stmt.cpp:374-456`）。`__for_in_idx`を0で初期化し、条件はスライスなら`__builtin_slice_len`（→`cm_slice_len`呼び出し）、固定長配列ならコンパイル時定数のサイズとの比較、更新は+1、本体先頭でループ変数へ`iterable[idx]`を代入する。要素ロードは通常の添字読みと同じ経路（スライスは`cm_slice_get_*`、固定長配列はGEP+load）を通るため、for-in専用のコード生成は存在しない。イテレータ実装型に対しては`iter()`/`has_next()`/`next()`のwhileループへ展開する別経路がある（`stmt.cpp:301-372`）。

```llvm
bb8:                                              ; ループ条件評価の先頭
  %4 = call i64 @cm_slice_len(ptr %load22)
  %trunc = trunc i64 %4 to i32
  ...
bb11:
  %ult = icmp ult i32 %load29, %load30             ; idx < len
  ...
bb9:                                              ; 本体: 要素ロード
  %5 = call i32 @cm_slice_get_i32(ptr %load24, i64 %sext)
```

### 境界検査との接続点

スライスアクセスがランタイム呼び出しであることを利用し、`--sanitize=bounds`のMIR計装パス（`src/internal/mir/passes/instrumentation/bounds.cpp`）は`cm_slice_get_*`（subslice除く）と`cm_slice_delete`の`Call`ターミネータを持つブロックを分割し、`cm_slice_len`取得→負値検査→上限検査→`cm_bounds_error`（`builtins.cpp:246-251`で宣言）の検査列を前置する。固定長配列のGEP側はLLVMの`BoundsCheckingPass`が補完する。二段構えの全体像は[境界検査](../slices/bounds-checking.md)を参照。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/types.cpp:73-84` | Array型のLLVM型マッピング（スライス=`ptr`、固定長=`[N x elem]`） |
| `src/internal/codegen/llvm/core/types.cpp:670-687` | ヘッダ付き文字列リテラル定数の生成（`@strh`+16オフセットGEP） |
| `src/internal/codegen/llvm/core/runtime/builtins.cpp:230-354` | `cm_slice_*`のLLVM宣言（getOrInsertFunctionによる固定シグネチャ表） |
| `src/internal/codegen/llvm/core/translate/function.cpp:385-444` / `:503-585` | スライスallocaと`cm_slice_new`初期化、集約memset、構造体スライスフィールド初期化 |
| `src/internal/codegen/llvm/core/operand.cpp:747-1060` | `Index`プロジェクション→GEP展開（多次元GEP・ポインタIndexing・sext） |
| `src/internal/codegen/llvm/core/statement/assign.cpp:441-451` | 集約コピーのmemcpy閾値切替 |
| `src/internal/codegen/llvm/core/rvalue.cpp:584-616` | Aggregate(Array)右辺値の一時alloca構築 |
| `src/internal/codegen/llvm/core/terminator/call.cpp:152-196` | 部分配列式`__builtin_array_slice`のcall発行 |
| `src/internal/mir/lowering/stmt/let.cpp:219-460` | スライス宣言・リテラル初期化のcall列生成（elem_size決定を含む） |
| `src/internal/mir/lowering/expr/access.cpp:280-353` / `:608-716` | サブスライス参照によるレシーバ場所化、添字読みの関数選択 |
| `src/internal/mir/lowering/expr/binary.cpp:316-366` | 添字書きの`cm_slice_get_element_ptr`+Deref格納への正規化 |
| `src/internal/mir/lowering/expr/construct.cpp:236-318` | 固定長配列リテラルの`Index`プロジェクション分解 |
| `src/internal/hir/lowering/stmt.cpp:296-456` | for-inのインデックスベースfor/イテレータwhileへの脱糖 |
| `src/internal/mir/passes/instrumentation/bounds.cpp` | 境界検査計装（スライスアクセスcallの前置検査） |

## 落とし穴とケア

- 要素サイズの対応表は2段階に存在する。MIR lowering側は`slice_dispatch.hpp`の`slice_scalar_info`に一元化されているが、LLVM層のalloca初期化（`translate/function.cpp:403-429`・`:545-570`）は独自のkind分岐でelem_sizeを計算しており、新しいスカラ型を追加するときは両方の整合を確認する必要がある。alloca段の既定初期化はMIR側の初期化に上書きされるため通常は露見しないが、MIR初期化を通らないローカルでは確保幅の不一致がヒープ破壊になる。
- `cm_slice_get_element_ptr`・`cm_slice_get_subslice_ref`が返すポインタはスライスのデータバッファ内を指すため、後続のpush等の再確保で無効化される。MIR loweringは取得直後の単一store/単一操作にのみ使う構造を守ること（詳細は[ランタイム表現](../slices/runtime-representation.md)）。
- 固定長配列GEPのインデックスは必ずi64へ`sext`してから渡す。i32のまま混ぜると型不一致のIRやネガティブインデックスの誤ゼロ拡張を生む（`operand.cpp:787-791`）。
- 到達不能な`Index`プロジェクション（nullアドレス・インデックスローカル未解決）は診断ログを出してnullptrを返し、黙って不正なGEPを発行しない（`operand.cpp:749-754`・`:795-800`）。
- 大きな集約要素のコピーはmemcpy閾値を経由させること。第一級集約のload/storeで書くとO2のSROAが全要素をSSA展開し、コンパイル時間とメモリの二次爆発を起こす（`assign.cpp:441-451`）。
- IRの確認は`CM_DUMP_IR=1 cm run <file>.cm`で行える。回帰テストは`tests/common/dynamic_array/`（要素型幅・添字書き・メンバ操作）と`tests/common/array/`（固定長・多次元）が担う。

## 関連資料

- [スライスのランタイム表現と要素型ディスパッチ](../slices/runtime-representation.md) — `CmSlice`ヘッダ構造・操作関数群・`slice_dispatch.hpp`
- [境界検査](../slices/bounds-checking.md) — MIR計装パスとLLVM BoundsCheckingPassの補完関係
- [チェーンレシーバの解決](../slices/chain-receiver.md) — `cm_slice_get_subslice_ref`によるレシーバ場所化の全体像
- [MIR→LLVM IR変換](mir-to-llvm.md) — 変換器の構成・型マッピング・関数宣言解決
- [集約コピーのlowering](../memory/aggregate-copy.md) — memcpy閾値とsret・ポインタ渡しの背景
