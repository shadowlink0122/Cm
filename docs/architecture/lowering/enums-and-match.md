# データ付きenumとmatchのlowering

Cmのデータ付きenum（タグ付きunion）は `{ i32 tag, [N x i8] payload }` の固定2フィールド構造体としてメモリ表現され、match式はHIR lowering段で「タグ読み出し + 整数比較 + ペイロード抽出」の三項演算子/if-elseチェーンへ完全に脱糖される。
本書はenumの型表現とメモリレイアウト、matchのタグ判定・ペイロード取り出しの変換、および網羅性検査（E500番台）が行われる段を記述する。

## 概要

データ付きenumは「どのバリアントかを示す整数タグ」と「最大バリアントサイズ分のペイロード領域」を持つタグ付きunionであり、バリアントごとの個別構造体型は作らない。
matchはMIRやLLVMに専用ノードを持たず、HIR段で `scrutinee.__tag` の整数比較と `__payload` の抽出という汎用の構造体フィールドアクセスに帰着させる。
この設計により、MIR以降のすべての段（最適化パス・native/jitコード生成）はenumとmatchを「普通の構造体と分岐」として扱えばよく、enum固有の処理はHIR loweringとLLVM型生成の2箇所に閉じる。
網羅性検査はコンパイルパイプラインの型検査段（AST上）で完結し、lowering以降は「armが網羅されている」ことを前提にできる。
組み込みの `Option<T>` / `Result<T,E>` もユーザ定義enumと同一の機構（同じタグ登録・同じ `__TaggedUnion_` レイアウト）を通る。

## データ構造とアルゴリズム

### 型表現: AST → HIR → MIR

ASTでは `struct EnumMember`（`src/internal/syntax/ast/decl.hpp:314-338`）がバリアントを表し、明示タグ値 `std::optional<int64_t> value` とペイロード `std::vector<std::pair<std::string, TypePtr>> fields` を持つ（設計上フィールドは0または1個）。
`struct EnumDecl`（`decl.hpp:341-361`）はメンバのいずれかがペイロードを持てば `is_tagged_union()` が真になる。
HIRにはenum構築と抽出の専用ノードがある（`src/internal/hir/nodes.hpp:176-191`）。

```cpp
// src/internal/hir/nodes.hpp:178-191
// 例: Option::Some(42) → { tag: 1, payload: 42 }
struct HirEnumConstruct {
    std::string enum_name;
    std::string variant_name;
    int64_t tag_value;
    HirExprPtr payload;        // nullptrなら引数なし
};
struct HirEnumPayload {
    HirExprPtr scrutinee;      // Tagged Union式
    std::string variant_name;  // 期待するバリアント名
    TypePtr payload_type;
};
```

MIR側の定義は `MirEnumMember` / `MirEnum`（`src/internal/mir/nodes.hpp:505-527`、`is_tagged_union()` と `max_payload_size()` を持つ）である。
バリアントの登録は型検査の `TypeChecker::register_enum`（`src/internal/types/checking/decl.cpp:896`）が行い、`"Enum::Variant"` の完全名でタグ値を `enum_values_` に登録し（`decl.cpp:934-939`）、ペイロード付きバリアントはコンストラクタ関数としてグローバルスコープへ登録する（`decl.cpp:961`）。
組み込みの `Result`（Ok=0/Err=1）と `Option`（Some=0/None=1）は `register_builtin_types`（`src/internal/types/checking/auto_impl.cpp:416-479`）が同じ `enum_values_` / `generic_enums_` へ登録する。

### メモリレイアウト: __TaggedUnion_ 構造体

レイアウト決定はLLVM型生成にある（`src/internal/codegen/llvm/core/types.cpp:404-452`）。
型名が `__TaggedUnion_` で始まる場合、enum定義から各バリアントのフィールド型を一時 `StructType` に組み、`DataLayout::getTypeAllocSize`（パディング込み）で正確なサイズを計算して最大値をペイロードサイズとする（`types.cpp:411-441`、ペイロードなしの既定は8バイト）。

```cpp
// src/internal/codegen/llvm/core/types.cpp:443-448
auto structType = llvm::StructType::create(ctx.getContext(), lookupName);
std::vector<llvm::Type*> fieldTypes = {
    ctx.getI32Type(),                                      // tag (field[0])
    llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize)  // payload (field[1])
};
structType->setBody(fieldTypes);
```

タグは常に `i32` でオフセット0、ペイロードは `i8` 配列で全バリアントが同じ領域を共有する。
`Result__int__string` のような単相化名はベースenumの `__TaggedUnion_` 型へ正規化され、同一レイアウトを共有する（`types.cpp:385-402`）。
なおMIR自体はサイズ・オフセットを一切保持せず、LLVMのDataLayoutを唯一の情報源とする（`src/internal/mir/nodes.hpp:481-483` の方針コメント）。

