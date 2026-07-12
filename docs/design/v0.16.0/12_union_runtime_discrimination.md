# 実装設計12: ユニオン型の実行時型判別（`is` 演算子・match型パターン）

## 背景

typedefユニオン（`typedef Value = int | string`）はタグ付きで格納されるが、実行時に安全に変種を判別する構文が存在しなかった。`as` はタグ検査つき（不一致で実行時パニック）のため、取り出す前に判別する手段が必要で、設計11（可変長引数のユニオンスライス糖衣）の前提機能でもある。

## 構文と意味論

### `is` 演算子

```cm
typedef Value = int | string;

Value v = 42;
if (v is int) {
    int n = v as int;  // タグ検査済みなので安全
    println("int: {n}");
}
```

- `expr is Type` は bool を返す。実行時にユニオンのタグを検査し、アクティブな変種が `Type` なら true。
- 左辺はユニオン型（typedefユニオン）の値であること。非ユニオン値への適用はコンパイルエラー。
- 右辺の型はそのユニオンの変種のいずれかであること。変種にない型はコンパイルエラー（常にfalseになる検査は書き間違いとみなす）。
- 優先順位は `as` と同じキャスト水準（`a is int == b` は `(a is int) == b`）。

### match型パターン

```cm
match (v) {
    int i => println("int: {i}"),
    string s => println("str: {s}"),
    _ => println("other"),
}
```

- パターン `Type binder` はユニオンの実行時タグが `Type` に一致したときにマッチし、`binder` にペイロード値（型 `Type`）を束縛する。
- 脱糖は `v is Type` による分岐 + `v as Type` による束縛（`as` のタグ検査はisで確認済みのため成功する）。
- ガード（`Type binder if cond`）・ブロック形式アームは既存matchと同様に使用できる。
- 網羅性検査は行わない（既存matchと同じ扱い。マッチしない場合は既定値/何もしない）。

## 実装方針

新ノードを増やさず、既存のキャスト経路にフラグを追加して最小侵襲で通す。

1. **Lexer**: `is` キーワード（`KwIs`）を追加。
2. **AST**: `CastExpr` に `bool type_check = false` を追加。パーサは `expr is Type` を `CastExpr{operand, type, type_check=true}` として生成（`as` と同じ後置位置で解析）。
3. **TypeChecker**: `type_check` のとき、(a) オペランドがユニオン型か検査、(b) 対象型が変種に含まれるか検査、(c) 式の型は bool。
4. **HIR**: `HirCast` に `check_only` を追加。式の型は bool。
5. **MIR**: `MirRvalue::CastData` に `check_only` を追加（既定false）。
6. **LLVM codegen**: `check_only` のCastは、ユニオンポインタからタグ（フィールド0のi32）をロードし対象変種インデックスと比較したboolを返す（`as` のタグ検査と同じ判定基準を共有）。
7. **JSバックエンド**: タグ付き表現（実装設計13）のタグ比較で判定する。
8. **match型パターン**: パーサに `MatchPatternKind::Type`（`TypePtr type` + `binding_name`）を追加し、HIRのmatch loweringで `is` 検査 + `as` 束縛へ脱糖する。

## テスト計画

- `tests/common/union/union_is.cm`: is演算子の真偽（int/string/構造体変種）、if分岐との併用、`as` との併用。
- `tests/common/union/union_match_type.cm`: match型パターン（束縛・ワイルドカード・ブロック形式）。
- `tests/common/errors/union_is_non_union.cm`: 非ユニオン値への `is` はコンパイルエラー。
- `tests/common/errors/union_is_invalid_variant.cm`: 変種にない型の `is` はコンパイルエラー。
- 全バックエンド（JIT/native/JS/WASM）で同一結果を確認する。

## 段階

1. `is` 演算子（Lexer→Parser→TypeChecker→HIR→MIR→LLVM→JS）
2. match型パターン（パーサ + HIR脱糖。コード生成は1の成果物を再利用）
3. チュートリアル（ユニオン型ページ）・VSCode拡張（`is` キーワード）・リリースノート
