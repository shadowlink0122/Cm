# FFIとextern宣言のlowering

CmのFFI（外部関数インターフェイス）は、`use libc { <Cシグネチャ>; ... }` ブロックと `extern "C" <Cシグネチャ>;` の2つの宣言形式で外部C関数を取り込み、`is_extern` フラグをAST→HIR→MIR→LLVMへ一貫して伝搬させ、コード生成では本体なしの宣言のみを発行して、シンボル解決をnativeではシステムリンカに、jitではORCの `DynamicLibrarySearchGenerator` に委ねる設計である。
本書は宣言のパースからHIR/MIR lowering、型検査の範囲、LLVMでの外部シンボル発行、native/jitそれぞれのリンク解決までを記述する。

## 概要

FFI宣言はどちらの形式でもC言語スタイルのシグネチャ（戻り値型が先頭、後置 `*` ポインタ）で書き、Cm側からは通常の関数と同じ構文で呼び出せる。

```cm
// 形式1: use ブロック（libs/native/io/file.cm:9-18）
use libc {
    long read(int fd, void* buf, long count);
    long write(int fd, void* buf, long count);
    int open(string path, int flags, int mode);
    int close(int fd);
    long strlen(string s);
}

// 形式2: 単一のextern "C"宣言（libs/std/strings/builder.cm:7-9）
extern "C" long cm_sb_create();
extern "C" void cm_sb_append(long handle, string s);
extern "C" string cm_sb_to_string(long handle);
```

形式1はlibc関数やパッケージ単位の取り込みに、形式2はCmランタイム（`cm_*` 関数群）や自作Cライブラリの個別宣言に使われる。
両者はパース後の表現が異なる（`UseDecl::FFIUse` と `ExternBlockDecl`/`FunctionDecl`）が、HIR loweringで同じ `HirExternBlock` + `is_extern` 付き `HirFunction` へ合流し、以降のパイプラインは区別しない。
extern関数は本体を持たないため、MIRでは引数ローカルだけを持つ空関数、LLVMでは宣言のみの `llvm::Function` になり、実体はリンク段（native）またはJITのシンボル解決（jit）で束縛される。

## データ構造とアルゴリズム

### パース: 2形式のエントリポイント

`extern` キーワードは `Parser::parse_extern`（`src/internal/syntax/parser/module/toplevel.cpp:376-415`）が処理し、`extern "C" { ... }` ブロックなら `ast::ExternBlockDecl`（`src/internal/syntax/ast/decl.hpp:381-388`）を作り、単一宣言なら `parse_extern_func_decl`（`toplevel.cpp:418-437`）で `is_extern = true` の `ast::FunctionDecl` を作る。
シグネチャのC互換性は `parse_extern_type`（`toplevel.cpp:440-471`）が担い、`const` 修飾子の読み飛ばし、後置 `*` によるポインタ化（`char*`・`void*` 等）、後置 `[]`/`[N]` による配列型をサポートする。

`use` キーワード側は `Parser::parse_use`（`src/internal/syntax/parser/module/decl.cpp:378-517`）が処理し、識別子パス形式 `use libc { ... }`（`decl.cpp:457-508`）と文字列パッケージ形式 `use "pkg" { ... }`（`decl.cpp:394-432`）の両方でFFIブロックを受け付ける。
ブロック内の各宣言は `ast::FFIFunctionDecl`（`src/internal/syntax/ast/module.hpp:192-200`）として名前・戻り値型・パラメータ列・`is_variadic` を保持し、`...` トークンで可変長引数をマークする（`decl.cpp:477-481`）。
結果は `ast::UseDecl`（`module.hpp:205-245`）に `kind = FFIUse` で格納され、文字列形式の場合のみ `package_name` フィールド（`module.hpp:213`）にパッケージ名が入る（`use libc` のような識別子パス形式では `package_name` は空のまま `path` に載る）。
`use libc as c { ... }` のエイリアス形式では `alias` が保持され、`c::open` のような修飾呼び出しを可能にする。

### HIR lowering: HirExternBlockへの合流

