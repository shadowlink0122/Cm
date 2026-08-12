# `as`キャストの設計

`as`はCmで唯一の明示型変換演算子であり、パーサが`CastExpr`を作り、型検査が許可規則の検査と結果型の決定を行い、HIRの`HirCast`・MIRの`MirRvalue::Cast`を経てLLVMコアの単一箇所（`rvalue.cpp`のCast右辺値処理）が変換命令を生成する一本道のパイプラインで実装されている。ユニオン型判別の`is`も同じ`CastExpr`ノードを`type_check`フラグ付きで共有し、数値変換の命令選択の詳細（飽和・符号拡張）は[../codegen-native/numeric-and-casts.md](../codegen-native/numeric-and-casts.md)が担当する。本文書は型検査段の許可規則とAST→HIR→MIR→LLVMのlowering全体像を扱う。

## 概要

`expr as Type`は単項演算子より低く乗除算より高い優先度で解析され（`src/internal/syntax/parser/expr/binary.cpp:326-352`）、`&x as ulong`は`(&x) as ulong`と解釈される。`parse_cast_expr`はwhileループで`as`/`is`を連続消費するため、チェーンキャスト`x as A as B`は左結合のネストした`CastExpr`になる（`binary.cpp:331-349`）。ASTノードは`CastExpr { operand, target_type, type_check }`（`src/internal/syntax/ast/expr.hpp:541-549`）で、`type_check=true`が`is`である。型検査は原則として「ターゲット型をそのまま式の型とする」寛容な規則を採り、危険なビット再解釈になるケースだけを型エラーとして拒否する。実行時意味論はすべてLLVM IR生成時に決まり、native実行とJIT実行は同じ`MIRToLLVM`のCast処理を共有するため意味論の分裂がない。

## データ構造とアルゴリズム

### 型検査段の許可規則（primary.cpp）

`CastExpr`の検査は`src/internal/types/checking/expr/primary.cpp:111-219`にある。規則は次の通り。

- 通常の`as`はオペランド型を推論した後、無条件にターゲット型を式の型として返す（`primary.cpp:141-143`）。数値間・ポインタ↔整数・ポインタ間・enum（値enumは`resolve_typedef`がintへ解決する。`src/internal/types/checking/utils/compat.cpp:18-81`）はこの経路で許可される。
- 数値スカラ（bool/整数/浮動小数/char）→ `string`/`cstring`は型エラーとして拒否する（`primary.cpp:144-178`）。ビット再解釈になりクラッシュ・空文字化の原因になるためで、ユニオンからのdowncastやポインタ/cstring→stringは正当なので対象外である。
- 整数リテラルの縮小キャストで値がターゲット型の範囲に収まらない場合は警告する（`primary.cpp:179-218`）。`300 as tiny`は44に切り捨てられるが、この警告が無いと無音で通る。
- `is`はユニオン型の値にのみ許可され、ターゲット型が変種のいずれかであることを検査して`bool`を返す（`primary.cpp:118-140`）。非ユニオンへの`is`と変種にない型の指定はエラーになる。

なお暗黙変換（キャストなしの代入互換）は`types_compatible`が担い、数値型どうしは暗黙互換（`compat.cpp:255-258`）、配列→ポインタdecayやstring↔cstring等のFFI互換も同所にある（`compat.cpp:274-304`）。`as`はこの暗黙互換の外にある変換（縮小・ポインタ⇄整数・ユニオン出し入れ）を明示するための演算子という位置づけである。

### HIR/MIRでの表現

HIRは`HirCast { operand, target_type, check_only }`（`src/internal/hir/nodes.hpp:168-174`）で、AST→HIRの変換は`src/internal/hir/lowering/expr.cpp:309-320`が行う（`is`は結果型boolに差し替える）。MIRでは`MirRvalue::Cast`と`CastData { operand, target_type, check_only }`（`src/internal/mir/nodes.hpp:188`・`:224-229`）になり、`ExprLowering::lower_cast`（`src/internal/mir/lowering/expr/cast.cpp:91-151`）が生成する。ここに2つの重要な前処理がある。

