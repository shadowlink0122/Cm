---
title: const集約への深い代入禁止（const構造体・const配列のフィールド/要素保護）
parent: v0.17.0 Design
---

# const集約への深い代入禁止（const構造体・const配列のフィールド/要素保護）

本文書は監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` のM3に対する実装設計である。
中核は「const構造体・const配列のフィールド/要素への代入を型検査で拒否し、const性を集約のメンバ・要素へ伝播させる」ことである。

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| M3 | 型システム | `const`構造体のフィールド代入が`--strict`でも検出されず実行される（constが集約に対して浅い） | 保留（破壊的変更のため。実装試行で既存テスト6件が壊れることを確認） |

## 背景と根本原因

### const代入検査がIdentExpr（単純変数）のみを対象にしている

代入の型検査は`TypeChecker::infer_binary`（src/internal/types/checking/expr/operator.cpp:20）にある。
constへの代入拒否は代入演算子ケース（operator.cpp:99-140）で行われるが、左辺が単純識別子`IdentExpr`のときだけである。

```cpp
// operator.cpp:100-106
if (auto* ident = binary.left->as<ast::IdentExpr>()) {
    auto sym = scopes_.current().lookup(ident->name);
    if (sym && sym->is_const) {
        error(binary.left->span,
              "Cannot assign to const variable '" + ident->name + "'");
        return ast::make_error();
    }
    ...
}
```

左辺が`MemberExpr`（`s.field = v`）や`IndexExpr`（`arr[i] = v`）の場合は、このブロックに入らず、後続の`else if`（デリファレンス経由、operator.cpp:143-160）にも該当しないため、const検査が一切行われない。
つまり`const Point p = ...; p.x = 99;`は型検査を素通りし、`--strict`でも実行される。

### 代入直前に基底変数を初期化済みマークしているが可変性は見ていない

`infer_binary`は代入時、左辺の基底変数（Member/Index/Sliceの連鎖を辿った先のIdentExpr）を初期化済み・変更ありとしてマークする（operator.cpp:40-60）。
この基底変数探索ロジックは可変性検査に流用できる形になっているが、現状は初期化・const推奨のマークにしか使われておらず、可変性（is_const）判定には接続されていない。

### const性が集約の要素・フィールドへ伝播していない

型推論では、フィールドアクセス`infer_member`（src/internal/types/checking/call/method.cpp:37）と要素アクセス`infer_index`（src/internal/types/checking/expr/operator.cpp:442）が、基底の`TypeQualifiers::is_const`（src/internal/syntax/ast/types.hpp:79）を結果型へ伝播しない。
`infer_index`は`obj_type->element_type`をそのまま返し（operator.cpp:456-464）、要素型のconst修飾を付けない。
このため「constな集約のメンバ・要素もまたconstである」という性質がどこにも表現されておらず、ポインタ経由の`Cannot assign through pointer to const`（operator.cpp:154-157）のような既存の要素const検査も、集約フィールド/要素には効かない。

`Symbol::is_const`（src/internal/types/scope.hpp:20）は変数単位の可変性であり、let宣言時に`let.is_const`から設定される（src/internal/types/checking/stmt.cpp:153-154, 215-220）。
この情報は変数には残るが、そこから派生するフィールド/要素の場所（place）には結び付いていない。

## 設計方針

「代入先の場所（place）がconstかどうか」を判定する共通ヘルパを導入し、代入検査を`IdentExpr`だけでなく`MemberExpr`・`IndexExpr`・`SliceExpr`の連鎖にも適用する。

### 方針1: 代入先placeのconst性判定ヘルパ

`is_place_const(ast::Expr& lhs)`（型検査内の新規ヘルパ）を追加し、左辺式を再帰的に辿ってconst性を判定する。

- `IdentExpr` → `scopes_.current().lookup(name)`の`Symbol::is_const`を返す。
- `MemberExpr`（`obj.field`） → 基底`obj`がconst placeなら、そのフィールドもconst（constの浅い伝播）。加えて、フィールド自身の型修飾（将来的にフィールド単位のconst宣言に対応する場合）も考慮できる形にする。
- `IndexExpr`（`obj[i]`）・`SliceExpr` → 基底`obj`がconst placeなら要素/スライスもconst。
- デリファレンス（`*p`） → ポインタの要素型const（既存のoperator.cpp:143-160の判定）に委譲。

この判定は既存の基底変数探索（operator.cpp:40-60）と同じ連鎖走査を可変性判定へ拡張する形で実装する。

### 方針2: 代入検査をplaceベースに統一

`infer_binary`の代入ケース（operator.cpp:99-140）を、左辺が`IdentExpr`に限らず`is_place_const(*binary.left)`が真なら拒否する形に変更する。

- 単純変数: 従来通り「Cannot assign to const variable 'x'」。
- フィールド: 「Cannot assign to field of const value」（対象名を含める。例: 「Cannot assign to 'p.x': 'p' is const」）。
- 要素: 「Cannot assign to element of const array」。

複合代入（`+=`等）も同じ経路を通るため、const集約への複合代入も自動的に拒否される。

### 方針3: const性の型伝播（要素/フィールドアクセス）

`infer_index`（operator.cpp:456-464）と`infer_member`（method.cpp:37）で、基底型が`qualifiers.is_const`を持つとき、返す要素型/フィールド型にも`qualifiers.is_const = true`を設定する。
これにより、const集約から取り出した部分式をポインタ化・エイリアスした場合にも既存の「pointer to const」検査（operator.cpp:154-157）が連鎖して効き、深いconstが型システム全体で一貫する。

型の共有（`TypePtr`はshared_ptr）に注意し、修飾を付ける際は元の型を破壊せずコピーしてから修飾する（他の非const参照へ波及させない）。

## 構文例・出力例

### M3: const構造体のフィールド代入（新規に拒否）

```cm
struct Point { int x; int y; }