`HirLowering::lower_extern_block`（`src/internal/hir/lowering/decl.cpp:46-61`）は `ExternBlockDecl` 内の各関数を `is_extern = true` の `HirFunction` に変換して `HirExternBlock`（`src/internal/hir/nodes.hpp:550-555`）へ束ねる。
単一の `extern "C"` 宣言は通常の `lower_function`（`decl.cpp:78-133`）を通り、`hir_func->is_extern = func.is_extern` で伝搬される（`decl.cpp:86`）。
`UseDecl::FFIUse` は `lower_use`（`decl.cpp:428-468`）が同じく `HirExternBlock` へ変換し、`package_name` の引き継ぎ（`decl.cpp:435`）と `is_variadic` の伝搬（`decl.cpp:442`）を行う。
エイリアス付きの場合は `alias::関数名 → 実関数名` の対応を `import_aliases_` に登録し（`decl.cpp:450-453`）、呼び出し式の解決で素の関数名へ剥がす。
`HirFunction` は `is_extern`/`is_variadic` フラグを持ち（`nodes.hpp:395-396`）、本体 `body` は空のままである。

### 型検査: 登録される範囲とされない範囲

型検査のPass 1で `ExternBlockDecl` と `FFIUse` の各関数がグローバルスコープへ関数シンボルとして登録される（`src/internal/types/checking/decl.cpp:481-502`）。
以降、呼び出し側は通常の関数呼び出しと同じ検査を受ける: 引数個数の一致、宣言されたパラメータ型と実引数型の適合である。
可変長引数関数は固定部分の個数を下限として検査し、固定部分のみ型を照合する（`src/internal/types/checking/call/function.cpp:563-575`）。
一方、宣言シグネチャそのものがCライブラリの実際のプロトタイプと一致しているかは検査されない（コンパイラはCヘッダを読まない）。
`long read(int fd, void* buf, long count)` を誤って `int read(...)` と書いてもコンパイルは通り、ABI不一致による誤動作は実行時に現れるため、宣言の正しさはユーザー責任である。
また、extern関数は本体を持たないため、戻り値パスの検査や到達性検査などの本体検査から除外される（`decl.cpp:288`、`decl.cpp:1086`）。

Cm型とC型の対応はLLVM型変換 `convertType`（`src/internal/codegen/llvm/core/types.cpp:26-84`）がそのまま規定する。

| Cm型 | C型 | LLVM型 | 根拠 |
|---|---|---|---|
| `int` / `uint` | `int` / `unsigned` | `i32` | `types.cpp:42-44` |
| `long` / `ulong` | `long`（LP64） / `size_t` | `i64` | `types.cpp:45-47` |
| `string` | `char*`（NUL終端互換） | `ptr` | `types.cpp:58-60` |
| `void*` / `T*` | `void*` / `T*` | `ptr`（opaque pointer） | `types.cpp:61-72` |
| `float` / `double` | `float` / `double` | `float` / `double` | `types.cpp:52-57` |

`string` を直接 `char*` として渡せるのは、ランタイム文字列がchar*互換のSDSヘッダ方式（ポインタの手前にメタデータを置き、ポインタ自体はNUL終端バイト列を指す）だからである（詳細は[文字列のランタイム表現](../strings/representation.md)）。

### MIR lowering: 空関数化とマングリング除外

`MirLowering::lower_functions`（`src/internal/mir/lowering/auto_impl.cpp:890-937`）は事前パスで `HirExternBlock` 内の関数も関数マップへ登録し（`auto_impl.cpp:896-899`）、本パスでブロックを展開して各関数を個別の `MirFunction` にし、`package_name` を引き継ぐ（`auto_impl.cpp:926-937`）。
`lower_function`（`src/internal/mir/lowering/impl.cpp:150-202`）は `is_extern`/`is_variadic` を `MirFunction`（`src/internal/mir/nodes.hpp:439-441`）へ設定し、extern関数については引数ローカルの記録だけで基本ブロックを一切作らずに早期returnする（`impl.cpp:191-202`）。

