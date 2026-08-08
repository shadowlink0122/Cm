# UTF-8コードポイント処理（native/jit）

Cmの文字列はUTF-8バイト列であり、ユーザー向けの長さ・添字APIはコードポイント単位、バイト単位のアクセスは`byte_len()`/`byte_at()`という別名のAPIに分離されている（R2: `charAt`/`at`もコードポイント添字。ASCIIのみ`char`で値を返し非ASCIIは`'\0'`）。`len()`は継続バイト（0b10xxxxxx）を数えないO(n)スキャンでコードポイント数を返し、`substring`/`slice`/`indexOf`/`codepoint_at`/`chars()`は全てコードポイント添字で統一されているため、マルチバイト文字の途中で切れた壊れたUTF-8が生成されることはない。

## 概要

- `s.len()`（別名`size`/`length`）はHIR loweringで`__builtin_string_codepoint_len`へ写像され、コードポイント数を返す（src/internal/hir/lowering/expr_member.cpp:777-783）。
- `s.byte_len()`は`__builtin_string_len`へ写像され、バイト数を返す（expr_member.cpp:785-792）。ヘッダ付き文字列ならO(1)である（[representation.md](representation.md)参照）。
- コードポイント添字API: `substring(start, end)`/`slice`、`indexOf(sub)`の戻り値、`codepoint_at(i)`、`chars()`。
- バイト添字API: `byte_at(i)`（生バイト0..255を`int`で返す。R2で追加）。`byte_len()`と対になるバイトアクセス。`charAt(i)`/`at(i)`はコードポイント添字でASCIIのみ`char`値を返す（R2でバイト添字から変更。従来はlen()との単位分裂で非ASCII走査が全滅していた）。
- UTF-8デコードは先頭バイトのビットパターン判定（1〜4バイト列）のみで行い、不正なバイト列でも停止しない防御的な実装になっている。

## データ構造とアルゴリズム

### コードポイント数のカウント

`__builtin_string_codepoint_len`は継続バイトを数えない1パス走査で、ヘッダ由来のバイト長を境界として使うため埋め込みNULも1コードポイントとして数える（src/internal/codegen/llvm/native/runtime_format.c:1003-1017）。

```c
size_t __builtin_string_codepoint_len(const char* str) {
    ...
    const size_t blen = cm_string_byte_len(str);
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)str;
    for (size_t i = 0; i < blen; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}
```

ASCIIのみの文字列では`len()`と`byte_len()`は一致するため、ASCII前提のコードは両者の分離の影響を受けない。

### コードポイント添字→バイトオフセット変換

コードポイント添字APIの共通基盤は`cm_cp_index_to_byte`で、指定個数のコードポイント開始をスキップしてバイトオフセットを返す（runtime_format.c:1043-1063）。末尾を越える添字は文字列末尾のバイトオフセットに丸められ、継続バイトの途中で止まらないよう次のコードポイント開始まで進める後処理を持つ。

```c
static size_t cm_cp_index_to_byte(const char* str, int64_t cp_index) {
    const size_t blen = cm_string_byte_len(str);
    ...
    while (i < blen) {
        if ((p[i] & 0xC0) != 0x80) {
            if (seen == cp_index) {
                break;
            }
            seen++;
        }
        i++;
    }
    while (i < blen && (p[i] & 0xC0) == 0x80) {
        i++;
    }
    return i;
}
```

### substring / slice

`substring`と`slice`は同一のビルトイン`__builtin_string_substring`へ写像され（expr_member.cpp:823-833）、添字はコードポイント単位、負添字はPython風（末尾からの位置）に解決される（runtime_format.c:1068-1094）。範囲確定後に`cm_cp_index_to_byte`で両端のバイトオフセットを求め、その区間をヘッダ付き新規バッファへ`memcpy`するため、切り出し結果がコードポイント境界を跨ぐことはない。

### codepoint_at

`codepoint_at(i)`はコードポイント添字iのUnicodeスカラ値を`uint`で返す（expr_member.cpp:801-811）。実体はUTF-8の1〜4バイト列を先頭バイトのパターン（`0xxxxxxx`/`110xxxxx`/`1110xxxx`/`11110xxx`）と継続バイト検証付きでデコードし、範囲外・不正列は0を返す（runtime_format.c:1097-1121）。

### indexOf

