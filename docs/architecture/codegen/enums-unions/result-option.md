# Result/Option構文とLLVM IRの対訳

組み込みの `Result<T,E>` と `Option<T>` はユーザ定義のデータ付きenumと同一の機構でloweringされ、`%__TaggedUnion_Result` / `%__TaggedUnion_Option` という `{ i32 tag, [N x i8] payload }` 構造体になる。
本書はOk/Err/Some/Noneの構築、`?` 伝播演算子の早期return分岐、unwrap系メソッドのタグ検査とパニック分岐について、`CM_DUMP_IR` で採取した実際のO3 IRを対訳として示す。

### Ok/Errの構築

```cm
Result<int, string> divide(int a, int b) {
    if (b == 0) {
        return Result::Err("division by zero");
    }
    return Result::Ok(a / b);
}
```

`Result<int, string>` は最大variant（string分）のペイロードを持つ `%__TaggedUnion_Result = type { i32, [12 x i8] }` になり、宣言順にOkがタグ0、Errがタグ1を持つ。
最適化前の構築は「タグstore + ペイロードのゼロ拡張store」の列である。

```llvm
; -O3 最適化前（CM_DUMP_IR=1）: Result::Ok(q * 10) の構築
%field_ptr4 = getelementptr %__TaggedUnion_Result, ptr %local_9, i32 0, i32 0
store i32 0, ptr %field_ptr4, align 4
%field_ptr7 = getelementptr %__TaggedUnion_Result, ptr %local_9, i32 0, i32 1
%payload_zext = zext i32 %load6 to i96
store i96 %payload_zext, ptr %field_ptr7, align 16
```

O3ではResult値がメモリを離れて第一級の集約値になり、`insertvalue %__TaggedUnion_Result { i32 0, [12 x i8] poison }, …` の列でレジスタ上に組み立てられて値返しされる。
ペイロードが定数の `Err("…")` はタグと文字列ポインタを埋め込んだ定数集約に畳まれ、実行時の構築コードは残らない。

### Some/Noneの構築

```cm
Option<int> half(int x) {
    if (x % 2 == 0) {
        return Option::Some(x / 2);
    }
    return Option::None;
}
```

```llvm
define %__TaggedUnion_Option @half(i32 %arg0) local_unnamed_addr #0 {
entry:
  %0 = and i32 %arg0, 1
  %eq = icmp eq i32 %0, 0
  br i1 %eq, label %bb1, label %common.ret

common.ret:
  ; Option::None はタグ1・ペイロード全ゼロの定数集約
  %common.ret.op = phi %__TaggedUnion_Option [ %retval6.fca.1.11.insert, %bb1 ], [ { i32 1, [12 x i8] zeroinitializer }, %entry ]
  ret %__TaggedUnion_Option %common.ret.op

bb1:
  %div = sdiv i32 %arg0, 2
  ; Some(x / 2): タグ0の集約へ insertvalue でペイロードを詰める
  %struct_load.fca.1.0.insert = insertvalue %__TaggedUnion_Option { i32 0, [12 x i8] poison }, i8 %trunc, 1, 0
  ; …（残りバイトの insertvalue が続く）
```

ペイロードなしの `None` は即値 `{ i32 1, [12 x i8] zeroinitializer }` に畳まれ、構築命令が1つも発生しない。
`Some(v)` はタグ0の集約にペイロードを `insertvalue` で詰める形になり、SROAの影響でバイト単位の詰め込み列として現れる。
enum構築の脱糖規則は [enumとmatchのlowering](../../lowering/enums-and-match.md) と共通である。

### `?` 伝播演算子の早期return分岐

```cm
Result<int, string> calc(int a, int b) {
    const int q = divide(a, b)?;
    return Result::Ok(q * 10);
}
```

```llvm
define %__TaggedUnion_Result @calc(i32 %arg0, i32 %arg1) local_unnamed_addr #1 {
entry:
  %0 = tail call %__TaggedUnion_Result @divide(i32 %arg0, i32 %arg1)
  %.fca.0.extract = extractvalue %__TaggedUnion_Result %0, 0
  %cond = icmp eq i32 %.fca.0.extract, 0
  br i1 %cond, label %bb2, label %common.ret

common.ret:
  ; Errなら divide の戻り値 %0 をそのまま呼び出し元へ返す
  %common.ret.op = phi %__TaggedUnion_Result [ %retval8.fca.1.11.insert, %bb2 ], [ %0, %entry ]
  ret %__TaggedUnion_Result %common.ret.op

bb2:
  ; Okならペイロードを取り出して後続の計算を続ける
  %.fca.1.0.extract = extractvalue %__TaggedUnion_Result %0, 1, 0
  ; …（ペイロード再構成と q * 10、Ok再構築が続く）
```

`?` は「呼び出し結果のタグを `extractvalue` → タグ0（Ok/Some）なら継続ブロックへ、それ以外なら関数の戻りブロックへ分岐」という2-wayの早期return構造に脱糖される。
Err/None側は受け取った集約値 `%0` を再構築せずphi経由でそのまま返すため、エラー伝播のコストはタグ比較1回と分岐だけである。
`Option<T>` の `?` もタグ0（Some）判定で同一構造になり、None側は定数集約を返す。

### unwrap系メソッド

```cm
Result<int, string> r = safe_divide(10, 2);
must {
    int v = r.unwrap();
    int w = r.unwrap_or(-1);
    println("{v} {w}");
}
```

```llvm
%2 = tail call %__TaggedUnion_Result @safe_divide(i32 10, i32 2)
%.fca.0.extract = extractvalue %__TaggedUnion_Result %2, 0
%eq = icmp eq i32 %.fca.0.extract, 0
br i1 %eq, label %bb8, label %bb3

bb3:                                              ; Err側
  tail call void @__cm_panic(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [30 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
  unreachable

bb8:                                              ; Ok側: ペイロードを取り出して使用
  %.fca.1.0.extract = extractvalue %__TaggedUnion_Result %2, 1, 0
  ; …（ペイロード再構成が続く）
```

`unwrap()` はメソッド呼び出しとしてではなくインライン展開され、「タグ検査 → Errなら `@__cm_panic("called unwrap on an Err value")` で `unreachable` → Okならペイロード取り出し」の分岐になる。
`unwrap_or(default)` はパニック経路の代わりにデフォルト値とのselect/phiになり、この例ではタグがOkと判明している経路に置かれたためO3で検査ごと消えてunwrapと同じ値に合流している。
この例は最適化での除去を防ぐため `must {}` アンカー内に置いているが、unwrap自体のタグ検査分岐は `must` の有無に依存しない。

## 関連資料

- [型推論の設計](../../types/inference.md) — `Result<T,E>`/`Option<T>` の型引数が決まる推論の流れ
- [enumとmatchのlowering](../../lowering/enums-and-match.md) — 組み込みResult/Optionが通る共通のタグ付きunion機構
- [ユニオン型の設計](../../types/union-types.md) — 匿名ユニオンとの表現比較
- [enumのIR対訳](enum.md) — ユーザ定義enumの構築・match・受け渡しの対訳
- [LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md) — 定数集約への畳み込みやSROAを担うパス群
