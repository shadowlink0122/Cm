# 実装設計13: Rust準拠のResult/Optionエラーハンドリング

## 背景

組み込みの `Result<T, E>` / `Option<T>` はTypeChecker内の疑似登録のみで、HIR/MIR/コード生成にenum定義が届いておらず、(1) 関数返却でペイロードが失われる、(2) 型IDが分裂して値が壊れる、(3) メソッド（is_ok等）が未解決、(4) `Option::None` が未定義変数になる、という状態だった。エラーハンドリングをResult型へ統一するため、Rustのセマンティクスに揃えて実装し直す。

## 意味論（Rust準拠）

```cm
Result<int, string> divide(int a, int b) {
    if (b == 0) {
        return Result::Err("division by zero");
    }
    return Result::Ok(a / b);
}

int use_it() {
    Result<int, string> r = divide(10, 2);
    if (r.is_ok()) {
        return r.unwrap();
    }
    return -1;
}

Result<int, string> chained(int a, int b) {
    int q = divide(a, b)?;   // Errなら呼び出し元へそのまま伝播
    return Result::Ok(q * 10);
}
```

- **構築**: `Result::Ok(v)` / `Result::Err(e)` / `Option::Some(v)` / `Option::None`。
- **判別**: matchのバリアントパターン（`Result::Ok(v) =>`）、または `is_ok()/is_err()/is_some()/is_none()`。
- **取り出し**: `unwrap()`（Err/Noneでパニック）、`expect(msg)`（メッセージ付きパニック）、`unwrap_or(default)`、`unwrap_err()`（OkでパニックしEを返す）。パニックは「panic: <メッセージ>」を出力して異常終了（Rustのpanic!相当）。
- **`?` 演算子**: `expr?` はOk/Someならペイロードを返し、Err/Noneなら**現在の関数からそのまま早期return**する。Resultの`?`はResultを返す関数の中でのみ、Optionの`?`はOptionを返す関数の中でのみ使用できる（型検査）。三項演算子（`cond ? a : b`）とは`?`の次のトークンが式を開始し得るかの先読みで区別する。
- **must_use（静的チェック）**: Result型の値を文として捨てると `[must_use]` 警告を出す（Rustのunused_must_use相当）。エラーの取りこぼしをコンパイル時に検出する。

## 実装

1. **prelude注入**: `TypeChecker::check` の冒頭で `Result<T,E>` / `Option<T>` を実enum宣言（`__prelude`属性付き）としてプログラム先頭に注入する。以降は通常のジェネリックenum（Tagged Union）として全パイプラインを流れる。ユーザーが同名enumを定義している場合は注入しない。
2. **ペイロード付きenumの型解決修正**: `resolve_typedef` が非ジェネリックのペイロード付きenum（`enum IntResult { Ok(int), Err(string) }`）も一律intへ解決していたため、Tagged Unionとして保持するよう修正（関数返却でのペイロード喪失の根本原因）。
3. **モノモーフィズドenum名の型正規化**: コード生成で `Result__int__string` 等の未知名が同レイアウト別IDの構造体にフォールバックし型が分裂していたため、ベースenumの `__TaggedUnion_<base>` 型へ正規化。
4. **メソッド脱糖**: `is_ok/is_err/is_some/is_none/unwrap/unwrap_or/unwrap_err/expect` はHIR loweringでタグ比較・ペイロード取り出し・panicへ脱糖する（TypeCheckerはベース名で解決し、ジェネリックパラメータを型引数で置換）。
5. **`?` のMIR降下**: タグ読み出し→`switch_int`で分岐→Err/Noneはユニオン値を戻り値ローカルへ丸ごとコピーして`return`、Ok/Someはペイロード（field 1）を取り出して継続。`Result<T,E>`と`Result<U,E>`は同一のタグ付きユニオン表現のため直接コピーできる。MIRレベルの降下のためLLVM系・JS・WASMの全バックエンドで共通に動作する。
6. **matchのscrutinee一時変数束縛**: `match (f())` のようにクローン不能な式を直接matchすると条件・ペイロード抽出がダミー値に落ちる問題を修正（一時変数へ束縛してから降下）。
7. **panicランタイム**: 呼び出しを `void __cm_panic(const char*)` へ正規化（誤ったシグネチャ宣言によるwasmのsignature mismatch防止）。WASMランタイムに `panic`/`__cm_panic` を追加、JSバックエンドにpanicビルトインを追加（全バックエンドで「panic: <msg>」形式）。

## 制限（今後の課題）

- `Result<構造体, E>` のようにペイロードが8バイトを超える変種はモノモーフィズド型の正規化（単一の`__TaggedUnion_Result`型）ではペイロード領域が不足する。インスタンス化ごとの型生成が必要。
- ユニオン引数への暗黙変換（`f(42)` で `Value` 引数へ渡す）は未対応（設計11の範囲）。
- `map/and_then/ok_or` 等の高階メソッドはクロージャ渡しの型推論整備後に追加する。
- `?` のE型変換（RustのFrom<E>）は未対応。E型が一致する場合のみ伝播できる。

## テスト計画

- `tests/common/types/result_option_basic.cm`: 構築・関数返却・match・全メソッド・call式の直接match。
- `tests/common/types/result_try_operator.cm`: `?` のOk/Err・Some/None伝播、三項演算子との共存。
- `tests/common/errors/result_try_non_result.cm`: 非Result/Option値への `?` はコンパイルエラー。
- 全バックエンド（JIT/native/JS/WASM）で同一結果を確認する。
