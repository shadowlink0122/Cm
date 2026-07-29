# 文字列のランタイム表現（native/jit）

Cmの`string`はLLVMレベルでは素の`char*`（opaqueポインタ）だが、ランタイム生成文字列はデータ直前に16バイトのメタデータヘッダを持つSDS（Simple Dynamic String）方式を採り、`char*`のABI・FFI互換を一切壊さずにO(1)バイト長取得と埋め込みNUL保持を実現している。連結・比較は常に内容ベースのランタイム関数（`cm_string_concat*`・`cm_strcmp`）へloweringされ、ポインタ同一性に依存しない。

## 概要

- 型マッピング: `hir::TypeKind::String`/`CString`はいずれも`ctx.getPtrType()`（opaqueポインタ）へ落ちる（src/internal/codegen/llvm/core/types.cpp:58-60）。(ptr,len)のfat pointerではない。
- 長さ情報はポインタの「直前」に置く: ランタイムが生成する文字列は、データ先頭の16バイト手前に`{magic, byte_len, magic2, reserved}`ヘッダを持つ（src/internal/codegen/llvm/native/runtime_format.c:39-44)。
- データ自体は常にNUL終端されるため、libc関数やC FFIへは`char*`のままゼロコストで渡せる（NUL終端と長さヘッダの二重保証）。
- ヘッダの無い文字列（外部由来のポインタ等）はマジック不一致で判別し、strlen走査へフォールバックする（runtime_format.c:33)。
- native AOTとjitは同一のランタイム実体を共有する: runtime_format.c等はruntime.cが単一翻訳単位として束ね（src/internal/codegen/llvm/native/runtime.c:18-25）、cm_runtime.oとしてcmバイナリ自身にリンクされる（CMakeLists.txt:606-627, 659-660）。jitはORCの`DynamicLibrarySearchGenerator`でホストプロセス（=cmバイナリ）から`cm_*`シンボルを解決する（src/internal/codegen/llvm/jit/jit_engine.cpp:83-97）。AOTリンク時は`~/.cm/lib/cm_runtime.o`等の探索パスから同じオブジェクトをリンクする（src/internal/codegen/llvm/native/codegen.cpp:1292-1298）。

## データ構造とアルゴリズム

### ヘッダレイアウトと確保

ヘッダは16バイト固定で、二重マジックによりヘッダ有無の誤判定を防ぐ（runtime_format.c:36-44）。

```c
#define CM_STR_MAGIC 0x434D5331u  /* "CMS1" */
#define CM_STR_MAGIC2 0x53315243u /* "S1RC" */

typedef struct {
    uint32_t magic;
    uint32_t byte_len;
    uint32_t magic2;
    uint32_t reserved;
} CmStrHdr;
```

確保は`cm_str_alloc`に一元化されている（runtime_format.c:48-66）。データがページ先頭16バイト未満に落ちると後述の読み取りゲートが自前文字列を判別できなくなるため、その場合は配置を16バイトずらし、確保起点からのオフセットを`reserved`へ記録する（解放時に`data - reserved`で起点を復元する）。末尾NULもここで必ず書く（`data[len] = 0`）。

```c
static inline char* cm_str_alloc(size_t len) {
    char* raw = (char*)cm_alloc(sizeof(CmStrHdr) + len + 1 + 16);
    ...
    char* data = raw + sizeof(CmStrHdr);
    if (((uintptr_t)data & 4095u) < sizeof(CmStrHdr)) {
        data += 16;
    }
    CmStrHdr* hdr = (CmStrHdr*)(data - sizeof(CmStrHdr));
    hdr->magic = CM_STR_MAGIC;
    hdr->byte_len = (uint32_t)len;
    ...
    data[len] = 0;
    return data;
}
```

### 前方読みの安全ゲートとO(1)バイト長

任意の`char*`に対してヘッダの有無を判定する`cm_str_hdr`は、未知ポインタの16バイト手前を読んでも安全なように多段ゲートを敷く（runtime_format.c:69-85）。ヘッダ付き文字列のデータは必ず16バイト整列なので、未整列ポインタは即ヘッダ無し確定、ページ先頭16バイト未満は前方が未マップの可能性があるため読まずにフォールバックする。マジック2つの一致に加えて`s[hdr->byte_len] == 0`の終端検証も行い、偶然の一致による誤判定を実質ゼロにしている。