extern関数はシンボル名がCライブラリ側の名前と厳密に一致しなければならないため、オーバーロード用の型サフィックスマングリングから除外される。

```cpp
// src/internal/codegen/llvm/core/translate/signature.cpp:41-44
// 外部関数（extern）はそのまま
if (func.is_extern) {
    return func.name;
}
```

これにより `open` や `pthread_mutex_lock` はCm内部のマングリング規約（`関数名_型サフィックス`）を受けず、素の名前でLLVMモジュールに現れる。

### LLVMコード生成: 宣言のみのllvm::Function

関数シグネチャ変換（`signature.cpp:316-328`）では `llvm::FunctionType::get(returnType, paramTypes, func.is_variadic)` で可変長引数を含む関数型を構築し、extern関数は `module->getOrInsertFunction` で宣言のみを作成する（既存宣言があれば再利用して重複を防ぐ）。
関数本体の変換 `convertFunction`（`src/internal/codegen/llvm/core/translate/function.cpp:25-29`）はextern関数を先頭で弾き、基本ブロックを生成しない。
結果としてLLVM IRには `declare i64 @read(i32, ptr, i64)` のような外部リンケージの宣言だけが載り、呼び出しは通常の `call` 命令になる。
また、到達可能性ベースの関数削減ではextern関数がエントリポイント扱いでルート集合に入り（`src/internal/codegen/llvm/core/translate/program.cpp:396-402`）、宣言が誤って刈られることはない。
`printf` のような可変長引数関数の呼び出しは `use` ブロックの `...` 宣言経由でC可変長引数ABIのままcallされ、コンパイラ側でのva_list構築や引数昇格の追加処理は行わない。

### リンク解決: nativeとjitの2経路

nativeバックエンドはオブジェクトファイル生成後にシステムのCコンパイラドライバ（macOSは `/usr/bin/clang++`、Linuxは `clang`）でリンクし、libcはドライバの既定リンクで、Cmランタイムは `findRuntimeLibrary()` の同梱静的ライブラリで解決する（`src/internal/codegen/llvm/native/codegen.cpp:527-609`）。
`cm_thread_*`/`cm_mutex_*` 等のランタイム呼び出しを検出すると `-lpthread` や各バッキングライブラリを自動追加する（`codegen.cpp:477-510`）。
未解決シンボルが残ればリンカエラーとして表面化するため、宣言だけして実体のない関数はこの段で検出される。

