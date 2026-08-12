---
title: 文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入
parent: v0.17.0 Design
---

# 文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H9 | 言語 | 文字列がNUL終端`char*`のため埋め込みNULでデータ喪失・lenはバイト数のみ（UTF-8非対応）・StringBuilderが無くループ連結はO(n²)（実測で二次時間、N=200kで4.6秒） | 第1段実装済み（`std::strings::StringBuilder`を追加。native/wasmは容量倍増バッファのランタイム（cm_sb_*、ハンドルはint64_t——wasm32のC longは32ビットでCmのlongと不一致になるため）、jsは{parts:[], n}への写像。jit/native/wasm/js/tsの5系で出力一致、N=50kのappendで素朴連結1.4秒CPU→ほぼ0秒を実測。第2段（byte_len分離）・第3段のlen()コードポイント化も実装済み（native/wasm=継続バイトスキップ、js=[...s].length、byte_lenのjsはTextEncoderでUTF-8バイト数）。部分文字列・codepoint_at・chars()・indexOf()のコードポイント単位化も実装済み（charAt/atはバイトAPIとして維持）。第5段（連結最適化）も実装済み: 連結チェーンをcm_string_concat3/4へ集約。第4段も実装済み（設計判断の更新: (ptr,len)の全面ABI変更でなく、char*完全互換のSDS方式=データ直前の16バイトヘッダ{magic, byte_len, magic2, 確保オフセット}で目的を達成した。文字列リテラルはコード生成が{ヘッダ+データ}のグローバル定数として発行しデータ先頭+16を参照、ランタイム生成文字列は全プロデューサがcm_str_alloc経由でヘッダ付き確保。byte_len()はO(1)、埋め込みNULはfrom_bytes（std::strings、バイト列からの構築API）で作成でき、concat/substring/len/codepoint_atが長さ境界で正しく扱う。FFI/libcへはNUL終端char*のままゼロコストで渡せる。安全性: 未知ポインタの前方読みは16バイト整列+ページ先頭ゲートで保護し、確保時はデータがページ先頭16バイト未満に落ちる場合16バイトシフト（オフセットをヘッダに記録）、解放時はマジック消去で再利用ブロックの誤認を防止。二重マジック+終端検証で誤判定は実質ゼロ。native/wasm実装+js写像（TextDecoder）、MallocScribble下の反復とO0/O3・全12スイート2周で安定を確認） |

これはランタイム表現・型マッピング・全バックエンドのコード生成に及ぶ大規模な設計変更であり、段階分割を厚く扱う。

## 背景と根本原因

### 文字列は「NUL終端char*単一ポインタ」

文字列型は全バックエンドで長さを持たない素のポインタへ落ちる。

- 型マッピング: src/internal/codegen/llvm/core/types.cpp:58-60 で`hir::TypeKind::String`/`CString`がいずれも`ctx.getPtrType()`（opaqueポインタ、長さフィールドを伴う構造体ではない）。
- リテラル生成（式）: types.cpp:726 `builder->CreateGlobalStringPtr(str, "str")`（NUL終端Cストリングのポインタ）。
- グローバル文字列定数: src/internal/codegen/llvm/core/translate/program.cpp:266（および:612）で`llvm::ConstantDataArray::getString(ctx, str, /*AddNull=*/true)`。第3引数`true`が末尾NUL付加。
- nullリテラル/デフォルト: types.cpp:687-691で`ConstantPointerNull`。
- rvalue経路も src/internal/codegen/llvm/core/rvalue.cpp:609 で`getPtrType()`。

(ptr, len)ペアや長さ付き構造体表現はコードベース上どこにも生成されない。このため**埋め込みNULは終端と区別できずデータ喪失**し、**長さは毎回O(n)走査**になる。

### lenはバイト数（UTF-8非対応）

長さ計算はstrlen相当のNUL終端バイトカウントで、コードポイント単位ではない。

- native: src/internal/codegen/llvm/native/runtime_format.c:762 `size_t __builtin_string_len(const char* str)` が`cm_strlen_impl`（同ファイル:28、`while(*p) p++`のバイト数）を呼ぶ。
- wasm: src/internal/codegen/llvm/wasm/runtime_format.c:39 `__builtin_string_len`が`wasm_strlen`（:30、バイト数）を呼ぶ。
- js: src/internal/codegen/js/builtins.cpp:321 で`.length`（JSはUTF-16単位）。native（バイト数）とjs（UTF-16単位）で意味が食い違う。
- std: libs/std/core/string.cm:38 `len`もlibc `strlen`依存。