`cm_string_byte_len`はヘッダがあればO(1)、無ければstrlen相当のO(n)走査になる（runtime_format.c:88-98）。`s.byte_len()`はHIR loweringで`__builtin_string_len`（runtime_format.c:996-999）へ写像され、この関数を経由する。

### リテラルの発行

文字列リテラルはコード生成側が「ヘッダ + NUL終端データ」のパック構造体グローバル定数として発行し、式の値としてはデータ先頭（グローバル+16バイト）へのGEPを返す（src/internal/codegen/llvm/core/types.cpp:670-687）。アラインメント16を明示指定し、ランタイムの読み取りゲートと整合させている。

```cpp
llvm::Constant* MIRToLLVM::createHeaderedStringLiteral(const std::string& str) {
    auto bytes = llvm::ConstantDataArray::getString(llvm_ctx, str, true);
    auto sty = llvm::StructType::get(llvm_ctx, {i32, i32, i32, i32, bytes->getType()}, /*isPacked=*/true);
    auto init = llvm::ConstantStruct::get(
        sty, {llvm::ConstantInt::get(i32, 0x434D5331u),
              llvm::ConstantInt::get(i32, static_cast<uint32_t>(str.size())),
              llvm::ConstantInt::get(i32, 0x53315243u), llvm::ConstantInt::get(i32, 0), bytes});
    ...
    gv->setAlignment(llvm::Align(16));
    llvm::Constant* idx[] = {llvm::ConstantInt::get(ctx.getI64Type(), 16)};
    return llvm::ConstantExpr::getInBoundsGetElementPtr(i8, gv, idx);
}
```

MIR定数のstd::string値は全てこの経路を通る（types.cpp:743-746）。string型のnullリテラルは`ConstantPointerNull`になる（types.cpp:705-711）。

### 連結

`+`演算子はLLVM二項演算のAdd変換でポインタ同士の場合に`cm_string_concat`呼び出しへ置換される（src/internal/codegen/llvm/core/operators.cpp:221-225、配列型経路は:260-264）。ランタイム実体は両辺のバイト長（ヘッダ経由でO(1)）を取り、`cm_str_alloc`で1回確保して`memcpy`する（runtime_format.c:2187-2203）。長さがヘッダ由来のため、埋め込みNULを含む文字列も欠落なく連結される。

`a + b + c (+ d)`の連結チェーンはMIR loweringの`lower_binary`で平坦化され、`cm_string_concat3`/`cm_string_concat4`（runtime_format.c:2145-2185）へまとめられる（src/internal/mir/lowering/expr/binary.cpp:43-108）。3要素で中間確保2回→1回、4要素で3回→1回に減る。中間・最終結果は`ctx.note_string_temp`で無名一時としてdropパスへ登録される（binary.cpp:101-104）。

### 比較

文字列の比較は全てポインタ同一性でなく内容比較になる。等値（`==`/`!=`）はポインタ同士の比較を`cm_strcmp`（自前実装、runtime_format.c:188-201）呼び出し + 0との比較へ変換する（operators.cpp:429-437, 472-479）。順序比較（`<`/`<=`/`>`/`>=`）も同様に`cm_strcmp`の符号判定へ変換するが、こちらはオペランドのCm型がstring/cstringの場合に限定するゲートが入る（operators.cpp:98-103, 510-518, 541-549, 572-580, 603-611）。LLVM上はstringも`int*`も同じptr型のため、このゲートが無いとポインタ同士の順序比較までstrcmpになりポインタ演算が壊れる（operators.cpp:97-99のコメントが回帰理由を明記している）。

### 解放と所有

文一時や再代入で不要になった文字列は`cm_string_free`で解放する（runtime_format.c:2206-2221）。ヘッダ付きの場合は解放前にマジックを消去してからreservedオフセットで確保起点を復元して解放し、ブロック再利用時の残留ヘッダ誤認を防ぐ。どのランタイム関数の戻り値が新規所有バッファか（`cm_string_concat*`・`*_to_string`・`cm_format_*`）はMIRクリーンアップパスがホワイトリストで判定する（src/internal/mir/passes/cleanup/string_reassign_free.cpp:23-38）。