jitバックエンドはLLJITのメインJITDylibに `DynamicLibrarySearchGenerator::GetForCurrentProcess` をジェネレータ登録し、ホストプロセス（cmバイナリ自身とそれがロードしているlibc・ランタイム）から全シンボルを動的解決する（`src/internal/codegen/llvm/jit/jit_engine.cpp:83-97`）。
cmバイナリ自体がlibcとCmランタイムをリンク済みであるため、`malloc`/`printf`/`cm_sb_create` 等は明示登録なしで解決される。
裏返すと、jit実行でホストプロセスに存在しない外部ライブラリの関数は解決できず、その場合はnativeビルドでリンク対象を指定する必要がある。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/syntax/parser/module/toplevel.cpp:376-509` | `extern "C"` のパース（`parse_extern`/`parse_extern_func_decl`/`parse_extern_type`/`parse_extern_params`） |
| `src/internal/syntax/parser/module/decl.cpp:378-517` | `use libc { ... }`/`use "pkg" { ... }` のパースと `...` 可変長引数 |
| `src/internal/syntax/ast/decl.hpp:381-388`、`src/internal/syntax/ast/module.hpp:192-245` | `ExternBlockDecl`・`FFIFunctionDecl`・`UseDecl`（`package_name`/`is_variadic`）のAST表現 |
| `src/internal/hir/lowering/decl.cpp:46-61,428-468` | `HirExternBlock` への合流、`is_extern`/`is_variadic`/`package_name` 伝搬、エイリアス登録 |
| `src/internal/types/checking/decl.cpp:481-502` | extern関数のグローバル関数シンボル登録（Pass 1） |
| `src/internal/types/checking/call/function.cpp:563-575` | 可変長引数呼び出しの下限個数・固定部分型検査 |
| `src/internal/mir/lowering/auto_impl.cpp:890-937`、`src/internal/mir/lowering/impl.cpp:150-202` | externブロック展開と本体なし `MirFunction` 生成 |
| `src/internal/codegen/llvm/core/translate/signature.cpp:41-44,316-328` | マングリング除外と宣言のみの `llvm::Function` 発行（variadic対応） |
| `src/internal/codegen/llvm/jit/jit_engine.cpp:83-97` | `DynamicLibrarySearchGenerator` によるホストプロセスからのシンボル解決 |
| `src/internal/codegen/llvm/native/codegen.cpp:477-609` | システムリンカ起動とランタイム・pthread等の自動リンク |

## 落とし穴とケア

- 宣言シグネチャの正しさは検査されない: Cヘッダとの照合は行われないため、型幅の誤り（`int` と `long` の取り違え等）はコンパイルを通過し実行時に壊れる。特にLP64環境の `size_t`/`ssize_t` はCm側で `long`/`ulong` と書く必要がある。
- 可変長引数 `...` は `use` ブロック形式でのみ書ける: `extern "C"` 単一宣言のパラメータパース（`toplevel.cpp:474-500`）は `...` を受け付けないため、`printf` 系は `use libc { int printf(string fmt, ...); }` の形で宣言する。
- 構造体は原則ポインタで渡す: Cmの構造体値渡しABIとCコンパイラの集約渡しABIの一致は保証されないため、FFI境界では `void*` または `T*` を介して受け渡すのが安全である（実在コードもすべてポインタ渡しで統一されている）。
- コールバックは `関数名 as void*` で渡す: `spawn(worker as void*)` や `set_allocator_fns(counting_alloc as void*, ...)` のように関数参照を `void*` へキャストして渡し、C側で関数ポインタとして呼び出す。シグネチャの一致は検査されないためユーザー責任である。
- プラットフォーム依存の不透明型サイズに注意: `pthread_rwlock_t` はmacOSで200バイト（Linuxは56バイト）あり、`long[8]`（64バイト）で確保すると初期化がバッファ外へ書き込みスタックを破壊してSIGILLになる。`libs/native/sync/mod.cm:89-94` の `RawRwLock` は全プラットフォームの最大に余裕を持たせた `long[32]`（256バイト）を確保しており、C側の不透明型をCm構造体で持つ場合は最大サイズ側に合わせるのが原則である。
- `string` の受け渡しはNUL終端前提: SDS表現によりCm文字列はそのまま `char*` として渡せるが、C側は最初のNULまでしか読まないため、埋め込みNULを含む文字列やバイナリデータは `void*` + 長さで渡す。
- js/wasmバックエンドは別経路: extern関数の `package_name` はjsバックエンドでrequire生成の判断に使われ、`"js"`・`"libc"`・空文字列は組み込み扱いでrequireを生成しない（`src/internal/codegen/js/codegen.cpp:217-238`、`src/internal/codegen/js/emit_statements.cpp:379-395`）。libc関数がnative/jitのように自動でホストのCライブラリへ束縛されるわけではなく、この境界を越えるFFIはバックエンドごとの解決機構に従う（本書の対象はnative/jitのため詳細は扱わない）。

## 関連資料

- [LLJITエンジン](../codegen-jit/lljit-engine.md) — jit実行の全体像とシンボル解決・ランタイム束縛
- [リンクとランタイム](../codegen-native/linking-and-runtime.md) — nativeリンクコマンドの構成とランタイムライブラリ探索
- [文字列のランタイム表現](../strings/representation.md) — `string` がchar*互換でFFIへ渡せる理由（SDSヘッダ方式）
- [アロケータ](../memory/allocator.md) — `cm_mem_*`/`set_allocator_fns` というextern関数ファサードの実例
- [シンボルマングリング](../generics/mangling.md) — extern以外の関数に適用されるマングリング規約
- [コンパイルパイプライン全体像](../pipeline/overview.md) — 本書が扱うAST→HIR→MIR→LLVMの段構成
