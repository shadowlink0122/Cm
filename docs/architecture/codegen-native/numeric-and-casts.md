# 数値出力とキャストの一貫性

数値の文字列化と `as` キャストは「同じCmプログラムはどのバックエンドでも同じ出力・同じ値になる」ことを設計目標とし、doubleの既定出力は最短round-trip表現、float→intは飽和、整数の縮小はビット切り捨て（ラップ）という仕様に統一されている。ネイティブバックエンドの書式化実体は `runtime_format.c` にあり、宣言契約と可搬アルゴリズムは `src/internal/codegen/common/` の共通ヘッダ群（`format_core.h` 等）へ集約されている。

## 概要

書式化の呼び出し面はLLVMコアが生成する `cm_format_*`/`cm_*_to_string` 宣言（`src/internal/codegen/llvm/core/runtime/builtins.cpp:13` で宣言のみ生成）で、実体はネイティブでは `src/internal/codegen/llvm/native/runtime_format.c`、Wasmでは `src/internal/codegen/llvm/wasm/runtime_format.c` がリンク時に解決する。`as` キャストのLLVM IR生成は `src/internal/codegen/llvm/core/rvalue.cpp:68-179` のCast右辺値処理が唯一の生成箇所である。

## データ構造とアルゴリズム

### 整数の書式化（runtime_format.c）

整数はlibcを使わない自前の桁列生成で文字列化する（`cm_format_int` `runtime_format.c:1994`、`cm_format_uint`:2001、`cm_format_long`:2008 等）。10進のほか、フォーマット指定子 `:x`/`:X`/`:b`/`:o` に対応する `cm_format_int_hex`/`_HEX`/`_binary`/`_octal`（`runtime_format.c:2067` 以降）があり、指定子のパースは共通の `CmFormatSpec` 構造体（`src/internal/codegen/common/format_spec.h:34-41`）と `cm_parse_format_spec`（`format_spec.h:56`）で行う。幅・整列・0詰めの適用も共通実装 `cm_apply_alignment`（`src/internal/codegen/common/format_core.h:179-221`）にアルゴリズムがある。

### 浮動小数の書式化: 最短round-trip表現

doubleの既定出力（precision未指定）は `cm_dtoa_shortest`（`runtime_format.c:581`）が担う。アルゴリズムは「桁数を1から増やしながら `%.*e` で出力し、`strtod` で元の値へ戻る最小の桁数を選ぶ」round-trip検証方式で、整形規則はJavaScriptのNumber→String規則（小数点位置nが(-6, 21]なら10進表記、それ以外は指数表記）に整合させている（`runtime_format.c:578-580` のコメントと `:653` 以降の整形）。

```c
// runtime_format.c:610-619 — float(32bit)から拡張された値はfloat精度でのround-tripを採用
if ((double)(float)value == value) {
    for (int p = 1; p <= 9; ++p) {
        snprintf(tmp, sizeof(tmp), "%.*e", p - 1, value);
        if ((double)strtof(tmp, NULL) == value) {
            digits_count = p;
            found = 1;
            break;
        }
    }
}
```

このfloat精度優先の分岐は、printlnがfloatをdoubleへ拡張して渡すために `3.14f` が `3.140000104904175` と冗長表示になるのを防ぐ。整数値のdouble（±1e15未満）は整数として出力する（`cm_format_double` `runtime_format.c:2022-2032`）。明示精度指定（`{x:.2}` 等）は `cm_format_double_precision`（`runtime_format.c:2034`）→ `cm_dtoa_buf`（`runtime_format.c:704-856`）で、precision>0のときは0.5×10^-Nの丸めを加えて小数点以下N桁固定・末尾0保持で出力する。NaN/±infは `nan`/`inf`/`-inf` の固定トークンである（`runtime_format.c:586-599`）。

この最短round-trip方式が防ぐバグは「バックエンドごとの桁数分裂」である。有効桁固定の実装は `123456789.5` を `123457000` のように桁化けさせ、js/ts（ECMAScriptのround-trip出力）と食い違う（経緯は[../../archive/v0.17.0/numeric/numeric-output-and-cast-consistency.md](../../archive/v0.17.0/numeric/numeric-output-and-cast-consistency.md)のM8）。

### `as` キャストの意味論（rvalue.cpp）

Cast右辺値の変換（`src/internal/codegen/llvm/core/rvalue.cpp:68`）は、ソース/ターゲットのLLVM型とHIR型（signedness判定用）から次の規則で命令を選ぶ。

| 変換 | 生成 | 意味論 |
|---|---|---|
| float→double / double→float | `fpext` / `fptrunc`（`rvalue.cpp:111-116`） | IEEE拡張/丸め |
| 整数→浮動小数 | 符号なしソースは `uitofp`、符号ありは `sitofp`（`rvalue.cpp:118-133`） | `uint 4000000000 as double` が負値化しない |
| 浮動小数→整数 | `llvm.fptosi.sat` / `llvm.fptoui.sat`（`rvalue.cpp:134-148`） | 範囲外は型の最大/最小へ飽和、NaNは0 |
| 整数の拡大 | ソースが符号なし（bool/char含む）なら `zext`、符号ありなら `sext`（`rvalue.cpp:151-175`） | `utiny 255 as int` は255（C言語と同じ規則） |
| 整数の縮小 | `trunc`（`rvalue.cpp:176-177`） | 下位ビット切り捨て（ラップ） |
| 整数→ポインタ | `inttoptr`（`rvalue.cpp:458`） | アドレスとして再解釈 |
| ポインタ→整数 | `ptrtoint`（`rvalue.cpp:499`） | アドレス値の取得 |

浮動小数→整数を生の `fptosi` にしない理由はコメントに明記されている。

