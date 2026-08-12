# 文字列リテラルのLLVM IR対訳

Cmの文字列リテラルは、16バイトのSDSヘッダ（`{magic, byte_len, magic2, reserved}`）とNUL終端データをパックしたグローバル定数`@strh`として発行され、式の値としてはデータ先頭（グローバル+16バイト）を指すGEPが使われる。
このため`string`はLLVMレベルでは素の`ptr`（`char*`互換）でありながら、ランタイムはポインタの16バイト手前を読むだけでO(1)でバイト長を取得できる。
エスケープシーケンスはレクサで解決済みのバイト列として定数に埋め込まれ、O3でも文字列定数自体は（使用されている限り）そのままIRに残る。

### 基本の文字列リテラル

```cm
int main() {
    string s = "hello";
    println(s);
    return 0;
}
```

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [6 x i8] }> <{ i32 1129141041, i32 5, i32 1395741251, i32 0, [6 x i8] c"hello\00" }>, align 16

tail call void @cm_println_string(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0))
```

パック構造体の4つの`i32`はSDSヘッダ（マジック・バイト長5・第2マジック・予約）で、続く`[6 x i8]`が`"hello\00"`のNUL終端データである。
式の値は第4フィールド（インデックス4）先頭へのGEP、すなわちグローバル+16バイトのデータポインタであり、ヘッダ付きであることはランタイムがマジック検査で判別する。
`align 16`はランタイムの読み取りゲート（未整列ポインタは即ヘッダ無し判定）と整合させるための必須条件である。
ヘッダレイアウトと判定アルゴリズムは[../../strings/representation.md](../../strings/representation.md)を参照。

### エスケープシーケンス

```cm
string esc = "line1\nline2\ttab\\end";
println(esc);
```

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [20 x i8] }> <{ i32 1129141041, i32 19, i32 1395741251, i32 0, [20 x i8] c"line1\0Aline2\09tab\\end\00" }>, align 16
```

`\n`・`\t`・`\r`・`\\`・`\"`・`\'`・`\0`はレクサの`scan_escape_char`で実バイトへ変換され、IRの定数では`\0A`（LF）・`\09`（TAB）のような16進表記で現れる。
ヘッダのバイト長（19）はエスケープ解決後の長さであり、`\0`で埋め込みNULを含めても長さがヘッダ由来のため連結や`byte_len()`が途中で切れない。
`{`・`}`は文字列補間の区切りとして解釈されるため、リテラル波括弧は`\{`・`\}`でエスケープする。

### 空文字列

```cm
string empty = "";
println(empty);
```

```llvm
@strh.1 = private unnamed_addr constant <{ i32, i32, i32, i32, [1 x i8] }> <{ i32 1129141041, i32 0, i32 1395741251, i32 0, [1 x i8] zeroinitializer }>, align 16
```

空文字列もヘッダ＋NUL終端1バイトのグローバル定数として発行され、バイト長フィールドは0、データ部は`zeroinitializer`になる。
nullポインタとは異なる有効なデータポインタを持つため、空文字列に対する長さ取得や連結も通常の文字列と同じ経路で動作する。

### マルチバイト（UTF-8）文字列

```cm
string jp = "こんにちは";
println(jp);
```

```llvm
@strh.2 = private unnamed_addr constant <{ i32, i32, i32, i32, [16 x i8] }> <{ i32 1129141041, i32 15, i32 1395741251, i32 0, [16 x i8] c"\E3\81\93\E3\82\93\E3\81\AB\E3\81\A1\E3\81\AF\00" }>, align 16
```

ソースコード上のUTF-8バイト列がそのまま定数データになり、ヘッダのバイト長はコードポイント数（5）ではなくバイト数（15）を記録する。
コードポイント単位の長さ・添字アクセスは`len()`や`substring()`などのランタイム関数が担い、リテラル発行時点ではバイト列以上の構造を持たない。
UTF-8走査とコードポイント添字の設計は[../../strings/utf8.md](../../strings/utf8.md)を参照。

### 補間を含む文字列リテラル

```cm
int main(int argc, string[] argv) {
    string msg = "count={argc}";
    println(msg);
    return 0;
}
```

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [9 x i8] }> <{ i32 1129141041, i32 8, i32 1395741251, i32 0, [9 x i8] c"count={}\00" }>, align 16

%2 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (...) )
%3 = tail call ptr @cm_format_replace_int(ptr %2, i32 %arg0)
tail call void @cm_println_string(ptr %3)
```

`{式}`を含むリテラルは純粋な定数ではなくフォーマットテンプレート`"count={}"`へ分解され、実行時に`cm_format_replace_int`等で値を埋め込んだ新規文字列が生成される。
テンプレート部分だけが`@strh`グローバル定数として残り、補間結果はヒープ上のSDS文字列になる点が非補間リテラルとの本質的な違いである。
補間のlowering全体は[../../codegen-native/print-and-interpolation.md](../../codegen-native/print-and-interpolation.md)を参照。

### O3での扱い

未使用の文字列リテラルは変数ごとDCEで削除され、対応する`@strh`グローバルもIRから消える。
使用される文字列はO0・O3を問わず同じ「グローバル定数＋16バイトオフセットGEP」の形であり、O3ではGEPが定数式に畳み込まれて`nonnull`属性が付く程度の差しかない。
同一内容のリテラルが複数回現れた場合は1つのグローバル定数（例: `@strh.3`）へ共有されるが、`==`比較や`+`連結は定数どうしでもランタイム関数（`cm_strcmp`・`cm_string_concat`）の呼び出しとして残り、コンパイル時には畳み込まれない。

## 関連資料

- [../../strings/representation.md](../../strings/representation.md) — SDSヘッダの構造・判定ゲート・連結や解放の詳細
- [../../strings/utf8.md](../../strings/utf8.md) — コードポイント添字とUTF-8走査
- [../../codegen-native/print-and-interpolation.md](../../codegen-native/print-and-interpolation.md) — println系と文字列補間のlowering
- [numeric-literals.md](numeric-literals.md) — 数値・bool・charリテラルの対訳
- [aggregate-literals.md](aggregate-literals.md) — 配列・スライス・構造体リテラルの対訳
