# ユニオン型構文とLLVM IRの対訳

Cmのユニオン型（`int | string` など）は、メンバ型の宣言順を整数タグとする `{ i32 tag, [N x i8] payload }` の匿名タグ付きunion構造体 `%__anon_union_N` に変換される。
本書はユニオン変数の構築store列、`is` のタグ比較、`as` 絞り込みのタグ検査付き取り出し、matchの型パターンについて、`CM_DUMP_IR` で採取した実際のO3 IRを対訳として示す。

### ユニオン型変数の構築

```cm
typedef Value = int | string;

Value a = 42;
Value b = "hello";
```

`int | string` は最大メンバサイズ分のペイロードを持つ `%__anon_union_8 = type { i32, [8 x i8] }` になり、代入は「メンバ型に対応するタグのstore + ペイロードのstore」の列に変換される。

```llvm
; Value a = 42;（int はタグ0）
%tag_ptr = getelementptr inbounds %__anon_union_8, ptr %union_temp, i32 0, i32 0
store i32 0, ptr %tag_ptr, align 4
%payload_ptr = getelementptr inbounds %__anon_union_8, ptr %union_temp, i32 0, i32 1
store i32 42, ptr %payload_ptr, align 4

; Value b = "hello";（string はタグ1、ペイロードは文字列定数へのptr）
store i32 1, ptr %tag_ptr2, align 4
store ptr getelementptr inbounds (i8, ptr @strh, i64 16), ptr %payload_ptr3, align 8
```

再代入も同じ構築列を通るため、タグとペイロードは常に対で更新される。
タグ割り当てと代入互換の規則は [ユニオン型の設計](../../types/union-types.md) を参照。

### `is` 演算子のタグ比較

```cm
int describe(Value v) {
    if (v is int) {
        const int n = v as int;
        return n;
    }
    return 0;
}
```

```llvm
define i32 @describe(%__anon_union_8 %arg0) local_unnamed_addr #0 {
entry:
  %arg0.fca.0.extract = extractvalue %__anon_union_8 %arg0, 0
  %union_is = icmp eq i32 %arg0.fca.0.extract, 0
  br i1 %union_is, label %union_tag.cont, label %common.ret
```

`v is int` はタグフィールドと `int` のタグ値との `icmp eq` 1命令に変換され、実行時型情報のテーブル引きなどは発生しない。
ユニオン値は第一級の集約値として `%__anon_union_8` のまま値渡しされ、O3ではメモリを経由せず `extractvalue` でタグを取り出す。
`is` ガード内の `v as int` はO3でタグ検査が分岐条件と重複するため除去され、ペイロード読み出しだけが残る。

### `as` 絞り込みのタグ検査付き取り出し

```cm
int force_int(Value v) {
    return v as int;
}
```

```llvm
define i32 @force_int(%__anon_union_8 %arg0) local_unnamed_addr {
entry:
  %arg0.fca.0.extract = extractvalue %__anon_union_8 %arg0, 0
  %union_tag.check.not = icmp eq i32 %arg0.fca.0.extract, 0
  br i1 %union_tag.check.not, label %union_tag.cont, label %union_tag.fail

union_tag.fail:
  %0 = tail call i32 @puts(ptr nonnull dereferenceable(1) @panic_msg)
  tail call void @exit(i32 1)
  unreachable

union_tag.cont:
  ; ペイロード領域 [8 x i8] の先頭バイト列を i32 に組み立て直して返す
  %arg0.fca.1.0.extract = extractvalue %__anon_union_8 %arg0, 1, 0
  ; …（zext/shl/or によるバイト再構成が続く）
```

ガードなしの `as` は「タグ検査 → 不一致なら `union_tag.fail` でパニックメッセージ出力と `exit(1)` → 一致なら `union_tag.cont` でペイロード取り出し」の3ブロック構造になる。
`[8 x i8]` からの `i32` 復元は、値渡しされた集約をSROAがバイト単位に分解した結果として `zext`/`shl`/`or` の組み立て列になるが、意味的には単なるペイロードloadである。
キャストパイプラインにおけるユニオン出し入れの位置づけは [`as`キャストの設計](../../types/casts.md) を参照。

### matchの型パターン

```cm
typedef Value = int | string | bool;

string kind(Value v) {
    match (v) {
        int n => { return "int"; }
        string s => { return "string"; }
        bool b => { return "bool"; }
    }
    return "?";
}
```

```llvm
define nonnull ptr @kind(%__anon_union_8 %arg0) local_unnamed_addr #0 {
entry:
  %arg0.fca.0.extract = extractvalue %__anon_union_8 %arg0, 0
  %0 = icmp ult i32 %arg0.fca.0.extract, 3
  br i1 %0, label %switch.lookup, label %common.ret

switch.lookup:
  %1 = sext i32 %arg0.fca.0.extract to i64
  %switch.gep = getelementptr inbounds [3 x ptr], ptr @switch.table.kind, i64 0, i64 %1
  %switch.load = load ptr, ptr %switch.gep, align 8
  br label %common.ret
```

型パターンのmatchは各armが `is` と同じタグ比較に脱糖され、O3では単純enumのmatchと同様にタグ範囲チェック1回とルックアップテーブル参照へ潰される。
armで束縛した変数（`int n` など）を使う場合は、対応するブロック内でのペイロード取り出しが加わる。
matchの脱糖機構はenumと共通であり、詳細は [enumとmatchのlowering](../../lowering/enums-and-match.md) を参照。

## 関連資料

- [ユニオン型の設計](../../types/union-types.md) — タグ割り当て・代入互換・`is`/`as`の型検査の全体設計
- [`as`キャストの設計](../../types/casts.md) — ユニオン出し入れを含むキャストパイプライン
- [enumとmatchのlowering](../../lowering/enums-and-match.md) — 同じタグ付きunionレイアウトを使うmatch脱糖の機構
- [enumのIR対訳](enum.md) — 名前付きタグを持つenumとの表現比較
- [Result/OptionのIR対訳](result-option.md) — 組み込みタグ付きunionの対訳