```cpp
// src/internal/mir/lowering/expr/cast.cpp:97-99
// typedefエイリアス（Shape = Circle | Rect 等）を解決してからCastを発行する。
// 未解決のままだとバックエンドがユニオン構築/タグ検査を認識できない
hir::TypePtr target_type = ctx.resolve_typedef(cast.target_type);
```

もう1つは固定長配列→ポインタキャストのarray-to-pointer decayで、配列オペランドに暗黙の`Ref`（`&arr`）を挿入してからポインタキャストする（`cast.cpp:113-141`）。これが無いと`b as void*`で配列全体が値コピーされる。チェーンキャストはHIR/MIRでも段ごとに独立した一時変数とCast文になるため、`300 as tiny as int`は「truncで44→sextで44」と段階ごとの意味論が適用される。

### codegenでの意味論（rvalue.cpp）

LLVM IRの生成は`src/internal/codegen/llvm/core/rvalue.cpp:68-503`のCast右辺値処理が唯一の箇所で、native（オブジェクト出力）とjit（LLJIT実行）はこのIRを共有する。命令選択の要点は次の通り（詳細な根拠と防ぐバグは[../codegen-native/numeric-and-casts.md](../codegen-native/numeric-and-casts.md)参照）。

| 変換 | 生成箇所 | 意味論 |
|---|---|---|
| float↔double | `rvalue.cpp:111-116` | `fpext`/`fptrunc` |
| 整数→浮動小数 | `rvalue.cpp:118-133` | ソース型が符号なしなら`uitofp`、符号ありなら`sitofp` |
| 浮動小数→整数 | `rvalue.cpp:134-148` | `llvm.fptosi.sat`/`fptoui.sat`による飽和（範囲外はclamp、NaNは0） |
| 整数の拡大 | `rvalue.cpp:151-175` | ソース型のsignednessで`zext`/`sext`を選ぶ |
| 整数の縮小 | `rvalue.cpp:176-177` | `trunc`（下位ビット切り捨て・ラップ） |
| ポインタ間 | `rvalue.cpp:437-454` | opaque pointersでは実質no-op |
| 整数→ポインタ / ポインタ→整数 | `rvalue.cpp:456-459`・`:497-500` | `inttoptr`/`ptrtoint` |
| 値→ユニオン / ユニオン→値 | `rvalue.cpp:181-294`・`:394-496` | タグ+ペイロード構築／タグ検査付き取り出し（[union-types.md](union-types.md)参照） |

Cmコードでの対応例。

```cm
const utiny u = 255;
const int a = u as int;          // zext → 255（ソース符号なし判定）
const tiny t = 300 as tiny;      // 型検査で縮小警告、実行時はtruncで44
const int b = 3.9e10 as int;     // fptosi.satでINT_MAXへ飽和
const void* p = &a as void*;     // ポインタキャスト（opaque ptrでno-op）
const ulong addr = p as ulong;   // ptrtoint
```

### enumのキャスト

値enum（ペイロードなし）は`resolve_typedef`が基底のint表現へ解決するため、`Color::Red as int`は整数キャストの経路をそのまま通る。一方でペイロード付きバリアントを持つenum（`Result`/`Option`等のタグ付きunion）は一律int化するとペイロードが失われるため、`resolve_typedef`はペイロード付きバリアント（`member.has_data()`）を持つenumを解決せずに保持する（`src/internal/types/checking/utils/compat.cpp:34-71`）。この振り分けにより「値enumは整数として自由にキャストでき、タグ付きenumは構造体表現のまま運ばれる」という二層の扱いになる。

### チェーンキャストの扱い