### 埋め込みNULを含む文字列の構築

埋め込みNULは通常のリテラルでは表現できないため、バイト列から長さ指定で構築する`cm_string_from_bytes`が第一級コンストラクタとなる（runtime_format.c:118-128）。スライスを受けるラッパー`cm_string_from_byte_slice`（src/internal/codegen/llvm/native/runtime_slice.c:996-1001）を、標準ライブラリの`std::strings::from_bytes(utiny[])`が呼び出す（libs/std/strings/from_bytes.cm:11-13）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/codegen/llvm/core/types.cpp | 型マッピング（:58-60）、ヘッダ付きリテラル発行（:670-687）、null定数（:705-711） |
| src/internal/codegen/llvm/core/operators.cpp | `+`→concat（:221-225, :260-264）、`==`等→cm_strcmp（:429-437, :472-479, :510-518他） |
| src/internal/codegen/llvm/core/runtime/builtins.cpp | concat/concat3/concat4/cm_string_free等のシグネチャ登録（:111-131） |
| src/internal/codegen/llvm/native/runtime_format.c | CmStrHdr・cm_str_alloc・cm_str_hdr・byte_len（:36-98）、concat群（:2145-2203）、cm_string_free（:2206-2221）、from_bytes（:118-128） |
| src/internal/codegen/llvm/native/runtime_slice.c | cm_string_from_byte_slice（:996-1001） |
| src/internal/codegen/llvm/native/runtime.c | ランタイム単一翻訳単位（jit/native共用、:18-25） |
| src/internal/mir/lowering/expr/binary.cpp | 連結チェーン平坦化→concat3/4（:43-108） |
| src/internal/mir/passes/cleanup/string_reassign_free.cpp | fresh所有バッファの判定と再代入時解放（:23-38） |
| src/internal/codegen/llvm/jit/jit_engine.cpp | jitのランタイムシンボル解決（:83-97） |
| libs/std/strings/from_bytes.cm | 埋め込みNUL対応の構築API |

## 落とし穴とケア

- 防ぐバグのクラス: 埋め込みNULによるデータ喪失（長さがヘッダ由来のため連結・byte_lenがNULで切れない）、`len`取得の毎回O(n)走査、`==`のポインタ同一性比較（同内容でも不一致になる誤り）、`int*`同士の順序比較がstrcmpへ化けるポインタ演算破壊（operators.cpp:98-103の型ゲートが防ぐ）。
- 維持すべき不変条件: ランタイムで文字列を生成する全プロデューサは`cm_str_alloc`を経由する（直接`cm_alloc`した文字列はヘッダ無し扱いになりO(1)長・埋め込みNUL保持が効かない）。ヘッダ付きデータは16バイト整列かつページ先頭16バイト以上のオフセットに置く。リテラル発行側のパックレイアウト・align 16・+16オフセットはランタイムの`CmStrHdr`とバイト単位で一致させる。解放時は必ずマジックを消去する。
- ヘッダ判定は「未知ポインタでも安全に失敗する」設計であり、判定に失敗してもstrlenフォールバックで正しさは保たれる（性能が落ちるだけ）。この性質を壊す変更（フォールバック削除等）をしてはならない。
- C FFI境界では長さ情報は伝わらない（`char*`のNUL終端契約のみ）。埋め込みNULを含む文字列をlibcへ渡せばNULまでしか見えないのは仕様である。
- 回帰テストの場所: tests/common/strings/embedded_nul_test.cm（埋め込みNUL保持）、tests/common/strings/concat_chain_test.cm（連結チェーン）、および文字列比較・フォーマット系の各バックエンドスイート（tests/common/）。

## 関連資料

- 設計経緯: [文字列の(ポインタ,長さ)表現・UTF-8対応・StringBuilder導入](../../archive/v0.17.0/strings-utf8-and-stringbuilder.md)
- UTF-8コードポイントAPI: [utf8.md](utf8.md)
- StringBuilder: [stringbuilder.md](stringbuilder.md)