`indexOf`は`strstr`で検出したバイト位置を、先頭からの継続バイトを除いたカウントでコードポイント添字へ変換して返す（runtime_format.c:1124-1137）。未検出は-1のままである。戻り値をそのまま`substring`へ渡せる（添字単位が一致している）ことがこの設計の要点で、libs/std/strings/split.cm:19-25の`split`実装はまさに`indexOf`の戻り値と`sep.len()`（コードポイント数）を`substring`の添字として合成している。

### chars()

`chars()`はコードポイント列を`uint[]`スライスとして実体化して返し、for-inで列挙できる（expr_member.cpp:793-800）。実体は2パス（コードポイント数カウント→`cm_slice_new`で確保→デコードして充填）で、遅延イテレータではなく実体化スライスである（src/internal/codegen/llvm/native/runtime_slice.c:55-98）。現行のイテレータ基盤で全バックエンドの観測一致を優先した設計判断であり、経緯はarchive文書に記録されている。

### バイトAPIとの分離

`charAt`/`at`はコードポイント添字でASCIIのみ`char`値を返す（R2。非ASCIIコードポイントは`'\0'`）。バイナリ的なアクセスは`byte_at`（`byte_len`と対）を使う。`first`/`last`は先頭/末尾バイトを返すバイト系のままであり、非ASCII先頭文字にはコードポイント系（`codepoint_at(0)`等）を使う。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/hir/lowering/expr_member.cpp | メソッド名→ビルトイン写像: len/size/length（:777-783）、byte_len（:785-792）、chars（:793-800）、codepoint_at（:801-811）、charAt/at（:812-822）、substring/slice（:823-833）、indexOf（:834-844） |
| src/internal/codegen/llvm/native/runtime_format.c | codepoint_len（:1003-1017）、cm_cp_index_to_byte（:1043-1063）、substring（:1068-1094）、codepoint_at（:1097-1121）、indexOf（:1124-1137）、charAt/first/last（:1019-1039） |
| src/internal/codegen/llvm/native/runtime_slice.c | chars()の実体化スライス生成（:55-98） |
| src/internal/codegen/llvm/core/runtime/builtins.cpp | 各ビルトインのLLVMシグネチャ登録（:145-165） |
| libs/std/strings/split.cm | コードポイント添字APIの合成利用例（split/lines） |

## 落とし穴とケア

- 防ぐバグのクラス: バイト添字での切り出しによる壊れたUTF-8の生成（マルチバイト文字の途中で切れる）、バックエンド間の添字単位不一致（バイト単位とUTF-16単位で同じプログラムの観測が食い違う）、`indexOf`の戻り値を`substring`へ渡すと位置がずれる単位混在バグ。
- 維持すべき不変条件: 「添字・長さを返す/受けるユーザーAPIはコードポイント単位、バイト単位は`byte_len`/`byte_at`のbyte_接頭辞名のみ」という二層構造を崩さない。新しい文字列APIを追加するときは、どちらの層に属するかを決めてから`cm_cp_index_to_byte`または`cm_string_byte_len`を使い分けること（1つのAPI内で両単位を混ぜない）。
- 走査境界は必ず`cm_string_byte_len`（ヘッダ由来）を使う。NUL終端で走査を打ち切ると埋め込みNULを含む文字列で添字解決が壊れる（codepoint_len・cm_cp_index_to_byteは既にこの規約に従っている）。
- UTF-8デコードは不正列で停止せず0や近傍境界へ丸める防御的動作とする。ランタイムはユーザー入力由来の任意バイト列を受けるため、assertやクラッシュで落ちてはならない。
- `split(s, "")`のような全文字分割は`substring(i, i+1)`のO(n)呼び出しを繰り返すためO(n²)になり得る。コードポイント列が必要なだけなら`chars()`の方が線形である。
- 回帰テストの場所: tests/common/strings/utf8_len_test.cm（len/byte_len分離）、tests/common/strings/utf8_index_test.cm（substring/codepoint_at/indexOfのコードポイント添字）、tests/common/strings/utf8_chars_test.cm（chars()列挙）、tests/common/strings/split_lines_test.cm（コードポイント添字の合成利用）。

## 関連資料

- 設計経緯: [文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入](../../archive/v0.17.0/strings/strings-utf8-and-stringbuilder.md)
- ランタイム表現とO(1)バイト長: [representation.md](representation.md)
- StringBuilder: [stringbuilder.md](stringbuilder.md)
