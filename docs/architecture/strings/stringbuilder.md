# StringBuilder（native/jit）

`std::strings::StringBuilder`は容量倍増の可変バッファへ償却O(1)で追記するランタイム（`cm_sb_*`関数群）の薄いCmラッパーで、ループ内の`+`連結が引き起こすO(n²)の二次時間を線形時間に置き換える。Cm側はint64互換の`long`ハンドル1個を持つPOD構造体で、RAII（`~self()`）によりスコープ終了時にランタイムバッファが解放される。

## 概要

- ランタイム実体は`{char* data; size_t len; size_t cap;}`の容量倍増バッファで、native/jitともsrc/internal/codegen/llvm/native/runtime_format.c:2228-2316の同一実装を使う（jitはcmバイナリにリンク済みのランタイムシンボルを解決する。[representation.md](representation.md)参照）。
- API面はlibs/std/strings/builder.cmの`StringBuilder`構造体（`append`/`to_string`/`len`/`clear`/デストラクタ）で、`extern "C"`宣言経由でランタイムへ委譲する。
- ハンドルはCmの`long`（=常に64ビット）であり、ランタイム側のシグネチャはCの`long`ではなく`int64_t`で固定されている。wasm32ではCの`long`が32ビットになりCmの`long`と幅が食い違うため、全ターゲットで同一幅になる`int64_t`をABIとして選んでいる（native/wasmで同一シグネチャを保つ設計。src/internal/codegen/llvm/wasm/runtime_format.c:1387にも同型の実装がある）。
- 素朴な`+`連結は毎回「両辺の長さ取得+新規確保+全コピー」（runtime_format.c:2187-2203）なので、ループ内で累積するとO(n²)になる。StringBuilderはこれを償却O(1)追記へ置き換える手段であり、単一式内の`a + b + c (+ d)`はMIR loweringが`cm_string_concat3/4`へ自動集約するため書き換え不要である（src/internal/mir/lowering/expr/binary.cpp:43-108）。

## データ構造とアルゴリズム

### ランタイムバッファ

バッファ本体とその管理構造体はランタイム内部に閉じ、Cm側にはポインタ値をint64へ詰めたハンドルだけが渡る（runtime_format.c:2228-2246）。

```c
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} CmStringBuilder;

int64_t cm_sb_create(void) {
    CmStringBuilder* sb = (CmStringBuilder*)cm_alloc(sizeof(CmStringBuilder));
    ...
    sb->cap = 16;
    sb->len = 0;
    sb->data = (char*)cm_alloc(sb->cap);
    ...
    return (int64_t)(intptr_t)sb;
}
```

### 償却O(1)のappend

`cm_sb_append`は不足時のみ容量を2倍ずつ拡張し、通常経路は`memcpy`1回で済む（runtime_format.c:2249-2274）。拡張回数は対数オーダーなので、n回のappendの総コピー量はO(n)（償却O(1)/回）になる。

```c
void cm_sb_append(int64_t handle, const char* s) {
    ...
    size_t add = strlen(s);
    ...
    if (sb->len + add > sb->cap) {
        size_t new_cap = sb->cap;
        while (sb->len + add > new_cap) {
            new_cap *= 2;
        }
        char* new_data = (char*)cm_alloc(new_cap);
        ...
        memcpy(new_data, sb->data, sb->len);
        cm_dealloc(sb->data);
        sb->data = new_data;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, add);
    sb->len += add;
}
```

### to_string / len / clear / destroy

- `cm_sb_to_string`は現在の内容を`cm_str_alloc`（長さヘッダ付き）の新規バッファへコピーして返す。戻り値は呼び出し側所有で、builderは継続使用できる（runtime_format.c:2277-2293）。ヘッダ付きで返るため、生成された文字列の`byte_len()`はO(1)である。
- `cm_sb_len`は内部カウンタを返すだけのO(1)である（runtime_format.c:2295-2298）。
- `cm_sb_clear`は`len = 0`にするだけで容量は維持し、再利用時の再確保を避ける（runtime_format.c:2301-2306）。
- `cm_sb_destroy`はデータバッファと管理構造体を解放する（runtime_format.c:2308-2316）。

### Cm側API

Cm側は`long`ハンドル1個のPOD構造体で、コンストラクタ/デストラクタがランタイムの生成/解放へ1対1対応する（libs/std/strings/builder.cm:18-52）。