### enum構築とペイロード抽出のMIR lowering

`ExprLowering::lower_enum_construct`（`src/internal/mir/lowering/expr/construct.cpp:323`）は `__TaggedUnion_{enum名}` 型の一時ローカルを作り、`field(0)` へタグ定数、`field(1)` へペイロード（なければ0）を代入する2文に展開する（`construct.cpp:336-363`）。
`ExprLowering::lower_enum_payload`（`construct.cpp:371`）はscrutineeの `field(1)` を結果ローカルへコピーするだけの1文である（`construct.cpp:380-384`）。
HIRが発行する仮想フィールド名 `__tag` / `__payload` は、MIRのメンバアクセスloweringで `field(0)` / `field(1)` の射影へ写像される（`src/internal/mir/lowering/expr/access.cpp:127-140`）。
ペイロードを持たない単純enumは構造体化されず整数のまま表現され、その `__tag` アクセスは射影を追加しない恒等アクセスになる（`access.cpp:69-106`）。

### matchのlowering: タグ判定と分岐

match式はHIR lowering段で脱糖され、MIRにmatch専用ノードは存在しない。
式形式は `HirLowering::lower_match`（`src/internal/hir/lowering/expr_match.cpp:19`）がarmを逆順に走査し、`build_match_condition` で条件式を作って `HirTernary` をネストさせる（ワイルドカード/変数armはデフォルト値、`expr_match.cpp:65-82`）。
タグ判定は、tagged unionなら `enum_values_` からタグ値を整数リテラルとして直接生成し（`expr_match.cpp:291-298`、enum構築式を作ると構造体型になり `__tag` との比較で型不一致になるためというコメント付き）、`scrutinee.__tag` を `HirMember` で抽出して比較する（`expr_match.cpp:305-311`）。
バインディング付きパターン（`Some(x) => ...`）は `HirEnumPayload` を生成してarm本体の束縛変数に接続し、ペイロード型はenum定義の該当メンバから解決する（`expr_match.cpp:121-138`、ガード内の置換は `lower_guard_with_binding`）。
文形式は `lower_match_as_stmt`（`src/internal/hir/lowering/stmt.cpp:634`）がscrutineeを一時変数 `__match_scrutinee_N` に束縛し（`stmt.cpp:670-671`）、if-elseチェーンへ変換して、バインディングは `HirEnumPayload` + `HirLet` で導入する（`stmt.cpp:722-773`、ジェネリックenumのペイロード型具体化は `stmt.cpp:745-753`）。
以降はMIRの通常の `SwitchInt`/`Goto` 終端命令と構造体フィールドアクセスに落ち、LLVMでは汎用のフィールドGEP（`CreateGEP(structType, addr, {0, field_id})`、`src/internal/codegen/llvm/core/operand.cpp:729-734`）としてnative/jit共通の `MIRToLLVM` が処理する。
union型キャスト経路（typedef union）ではペイロードの格納・抽出を `CreateStructGEP(structTy, alloca, 1, ...)` + memcpy/bitcastで行い（`src/internal/codegen/llvm/core/rvalue.cpp:260-287`、抽出は `rvalue.cpp:412-422`）、期待タグとの実行時照合に失敗すると `invalid union cast` のpanicを生成する（`rvalue.cpp:298-351`）。

### 網羅性検査（E500番台）が行われる段

網羅性検査はloweringより前、型検査段（AST上）で行われる。
`TypeChecker::infer_match`（`src/internal/types/checking/expr/match.cpp:23`）が末尾で `check_match_exhaustiveness`（呼び出し `match.cpp:158`、実装 `match.cpp:169`）を呼ぶ。
検査は各armのパターン種別を集計し、ワイルドカードまたはガードなし変数束縛があれば網羅とみなして早期リターンする（`match.cpp:244-246`）。
bool型は `true`/`false` 両方のカバーを要求し（`match.cpp:248-255`）、enum型は `enum_values_` から `Enum::` 接頭辞で全バリアントを列挙して未カバーを検出し（`match.cpp:258-290`）、整数型はワイルドカード必須である（`match.cpp:294-296`）。
enum/match系の診断コードはE500番台としてカタログに定義されている: E500 `non-exhaustive-match`、E501 `duplicate-match-arm`、E502 `invalid-enum-variant`、E503 `match-guard-type-error`、E504 `unreachable-match-arm`（`src/internal/diagnostics/definitions/errors.hpp:82-96`、日英メッセージは `src/internal/base/messages/messages.cpp:442-456`）。

## 実装箇所