int main() {
    const Point p = Point { x: 1, y: 2 };
    p.x = 99;   // 修正前: 素通りして実行される / 修正後: 診断
    return 0;
}
```

修正後の期待診断:

```
example.cm:5:5: エラー: const値 'p' のフィールド 'x' には代入できません
```

### const配列の要素代入（新規に拒否）

```cm
const int[3] a = [10, 20, 30];
a[0] = 99;   // 修正後: 診断
```

修正後の期待診断:

```
example.cm:2:1: エラー: const配列 'a' の要素には代入できません
```

### 複合代入・非const変数（挙動の対比）

```cm
const Point p = Point { x: 1, y: 2 };
p.x += 1;              // 修正後: 拒否（複合代入も同経路）

Point q = Point { x: 1, y: 2 };
q.x = 99;              // 非const → 従来通り許可
```

## 実装の段階分割

1. 段階1: `is_place_const`ヘルパを追加し、`infer_binary`の代入ケース（operator.cpp:99-140）をplaceベースの拒否に拡張する。まずフィールド代入（MemberExpr）を対象にする。
2. 段階2: `IndexExpr`・`SliceExpr`の要素代入へ対象を拡張する。
3. 段階3: `infer_index`・`infer_member`にconst修飾伝播を追加し、const集約から派生した部分式のポインタ化・エイリアスでも深いconstが効くようにする。
4. 段階4: 診断メッセージのi18n対応（`kMessages`テーブルへの追加、MEMORY.mdのcm-i18n-architecture準拠。断片連結を避け完成文で登録する）。

## テスト計画（tests/common/ 配下）

- tests/common/const/const_struct_field_assign/ — const構造体のフィールド代入が拒否されることを検証（M3・負テスト）。`.expect`はエラーメッセージを期待する。
- tests/common/const/const_array_element_assign/ — const配列の要素代入が拒否されることを検証（負テスト）。
- tests/common/const/const_compound_assign/ — const集約への複合代入（`+=`等）が拒否されることを検証（負テスト）。
- tests/common/const/nonconst_aggregate_ok/ — 非const構造体/配列のフィールド・要素代入が従来通り成功することを検証（正ケース・非回帰）。
- tests/common/const/const_nested_aggregate/ — ネストした集約（const構造体の中の配列フィールド）でも深いconstが効くことを検証。
- 全バックエンド（interpreter/llvm/llvm-wasm/js）で診断が一致することを確認する。既存のtests/common/const/の非回帰を確認する。

## リスクと非互換性

- **後方非互換（実測で確認）**: 実装を試行したところ、`const Outer o = {...}; o.inner = {a: 1000, b: 2000};` のように**const集約のフィールドへ意図的に代入する既存テストが多数存在**し、深いconstを強制すると `tests/common/structs/struct_literal`・`nested_literal_assign`・`struct_multidim_member`・`struct_nested_deep`・`must/must_struct`・`llvm/thread/thread_join_test` の6件が壊れることを確認した。つまりCmの現行仕様では「const変数の集約フィールドは代入可能（浅いconst）」が事実上の確立された挙動であり、M3の強制は破壊的変更になる。このため単純な追加検査としては実装せず**保留**した。
- **移行方針**: 実装するなら (1) まずWarningで先行し既存コードを移行、(2) 標準ライブラリ・tests/commonのconst集約フィールド代入を全て洗い出して修正、(3) その後にerror化、という段階が必須。あるいは `struct S { const int id; }` のようなフィールド単位constを先に導入し、変数レベルの深いconstは別キーワード（`readonly`等）で opt-in する案も検討する。
- **const推奨lintとの相互作用**: 現状のconst推奨警告（src/internal/types/checking/utils/diagnostics.cpp:32-49、非変更変数へのconst提案）が、深いconst導入で「実は変更されている」と判定される集約を非変更扱いしないよう、`mark_variable_modified`の基底変数マーク（operator.cpp:53-60）との整合を確認する。
- **型修飾の共有破壊**: `TypePtr`はshared_ptrで共有されるため、const伝播で修飾を付ける際にコピーせず書き換えると、同じ型を参照する非const箇所へ波及する恐れがある。伝播時は必ずコピーしてから修飾する。
- **フィールド単位constの将来拡張**: 本設計は「変数がconstならその集約全体が深くconst」という浅い起点からの深い伝播であり、構造体宣言でのフィールド単位const（`struct S { const int id; }`）は対象外。将来対応できるよう`is_place_const`はフィールド型修飾も見られる形にしておく。

## 関連

- 監査レポート: `docs/design/v0.17.0/large-scale-bottleneck-audit.md`（M3、型システム/構造体の領域別詳細）
- 関連所見C10（`2 as string`等の不正キャスト受理）・M4（整数縮小キャスト無警告）と同じ「型検査での拒否を強める」方向の修正群に属する。
- i18nメッセージ登録はMEMORY.mdのcm-i18n-architecture（`kMessages[msg][lang]`集約、断片連結禁止）に従う。
- 実コード: src/internal/types/checking/expr/operator.cpp, src/internal/types/checking/call/method.cpp, src/internal/types/scope.hpp, src/internal/syntax/ast/types.hpp, src/internal/types/checking/stmt.cpp