コードポイント境界を扱うロジックは`src/internal/codegen`・`libs/std`全体に存在しない（`utf8|codepoint|rune|char32`のgrepでヒットゼロ、唯一のヒットはjs/codegen.cpp:146のHTML `<meta charset>`で無関係）。

### 連結がループ内O(n²)・StringBuilder不在

連結は毎回「両辺のstrlen + 新規alloc + 全コピー」を行う素朴なO(n)実装で、ループ内で使うとO(n²)になる。

- native実体: src/internal/codegen/llvm/native/runtime_format.c:1718 `char* cm_string_concat(const char* left, const char* right)`（:1724-1726で`strlen`×2 + `cm_alloc(len1+len2+1)` + `strcpy`+`strcat`）。
- 移植版: src/internal/codegen/common/format_core.h:254 `cm_string_concat_impl`（:260-267で`cm_strlen`×2 + `cm_alloc` + `cm_memcpy`×2）。
- wasm: src/internal/codegen/llvm/wasm/runtime_wasm.c:79 `cm_concat_strings`が`cm_string_concat`へ委譲。
- js: src/internal/codegen/js/runtime.cpp:261-266 `__cm_str_concat(a,b){ return String(a)+String(b); }`。
- 発行元: src/internal/codegen/llvm/core/operators.cpp:221, :260 で`cm_string_concat`をgetOrInsertして発行。

`StringBuilder|string_builder|str_builder|strbuf|string_buffer`のgrepは実装コードでヒットゼロ（監査文書のみ）。可変長バッファへ償却O(1)で追記するAPIは存在しない。

### バックエンド構成（変更対象）

バックエンドは4系統（js / llvm-native / llvm-wasm / sv）。TS専用ランタイムは無く、jsバックエンドがJS文字列へ直接lowerする。文字列ランタイム実体は以下。

- native: src/internal/codegen/llvm/native/（runtime_format.c等）
- wasm: src/internal/codegen/llvm/wasm/（runtime_format.cを共有include）
- 共通ヘッダ: src/internal/codegen/common/（format_core.h, runtime_common.h, runtime_functions.cpp）
- js: src/internal/codegen/js/（builtins.cpp, runtime.cpp）

## 設計方針

### 1. 文字列表現を(ポインタ, 長さ)へ

LLVM系（native/wasm）の文字列を「データポインタ + バイト長」を持つfat表現へ移行する。

- ランタイム内部表現: `{ char* data; size_t len; }`（長さを明示保持）。バイト長を持つことで埋め込みNULを保持でき、`len`はO(1)取得になる。
- 相互運用のためNUL終端は維持する（libc・既存`__builtin_string_*`との互換）。すなわち「長さ付き、かつ末尾NULも置く」二重保証とする。
- 型マッピング（types.cpp:58-60）を、長さフィールドを持つ表現（構造体 or 別途長さを運ぶ規約）へ変更。リテラル生成（types.cpp:726, program.cpp:266）は長さを同時に埋め込む。
- jsバックエンドはネイティブJS文字列（UTF-16、長さ内蔵）を継続利用しつつ、`len`のセマンティクスをコードポイント基準へ揃える（後述）。

破壊的変更を避けるため（Cm言語設計原則）、既存の`string`型の構文・利用方法は変えず、内部表現のみを差し替える。ユーザーコードからは`s.len()`・`s[i]`・`a + b`のインターフェイスが不変であることを保つ。

### 2. UTF-8コードポイント単位API

長さ・添字・イテレーションをコードポイント基準に揃え、バイト基準は別APIとして併存させる。

- `s.len()`: コードポイント数を返す（バックエンド間で一致）。
- `s.byte_len()`: バイト数を返す（従来のstrlen相当を明示名で温存）。
- コードポイント境界を意識した部分文字列・イテレーション（`chars()`等）。
- native/wasmにUTF-8デコード（先頭バイトから継続バイト数を判定）を実装し、jsは既存のJS文字列イテレータ（`for...of`はコードポイント単位）へ寄せる。

### 3. StringBuilder導入（償却O(1)追記）

可変長バッファへ追記する`StringBuilder`を新設し、ループ連結のO(n²)を解消する。

- 内部は容量2倍拡張の可変バッファ（Vector/スライスと同じ拡張規約、監査のTreeMap自動拡張スライスと整合）。
- API: `append(string)` / `append(char)` / `len()` / `to_string()`。
- native/wasmはランタイム関数（`cm_string_builder_new/append/build`等、新規）で実装。jsは配列push + join、または直接文字列連結の内部最適化。
- ループ内連結を`StringBuilder`へ書き換えれば、N=200kで4.6秒だった連結が線形時間になる。