```cpp
// rvalue.cpp:135-136
// M9: 生のfptosiは範囲外がpoison（ターゲット依存でINT_MIN/トラップに分裂）のため、
// 飽和intrinsicへ統一する（範囲外は型の最大/最小へclamp、NaNは0。全ターゲット共通）
```

生の `fptosi` はLLVM IR仕様上、範囲外がpoisonでありターゲット命令の挙動に依存する（x86_64は `cvttsd2si` でINT_MIN、wasmは `i32.trunc_f64_s` でトラップ）。飽和intrinsicへの統一により、native/jit/wasmが同一の飽和セマンティクスになる。符号なしターゲット（bool/char含む）は `fptoui_sat` を選ぶ（`rvalue.cpp:137-146`）。

整数拡大の符号判定は「ターゲット型」ではなく「ソース型」のsignednessで決める点が要である（`rvalue.cpp:152-171`）。ソース型が取得できない場合に限りターゲット型で判定するフォールバックを持つ。

### 共通実装への集約（src/internal/codegen/common/）

書式化はバックエンドごとにランタイム実体（native/wasmのC・js/tsのJS）が分かれるため、放置すると仕様が再分裂する。これを防ぐ集約点が `src/internal/codegen/common/` である。

- `runtime_common.h` — LLVM/WASM共有のランタイム関数宣言契約（`cm_format_*`/`cm_*_to_string` の唯一のシグネチャ定義）
- `format_spec.h` — `{:x}`/`{:.2}`/`{:>10}` 等のフォーマット指定子パーサ（`CmFormatSpec` と `cm_parse_format_spec`）
- `format_core.h` — 整数桁列生成・16進/2進/8進・整列/パディング・placeholder置換の可搬インライン実装（`cm_int_to_buffer` `format_core.h:17-55`、`cm_apply_alignment` `:179-221`、`cm_format_replace_impl` `:276-308`）
- `format/format_api.h` / `format/format_impl.h` — 各バックエンドが実装すべき書式化APIの宣言と共通インライン実装

共通ヘッダにアルゴリズムと契約を置き、各バックエンドのruntime_format.cはメモリ確保（`cm_alloc`）や出力（`cm_write_stdout`）などプラットフォーム依存部だけを差し替える構成が目標形であり、出力仕様（最短round-trip・飽和・NaN/infトークン）は全バックエンド共通のテストで固定されている。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/core/rvalue.cpp` | `as` キャストのLLVM IR生成（唯一の生成箇所） |
| `src/internal/codegen/llvm/core/runtime/builtins.cpp` | `cm_format_*`/`cm_*_to_string` のIR側宣言生成 |
| `src/internal/codegen/llvm/native/runtime_format.c` | ネイティブの書式化実体（`cm_dtoa_shortest`:581、`cm_dtoa_buf`:704、`cm_format_double`:2022） |
| `src/internal/codegen/llvm/wasm/runtime_format.c` | Wasmの書式化実体（libc非依存の同仕様実装） |
| `src/internal/codegen/common/runtime_common.h` | ランタイム関数の宣言契約 |
| `src/internal/codegen/common/format_spec.h` | フォーマット指定子パーサ（共通） |
| `src/internal/codegen/common/format_core.h` | 可搬な書式化アルゴリズム実装（共通） |
| `src/internal/codegen/common/format/format_api.h` / `format_impl.h` | 書式化APIの宣言と共通インライン実装 |

## 落とし穴とケア

- 浮動小数→整数は必ず飽和intrinsic経由にする不変条件を守ること（`rvalue.cpp:134-148`）。生の `fptosi`/`fptoui` に戻すと、範囲外挙動がターゲット依存（INT_MIN/トラップ）に分裂するバグのクラスが再発する。
- 整数拡大の `zext`/`sext` 判定はソース型のsignednessで行う（`rvalue.cpp:152-171`）。ターゲット型で判定すると `utiny 255 as int` が-1になる。同様に整数→浮動小数の `uitofp`/`sitofp` 判定もソース型で行う（`rvalue.cpp:118-133`）。
- doubleの既定出力を有効桁固定の実装に変えてはならない（`runtime_format.c:708-711` のコメント参照）。round-trip検証（`strtod`/`strtof` で元値へ戻ること）が精度保証の根拠である。
- float拡張値のfloat精度round-trip分岐（`runtime_format.c:610-619`）を外すと、float値のprintlnが冗長桁（`3.140000104904175`）になる。
- 書式化仕様を変更する際はnative/wasm/js/tsの全実体と、MIR定数畳み込みの同一セマンティクス（コンパイル時計算と実行時計算の一致）を同時に更新する必要がある。
- 回帰テストの場所: `tests/common/formatting/double_roundtrip.cm`（全バックエンドのdouble出力一致）、`tests/common/casting/float_int_saturation.cm`（範囲外float→intの飽和一致）、`tests/common/casting/as_cast.cm`・`cast_comprehensive.cm`（キャスト全般）。これらは `make test-llvm`/`test-llvm-wasm`/`test-js` 等のバックエンドスイートで同一期待値に対して検証される。

## 関連資料

- 数値出力・キャスト統一の設計と経緯: [../../archive/v0.17.0/numeric/numeric-output-and-cast-consistency.md](../../archive/v0.17.0/numeric/numeric-output-and-cast-consistency.md)
- [リンクとランタイム解決](linking-and-runtime.md)（runtime_format.cがリンクされる経路）
- [MIR→LLVM IR変換の構造](mir-to-llvm.md)（Cast右辺値を含む変換全体の位置づけ）
- 文字列・UTF-8まわりのランタイム設計: [../../archive/v0.17.0/strings/strings-utf8-and-stringbuilder.md](../../archive/v0.17.0/strings/strings-utf8-and-stringbuilder.md)