```cm
export struct StringBuilder {
    long handle;
}

export impl StringBuilder {
    self() { self.handle = cm_sb_create(); }
    void append(string s) { cm_sb_append(self.handle, s); }
    string to_string() { return cm_sb_to_string(self.handle); }
    long len() { return cm_sb_len(self.handle); }
    void clear() { cm_sb_clear(self.handle); }
    ~self() { cm_sb_destroy(self.handle); }
}
```

`~self()`による解放で、関数スコープを抜ければランタイムバッファはリークしない。`to_string()`の戻り値は新規確保バッファをユーザー変数が所有する形になり、既存の文字列drop規則（再代入時解放・一時解放）にそのまま乗る（fresh所有バッファの判定はsrc/internal/mir/passes/cleanup/string_reassign_free.cpp:23-27の`*_to_string`ホワイトリストに含まれる）。

### `+`連結との使い分け

| パターン | 推奨 | 理由 |
|---|---|---|
| 単一式の`a + b`〜`a + b + c + d` | `+`のまま | MIR loweringがconcat/concat3/concat4へ集約し確保1回で済む（binary.cpp:43-108） |
| ループ内での累積（`s = s + x`） | StringBuilder | `+`は毎回全コピーでO(n²)、appendは償却O(1)でO(n) |
| 行の再構成・大量パーツのjoin相当 | StringBuilder + 最後に`to_string()` | 中間文字列の確保・解放を出さない |

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/codegen/llvm/native/runtime_format.c | CmStringBuilderと`cm_sb_create/append/to_string/len/clear/destroy`（:2228-2316）。native AOTとjitの共用実体 |
| libs/std/strings/builder.cm | `StringBuilder`構造体とextern "C"宣言（:7-12, :18-52） |
| libs/std/strings/mod.cm | `std::strings`からのre-export |
| src/internal/mir/lowering/expr/binary.cpp | 単一式の連結チェーン集約（StringBuilderを不要にする側の最適化、:43-108） |
| src/internal/codegen/llvm/wasm/runtime_format.c | 同一シグネチャのwasm実装（:1382-1470。int64_tハンドルABIをnativeと共有） |
| src/internal/codegen/llvm/core/runtime/builtins.cpp | ランタイム関数のLLVMシグネチャ登録 |

## 落とし穴とケア

- 防ぐバグのクラス: ループ連結の二次時間（`+`累積は文字列長に比例するコピーを毎回行う）、および長大文字列生成時の中間バッファ大量確保・解放によるアロケータ負荷。
- ハンドル幅の不変条件: `cm_sb_*`のハンドルは`int64_t`固定を維持すること。Cの`long`に変えるとwasm32（C longが32ビット）でCmの`long`（64ビット）と幅が食い違い、ハンドル値の上位が欠落する。native/jitだけを見ると`long`でも動いてしまうため、シグネチャ変更時はこの理由を確認すること。
- `cm_sb_append`は追記長を`strlen(s)`で測る（runtime_format.c:2254）ため、埋め込みNULを含む文字列は最初のNULまでしか追記されない。埋め込みNULを保持したままバイト列を組み立てる用途には`std::strings::from_bytes`側の経路を使う（[representation.md](representation.md)参照）。
- `to_string()`は呼び出しごとに全内容をコピーした新規バッファを返す。ループ内で毎回`to_string()`を呼ぶとStringBuilderを使う意味が消えるので、取り出しは最後に1回にする。
- ハンドルはただの整数値なのでコピーできてしまうが、所有者は1つとして扱うこと。構造体を値コピーして両方のデストラクタが走ると二重解放になる（ハンドル方式共通の制約）。
- `clear()`は容量を維持する仕様であり、巨大な内容を一度でも保持したbuilderはメモリを持ち続ける。長寿命オブジェクトで使い回す場合はこの点を考慮する。
- 回帰テストの場所: tests/common/strings/string_builder_test.cm（内容・長さ・clear動作）、tests/common/strings/string_builder_perf_test.cm（大量appendが二次時間で破綻しないこと）、tests/common/strings/concat_chain_test.cm（`+`チェーン集約側の検証）。

## 関連資料

- 設計経緯: [文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入](../../archive/v0.17.0/strings-utf8-and-stringbuilder.md)
- ランタイム表現（cm_str_alloc・所有と解放）: [representation.md](representation.md)
- UTF-8コードポイントAPI: [utf8.md](utf8.md)