## 構文例・出力例

設計目標（現時点では未実装）。

```cm
// 埋め込みNULを保持（データ喪失しない）
string s = make_with_embedded_nul();
println(s.byte_len());   // NUL以降も含む正しいバイト長

// UTF-8コードポイント単位
string t = "あいう";
println(t.len());        // 3（コードポイント数）
println(t.byte_len());   // 9（UTF-8バイト数）

// StringBuilder（償却O(1)追記、ループでO(n)）
StringBuilder sb();
for (int i = 0; i < 200000; i++) {
    sb.append("x");
}
string result = sb.to_string();
println(result.len());   // 200000
```

出力は全バックエンド（jit/native/wasm/js/ts相当）で一致する（`len`のコードポイント基準を含む）。

## 実装の段階分割

大規模変更のため小さく安全な段階へ割る。

1. 第1段（StringBuilder先行、表現非依存）: 現行のNUL終端`char*`表現のまま、`StringBuilder`を可変バッファランタイムで実装。ループ連結のO(n²)を先に解消する（最も実害が大きく、表現変更に依存しない）。native/wasm/jsそれぞれに実装し、出力一致テストを整備。

   第1段の詳細設計（実装済み）:
   - API（`libs/std/strings/builder.cm`、`module std.strings.builder`）: `export struct StringBuilder { long handle; }` + `export impl StringBuilder`。メソッドは `self()`（ランタイムバッファ作成）・`void append(string s)`（償却O(1)追記）・`string to_string()`（現在内容のコピーを返す。builderは継続使用可能）・`long len()`（現在のバイト長O(1)）・`void clear()`（内容を空に、容量は維持）・`~self()`（バッファ解放）。ハンドルはチャネル（native::sync::channel）と同じlongハンドル方式で、extern "C"宣言でランタイムへ委譲する
   - ランタイム関数（native/wasm共通シグネチャ）: `long cm_sb_create()` / `void cm_sb_append(long handle, const char* s)` / `char* cm_sb_to_string(long handle)` / `long cm_sb_len(long handle)` / `void cm_sb_clear(long handle)` / `void cm_sb_destroy(long handle)`。実装は{char* data; size_t len; size_t cap;}の容量倍増（初期16、不足時2倍）で、appendはmemcpyのみ。to_stringはlen+1をcm_allocしてNUL終端コピーを返す（呼び出し側所有。C12のdropパス対象外＝ユーザー変数が所有）
   - jsバックエンド: ハンドルをオブジェクト`{parts: []}`へ写像するビルトインを追加（`cm_sb_create`→`{parts:[]}`相当、`cm_sb_append`→`parts.push(String(s))`、`cm_sb_to_string`→`parts.join("")`、`cm_sb_len`→追記時に加算する長さカウンタ、`cm_sb_destroy`→no-op）。JSはGC管理のためdestroyは何もしない
   - SVバックエンド: 対象外（動的文字列バッファは合成不能）。テストは`//! platform: !sv`で除外する
   - 所有権と安全性: StringBuilder構造体はlongハンドル1個のPODで、既存のRAII（~self()）により関数スコープ終了時にランタイムバッファが解放される。to_string()の戻り値は新規確保バッファで呼び出し側変数が所有する（既存のC12再代入解放・一時解放の対象規則にそのまま乗る）