| 役割 | ファイル |
|---|---|
| enum宣言AST | `src/internal/syntax/ast/decl.hpp:314-361` |
| バリアント登録・タグ値・コンストラクタ登録 | `src/internal/types/checking/decl.cpp:896-975` |
| 組み込みOption/Result登録 | `src/internal/types/checking/auto_impl.cpp:416-479` |
| 網羅性検査（型検査段） | `src/internal/types/checking/expr/match.cpp:23,158,169-296` |
| E500番台の診断定義 | `src/internal/diagnostics/definitions/errors.hpp:82-96`, `src/internal/base/messages/messages.cpp:442-456` |
| HIRのenum構築/抽出ノード | `src/internal/hir/nodes.hpp:176-200` |
| match脱糖（式形式/文形式） | `src/internal/hir/lowering/expr_match.cpp`, `src/internal/hir/lowering/stmt.cpp:634-` |
| enum構築/抽出のMIR lowering | `src/internal/mir/lowering/expr/construct.cpp:323-385` |
| __tag/__payload → field射影 | `src/internal/mir/lowering/expr/access.cpp:69-140` |
| __TaggedUnion_レイアウト生成（LLVM） | `src/internal/codegen/llvm/core/types.cpp:385-487` |
| フィールドGEP・unionキャストのタグ照合 | `src/internal/codegen/llvm/core/operand.cpp:533-734`, `src/internal/codegen/llvm/core/rvalue.cpp:260-422` |

## 落とし穴とケア

- `__tag` = field(0) / `__payload` = field(1) という仮想フィールド名は、HIR脱糖（`expr_match.cpp`）とMIRアクセスlowering（`access.cpp:134-140`）の間の契約である。どちらか一方だけ変更するとタグ比較が別フィールドを読む静かな誤答になるため、必ず両側を同期させること。
- タグ判定の比較値は必ず整数リテラルで生成する（`expr_match.cpp:291-294`）。パターン式を普通にlowerすると `HirEnumConstruct`（構造体型）が返り、`__tag`（int）との比較で型不一致になる。この規約が「構造体と整数の比較」という型崩れのバグのクラスを防いでいる。
- ペイロード領域のサイズはLLVM DataLayoutの `getTypeAllocSize`（パディング込み）で計算する（`types.cpp:431-434`）。手計算のサイズ表を持ち込むと、アラインメントの差でペイロードが切り詰められるバグを再導入する。レイアウト情報はMIRに持たせない方針（`mir/nodes.hpp:481-483`）を維持すること。
- 単相化されたenum名（`Result__int__string` 等）はベースの `__TaggedUnion_` 型へ正規化される（`types.cpp:385-402`）。型キャッシュのキーを変更する場合、同一enumの単相化インスタンス同士でレイアウトが分裂しないことを確認する。
- ペイロードを持たない単純enumは整数表現のままであり、tagged unionの構造体レイアウトを仮定してはならない（`access.cpp:69-106` の恒等アクセス経路）。enum関連の処理を追加するときは `is_tagged_union` 判定（型名 `__TaggedUnion_` 接頭辞または `enum_defs` 登録、`access.cpp:127-132`）を通すこと。
- match本体のペイロード抽出（`lower_enum_payload`）自体はタグを再検査しない。安全性は「タグ比較で分岐した後のarmでのみ抽出される」という脱糖の構造に依存しているため、arm条件と抽出の対応を崩す変更をしてはならない（一方、unionキャスト経路は実行時タグ照合とpanicを持つ: `rvalue.cpp:298-351`）。
- 網羅性検査は型検査段のみで行われ、lowering以降に安全網はない。Range/Or等の一部パターンはカバー集計の対象外でワイルドカードに依存するため（`match.cpp:229-241`）、パターン種別を追加するときは網羅性集計への追加も検討すること。
- 網羅性エラーの発行はカタログのE500定義と対応する一方、検査経路は `error(...)` に生成した文字列を直接渡している（`match.cpp:265-295`）。診断メッセージを変更するときはカタログ（`errors.hpp` / `messages.cpp`）と発行箇所の両方を確認すること。
- 回帰テスト: `tests/common/enum/`（`assoc_match.cm`・`match_extract.cm` 等）、`tests/common/match/`（`enum_exhaustive.cm` 等）、`tests/common/types/`（`enum*.cm`・`union_match_type.cm`）、ネガティブは `tests/common/errors/match_enum_non_exhaustive.cm`・`match_non_exhaustive.cm`・`union_cast_mismatch.cm` にある。

## 関連資料

- [コンパイルパイプライン全体像](../pipeline/overview.md)
- [MIRの設計](../pipeline/mir-design.md)
- [クロージャのlowering](closures.md)