`parse_cast_expr`のwhileループ（`binary.cpp:331-349`）により、`x as A as B`は`(x as A) as B`の左結合ネストとして解析される。型検査は内側から順に各段へ通常規則を適用し（`primary.cpp:111-219`の再帰）、HIR/MIRでも段ごとに独立の一時変数とCast文へ展開される（`src/internal/mir/lowering/expr/cast.cpp:143-150`）。中間段を融合する最適化は行わないため、各段の意味論（切り捨て・拡張・飽和）が観測可能な形で順に適用される。

```cm
const int c = 300 as tiny as int;   // trunc(300)=44 → sext(44)=44（300には戻らない）
const double d = -1 as uint as double;  // 同幅ビット再解釈の後にuitofp → 4294967295.0
```

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/syntax/parser/expr/binary.cpp:326-352` | `as`/`is`の解析（優先度・チェーン・`type_check`フラグ） |
| `src/internal/syntax/ast/expr.hpp:541-549` | `CastExpr`ノード定義 |
| `src/internal/types/checking/expr/primary.cpp:111-219` | 許可規則の検査（数値→string拒否・縮小警告・`is`の変種検査） |
| `src/internal/hir/nodes.hpp:168-174` / `src/internal/hir/lowering/expr.cpp:309-320` | `HirCast`表現とAST→HIR変換 |
| `src/internal/mir/nodes.hpp:224-229` / `src/internal/mir/lowering/expr/cast.cpp:91-151` | `CastData`表現とtypedef解決・配列decay挿入 |
| `src/internal/codegen/llvm/core/rvalue.cpp:68-503` | LLVM IR生成（native/jit共有の唯一の意味論定義点） |
| `src/internal/mir/passes/scalar/sccp.cpp:872-878` | 定数伝播はCastのオペランド置換のみ行い、キャスト自体の畳み込みで意味論を複製しない |

## 落とし穴とケア

- 数値意味論の不変条件（浮動小数→整数の飽和intrinsic固定、拡大の符号判定はソース型）は`rvalue.cpp`が単一の定義点であり、ここを迂回する変換パスを増やすとバックエンド間で挙動が分裂する。詳細は[../codegen-native/numeric-and-casts.md](../codegen-native/numeric-and-casts.md)の落とし穴を参照。
- MIR loweringでターゲット型のtypedefを解決してからCastを発行する不変条件を守ること（`cast.cpp:97-102`）。未解決の別名型がバックエンドに届くとユニオン構築・タグ検査が認識されず、無音のビット再解釈になる。
- 固定長配列オペランドへのポインタキャストは暗黙`Ref`挿入（`cast.cpp:113-141`）を経由する。これを外すと配列全体の値コピーと不正なポインタ値という2種類のバグが再発する。
- 数値→stringの拒否（`primary.cpp:144-178`）は型検査段の防壁である。文字列化は`as`ではなく文字列補間・`cm_*_to_string`系の経路を使う設計であり、この拒否を緩めるとポインタ再解釈による実行時クラッシュのクラスが戻る。
- 回帰テストの場所: `tests/common/casting/`（`as_cast.cm`・`cast_comprehensive.cm`・`numeric_cast.cm`・`float_int_saturation.cm`・`narrowing_literal.cm`・`ptr_cast.cm`）、`tests/common/types/ptr_to_int_cast.cm`、型エラー側は`tests/common/errors/cast_num_to_string.cm`・`union_cast_mismatch.cm`。

## 関連資料

- [数値出力とキャストの一貫性](../codegen-native/numeric-and-casts.md) — 命令選択の根拠・飽和/符号拡張の防ぐバグ・書式化との統一
- [ユニオン型の設計](union-types.md) — `as`によるユニオン構築/取り出しとタグ検査
- [型推論の設計](inference.md) — 暗黙変換（types_compatible）と明示キャストの境界
- [MIR→LLVM IR変換の構造](../codegen-native/mir-to-llvm.md) — Cast右辺値を含む変換全体の位置づけ
- [MIRの設計](../pipeline/mir-design.md) — Rvalue/Statementの表現とパス構成