2. 第2段（byte_len明示化）: 従来のstrlenベース長さを`byte_len()`として公開し、`len()`のコードポイント化の受け皿を作る。（実装済み: 型検査`infer_string_method`とHIR loweringへ`byte_len`を追加し`__builtin_string_len`へ写像。jsのbyte_lenは`TextEncoder.encode(s).length`でUTF-8バイト数を返す——JS Stringの`.length`はUTF-16単位のためnativeと食い違っていた）
3. 第3段（UTF-8デコード）: native/wasmにUTF-8境界判定を実装し、`len()`をコードポイント数へ切替。jsをコードポイント基準へ揃える。添字・部分文字列・`chars()`をコードポイント単位化。（len()の切替は実装済み: `__builtin_string_codepoint_len`（継続バイト0b10xxxxxxを数えないO(n)スキャン）をnative/wasmへ追加し、jsは`[...s].length`（サロゲートペアを1と数える）。ASCIIのみの文字列は挙動不変。部分文字列とコードポイント取得も実装済み: substring/sliceの添字をコードポイント単位へ統一（従来はnative=バイト・js=UTF-16単位で既に不一致だった。負添字のPython風意味論は維持、SVはASCII前提で従来どおり）、codepoint_at(i)を新設（コードポイント添字iのスカラ値をuintで返す。範囲外は0）。charAt/atはバイト単位のまま維持（byte_lenと対のバイトアクセスAPIとして明記）。chars()とindexOfも実装済み: chars()はコードポイント列をuint[]スライスで返しfor-inで列挙できる（native/wasm=cm_slice_new+UTF-8デコード、js=スプレッド+codePointAt写像。遅延イテレータでなく実体化スライスの設計——現行のイテレータ基盤で全バックエンド一致を優先）。indexOf()の戻り値はバイトオフセットからコードポイント添字へ統一（js=UTF-16単位との不一致も解消、未検出は-1のまま）。第4段以降へ残るのは(ptr,len)表現・埋め込みNUL・連結最適化のみ）
4. 第4段: 実装済み（設計判断を(ptr,len)移行からchar*互換のSDSヘッダ方式へ更新。全バックエンドのABI・FFI境界を変えずに、O(1)バイト長・埋め込みNUL保持・from_bytes構築を実現。詳細は状態表を参照）。
5. 第5段（連結最適化）: 実装済み。MIR loweringのlower_binaryで文字列Addチェーンを平坦化し、cm_string_concat3/4（native/wasm、jsは+連結写像）へ集約する。StringBuilder経由でなくN引数連結関数方式を採用（確保1回で同等の効果、ランタイム追加が最小）。

各段は独立にテスト可能で、第1段だけでも実害（二次時間）を解消できる。

## テスト計画（tests/common/配下）

既存の文字列/フォーマット系スイート（`tests/common/formatting/`等の`.cm`+`.expect`形式）へ追加する。

- `tests/common/strings/string_builder_test.cm` + `.expect`: `StringBuilder`で大量append後の内容・長さを検証。全バックエンド一致。ループ連結との結果一致も確認。
- `tests/common/strings/string_builder_perf_test.cm` + `.expect`: 大きなN（例: 数万）でのappendが完了する（二次時間で破綻しない）ことを確認。出力は末尾数文字と長さで検証。
- `tests/common/strings/utf8_len_test.cm` + `.expect`: マルチバイト文字列で`len()`（コードポイント）と`byte_len()`（バイト）が期待値になり、jit/native/wasm/js/tsで一致することを確認。
- `tests/common/strings/embedded_nul_test.cm` + `.expect`: 埋め込みNULを含む文字列でデータが喪失しないこと（`byte_len`・NUL以降の内容）を検証（第4段以降）。negative check（NULで切れて短くならないこと）を含める。
- `tests/common/strings/utf8_index_test.cm` + `.expect`: コードポイント添字・部分文字列がバイト境界でなくコードポイント境界で切れることを確認。

## リスクと非互換性

- 文字列内部表現の(ptr,len)化（第4段）は最も影響が大きく、全バックエンドのコード生成・ランタイム・FFI境界（libc関数へstringを渡す箇所、例: input.cmの`read`/`malloc`、string.cmの`strlen`）に波及する。libc相互運用のためNUL終端も維持する二重保証で緩和する。
- `len()`のセマンティクス変更（バイト数→コードポイント数、第3段）は破壊的変更。ASCIIのみのプログラムは影響を受けないが、マルチバイトを扱い`len()`をバイト長として使っていたコードは`byte_len()`へ移行が必要。リリースノートで明記し、移行APIとして`byte_len()`を提供する。
- jsとnative/wasmで従来分裂していた`len`（UTF-16単位 vs バイト数）が、コードポイント基準へ統一される。これはバグ修正だが、いずれのバックエンドでも既存の観測値と変わる可能性がある。
- StringBuilderのバッファはdropパス未整備下ではリーク対象になりうる（監査C12/C13）。デストラクタ（`~self()`）で解放し、少なくとも明示スコープで解放されることを保証する。
- 大規模変更のため、第1段（StringBuilderのみ）を独立リリースとし、表現移行（第4段）はさらに後続へ回す判断も許容する。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（H9、および文字列比較のC2・派生OrdのC3・メモリ非解放のC12）
- 文字列型マッピング/生成: src/internal/codegen/llvm/core/types.cpp:58-60, :687-691, :726, translate/program.cpp:266, rvalue.cpp:609
- 連結/長さランタイム: src/internal/codegen/llvm/native/runtime_format.c:762, :1718, wasm/runtime_format.c:39, common/format_core.h:254, js/runtime.cpp:261-266, js/builtins.cpp:321
- std文字列API: libs/std/core/string.cm
