---
title: セルフホスト準備（OS連携APIの整備）
parent: v0.17.0 Design
---

# セルフホスト準備（OS連携APIの整備）

## 目的とスコープ

セルフホスト（CmによるCmコンパイラのnative/jit実装）は roadmap_v1.0.0.md の通り1.0以降の研究テーマだが、その前提となるOS連携APIが現状のstd/nativeライブラリに欠けている。
v0.17.0では「Cmでコンパイラのようなツール（ファイルを読み、解析し、バイナリを書き出し、外部コマンドを呼ぶプログラム）を書ける」状態まで標準ライブラリとランタイムを整備する。
CmコンパイラそのものをCmで書き始めることは本バージョンの対象外とする。

## 現状調査（ギャップ分析）

### 前提条件の充足状況

コンパイラという負荷が直撃する言語側の既知バグは、v0.17.0の監査対応で解消済み。

- 型同一性（C7/C8/C9・C16）: 修正済み。`Vec<Vec<Token>>`・`Option<Box<Ast>>`等のネスト型引数が正しく単相化される
- 文字列の順序比較（C2/C3）: 修正済み。`TreeMap<string, V>`のキー比較・`with Ord`が字句順で機能する
- インターフェイス値の集約格納（H1/H2）: 修正済み。トレイトオブジェクトのコレクションが使える
- `StringBuilder`（H9第1段）: 追加済み。出力バッファリングのO(n²)回避が可能

### 揃っているOS連携機能（native/jit）

| 機能 | API | 場所 |
|------|-----|------|
| テキストファイル読み書き | `read_file` / `write_file` / `append_file` / `exists` / `remove` / `size` | libs/std/fs/mod.cm |
| 低レベルファイルI/O | libc `open` / `read` / `write` / `lseek` / `close` | libs/native/io/file.cm |
| 標準入出力 | `print` / `println` / `eprintln`（fd 2） / `Stdin.read_line` / `BufferedReader/Writer` | libs/std/io・libs/native/io/stream |
| 文字列操作 | `substring` / `indexOf` / `startsWith` / `trim` / `replace` / `parse_int` / `StringBuilder` | ビルトイン + libs/std/strings |
| コレクション | `Vector<T>` / `TreeMap<K,V>`（文字列キー可） / `Queue` | libs/std/collections |
| バイト列 | `utiny[]`（IO層で第一級） | libs/native/io/traits.cm 等 |
| プロセス終了 | `exit` / `panic`（ビルトイン） | hir/lowering/expr.cpp |

### 欠けているOS連携機能

| # | 機能 | 現状 | 必要性 | 対応方法 |
|---|------|------|--------|----------|
| S1 | コマンドライン引数 | Cmの`main`はargc/argvを受け取らず、取得APIが皆無 | CLIツールの必須要件 | ランタイム改修（後述） |
| S2 | 環境変数 | `getenv`/`setenv`のCm側バインディング無し | `CM_MODULE_PATH`等の解決 | libc FFI宣言のみ |
| S3 | サブプロセス起動 | `system`/`popen`のバインディング無し | リンカ（clang/ld）呼び出し | libc FFI宣言のみ |
| S4 | ディレクトリ操作 | `mkdir`/`readdir`/`rmdir`とも無し | 出力ディレクトリ作成・モジュール探索 | mkdir/rmdirはFFI、readdirはCシム |
| S5 | バイナリ安全なファイルI/O | `cm_file_write_all`等が内部`strlen`のためNULで切れる | オブジェクトファイルの読み書き | ランタイムシム + Cmラッパー |
| S6 | 実行ファイル自身のパス取得 | 無し（現行C++コンパイラは`_NSGetExecutablePath`/`readlink`を使用） | 同梱stdライブラリの探索 | ランタイムシム |
| S7 | `string.split` | ビルトインにもstdにも無し | ソース行分割・オプション解析 | 純Cmで実装 |
| S8 | パス操作 | join/dirname/basename/extensionが無い | 入出力パスの組み立て | 純Cmで実装 |
| S9 | エンディアン指定のバイト詰め | `to_le_bytes`相当が無い | オブジェクトファイル生成（将来） | 純Cmで実装 |

## 設計方針

- 対象バックエンドはnative/jitのみとする。両者はランタイムCファイルを共有するため実装は一系統で済む。js/wasmはOS連携の性質上対象外とし、backend_support_matrix.mdに未対応と明記する
- libc FFI宣言（`use libc { ... }`）で済むものはCmソースだけで完結させ、ランタイムC改修は「libcだけでは実現できないもの」（S1・S4のreaddir・S5・S6）に限定する
- 既存の`module std.fs`のAPIスタイル（bool/値返却の基本API + `Result`返却の推奨API）を踏襲する
- Cm側の`main`シグネチャは変えない（破壊的変更の回避）。argc/argvはランタイムが保持し、`std::env::args()`で取得する
- ランタイムCの追加はファイル新設を避け、既存の`runtime_platform.c`（args/env/実行パス）と`runtime_file.c`（ディレクトリ・バイナリI/O）へ追記する

## 追加API仕様

### std::env（新設: libs/std/env/mod.cm）

```cm
import std::env;

int main() {
    // コマンドライン引数（先頭は実行ファイル名）
    const string[] argv = env::args();
    for (int i = 0; i < argv.len(); i++) {
        println("arg[{i}] = {argv[i]}");
    }

    // 環境変数
    match (env::get("CM_MODULE_PATH")) {
        Some(v) => println("module path: {v}"),
        None => println("module path: (unset)"),
    }
    env::set("CM_DEBUG", "1");

    // 自分自身の実行ファイルパス
    const string exe = env::current_exe();
    println("exe = {exe}");
    return 0;
}
```

```
$ ./tool input.cm -o out
arg[0] = ./tool
arg[1] = input.cm
arg[2] = -o
arg[3] = out
module path: (unset)
exe = /Users/user/bin/tool
```

- `string[] args()`: ランタイムの`cm_arg_count()`/`cm_arg_get(i)`から構築する
- `Option<string> get(string name)`: libc `getenv`。未設定は`None`
- `bool set(string name, string value)`: libc `setenv(name, value, 1)`
- `string current_exe()`: ランタイム`cm_current_exe()`（macOS: `_NSGetExecutablePath`、Linux: `readlink("/proc/self/exe")`）

### std::process（新設: libs/std/process/mod.cm）

```cm
import std::process;

int main() {
    // 終了コードだけ欲しい場合（リンカ呼び出し等）
    const int code = process::run("clang -o out main.o runtime.o");
    if (code != 0) {
        eprintln("link failed: {code}");
        return 1;
    }

    // 標準出力を取得したい場合
    match (process::output("clang --version")) {
        Ok(text) => println(text),
        Err(e) => eprintln(e),
    }
    return 0;
}
```

- `int run(string cmd)`: libc `system`の薄いラッパー。シェル経由で実行し終了コードを返す
- `Result<string, string> output(string cmd)`: libc `popen`/`fgets`/`pclose`で標準出力を収集する（`StringBuilder`で連結）
- `exit`は既存ビルトインのため追加しない

### std::fs拡張（libs/std/fs/mod.cm へ追記）

```cm
import std::fs;

int main() {
    // ディレクトリ
    fs::create_dir(".build");
    const string[] entries = fs::read_dir("src");

    // バイナリ安全な読み書き（NUL・非UTF-8を含んでよい）
    match (fs::read_bytes("input.o")) {
        Ok(data) => {
            println("read {data.len()} bytes");
            fs::write_bytes("copy.o", data);
        },
        Err(e) => eprintln(e),
    }
    return 0;
}
```

- `bool create_dir(string path)` / `bool remove_dir(string path)`: libc `mkdir(path, 0755)` / `rmdir`
- `string[] read_dir(string path)`: ランタイム`cm_dir_open`/`cm_dir_next`/`cm_dir_close`（`readdir`は`struct dirent*`を返すためCシム経由。`.`と`..`は除外し、順序はソートして返す）
- `Result<utiny[], string> read_bytes(string path)`: `cm_file_size`で長さを取り、`cm_file_read_bytes(path, buf, len)`で読み込む
- `bool write_bytes(string path, utiny[] data)` / `Result<bool, string> write_bytes_all(...)`: `cm_file_write_bytes(path, buf, len)`。長さ明示のため埋め込みNULで切れない

### std::path（新設: libs/std/path/mod.cm、純Cm実装）

```cm
import std::path;

const string p = path::join("src", "main.cm");   // "src/main.cm"
const string d = path::dirname("src/main.cm");   // "src"
const string b = path::basename("src/main.cm");  // "main.cm"
const string e = path::extension("main.cm");     // "cm"
const string s = path::with_extension("main.cm", "o");  // "main.o"
```

- 区切りは`/`固定とする（現行コンパイラの対応プラットフォームはmacOS/Linuxであり、Windowsは対象外）
- ビルトインの`indexOf`/`substring`/`endsWith`のみで実装し、FFIを使わない

### std::strings::split（libs/std/strings/ へ追記、純Cm実装）

```cm
import std::strings;

const string[] parts = strings::split("a,b,,c", ",");   // ["a", "b", "", "c"]
const string[] lines = strings::lines("x\ny\n");        // ["x", "y"]
```

- `string[] split(string s, string sep)`: `indexOf`+`substring`による走査。空要素は保持する。`sep`が空文字列の場合は1文字ずつ分割する
- `string[] lines(string s)`: `\n`区切り（末尾の空要素は落とす）。`\r\n`は`\n`に正規化する

### std::bytes（新設: libs/std/bytes/mod.cm、純Cm実装）

```cm
import std::bytes;

utiny[] buf = [];
bytes::push_u32_le(buf, 0xFEEDFACF);  // Mach-Oマジック等
bytes::push_u16_le(buf, 12);
const uint magic = bytes::read_u32_le(buf, 0);
```

- `push_u16_le` / `push_u32_le` / `push_u64_le`とビッグエンディアン版、対応する`read_*`を提供する
- シフトとマスクだけの純Cm実装。オブジェクトファイル生成（セルフホスト本体）で必要になるが、実装が小さいため本バージョンで先行整備する

## ランタイム変更（S1・S4・S5・S6）

### コマンドライン引数（S1）

Cmの`main`はLLVMの`main`シンボルとして直接発行される（translate/signature.cpp:27でエントリポイント特別扱い）。
このシグネチャを`i32 main(i32 %argc, ptr %argv)`へ変更し、関数プロローグでランタイムの`cm_args_init(argc, argv)`を呼んでから利用者コードへ入る。

- runtime_platform.cへ追加: `void cm_args_init(int argc, char** argv)`（静的グローバルへ保存）、`int cm_arg_count(void)`、`const char* cm_arg_get(int i)`
- Cm言語仕様は変更しない。`int main()`のまま`std::env::args()`で取得する
- jit（`cm run`）: JITはホストプロセスのシンボルを解決するため、run.cppがエントリ呼び出し前にホスト側で`cm_args_init`を呼ぶ。渡す値は`cm run file.cm -- <args...>`の`--`以降とし、`argv[0]`にはスクリプトパス（file.cm）を入れる
- `#[test]`実行では空のargv（`argc=1`、`argv[0]`=テスト名）で初期化し、テストが`args()`を呼んでもクラッシュしないようにする

### ディレクトリ列挙・バイナリI/O・実行パス（S4・S5・S6）

runtime_file.cへ追加:

```c
// ディレクトリ列挙（dirent*をCm側へ出さないためのハンドルシム）
void* cm_dir_open(const char* path);
const char* cm_dir_next(void* handle);   // 終端はNULL、"."/".."はスキップ
void cm_dir_close(void* handle);
int cm_dir_create(const char* path);     // mkdir(path, 0755)
int cm_dir_remove(const char* path);     // rmdir

// バイナリ安全I/O（長さ明示、strlenを使わない）
long cm_file_read_bytes(const char* path, void* buf, long buf_size);   // 読めたバイト数、失敗は-1
int cm_file_write_bytes(const char* path, const void* buf, long len);  // 成功1、失敗0
```

runtime_platform.cへ追加:

```c
// 実行ファイルの絶対パス（macOS: _NSGetExecutablePath + realpath、Linux: readlink("/proc/self/exe")）
const char* cm_current_exe(void);
```

## 段階分割

| 段階 | 内容 | 変更範囲 | 状態 |
|------|------|----------|------|
| 第1段 | libc FFIのみで済むAPI（S2 env::get/set、S3 process::run/output、S4のcreate_dir/remove_dir）と純Cm実装（S7 split/lines、S8 path、S9 bytes） | libs/のみ（Cmソースだけで完結） | 実装済み（std::env/std::process/std::path/std::bytes新設、strings::split/lines・fs::create_dir/remove_dir追加。名前空間形式import未対応のため選択的import＋エイリアスで使用。bytesの64ビットread/writeはJS 53bit精度のため非対応。テスト: tests/common/{strings,path,bytes,env,process,fs}） |
| 第2段 | ランタイムシム追加（S4 readdir、S5 バイナリI/O、S6 current_exe）とstd::fs拡張 | runtime_file.c・runtime_platform.c + libs/std/fs | 実装済み（cm_dir_open/next/close・cm_file_read_bytes/write_bytes・cm_current_exe追加。fs::read_dir（名前昇順）/read_bytes/write_bytes、env::current_exe。ジェネリック型引数のスライス型（Result<utiny[], string>）のパーサ対応も同時に実施。テスト: tests/common/fs/bytes_dir_test・tests/common/env/current_exe_test） |
| 第3段 | argv（S1）: mainシグネチャ変更 + cm_args_init + jit/testランナー連携 + std::env::args() | signature.cpp・function.cpp・runtime_platform.c・cmd/cm/backend/run.cpp + libs/std/env | 実装済み（ホストOS環境のmainのみi32 main(i32, ptr)化しプロローグでcm_args_init発行。wasm/UEFI/ベアメタルは従来シグネチャ維持。jitはcm run file.cm -- args...のargv[0]=入力ファイルで受け渡し、#[test]等の非mainエントリは無引数呼び出しのままでargs()は空。テスト: tests/common/env/args_test） |
| 第4段 | 通し検証: 上記APIだけで書いた小さなCLIツール（後述のセルフホスト素振り）をexamplesに追加しCIで実行 | examples/ + tests | 実装済み（examples/07_selfhost_drill: args解析→read_bytes→lines/splitトークン集計→push_u32_le+write_bytes→process::run。scripts/ci/check_examples.shでjit（--渡し）とnative（直接引数渡し）の両方を実行し成果物一致まで検証） |

第1段・第2段・第3段は独立にマージ可能で、依存は第4段のみが全段に依存する。

### 第4段の通し検証（セルフホスト素振り）

コンパイラの縮図となる`examples/09_selfhost_drill/`（仮）を追加し、native/jit両方でCIに載せる。

1. `env::args()`で入力パスと`-o`を受け取る
2. `fs::read_bytes`でソースを読み、`strings::lines`/`split`で行分割してトークン数を数える
3. `bytes::push_u32_le`でヘッダを組み立てた成果物を`fs::write_bytes`で書く
4. `process::run("cc ...")`で外部コマンドを呼び、終了コードを検証する

これが通れば「コンパイラの形をしたプログラム」に必要なOS連携が一巡していることの実証になる。

## テスト計画

- unit: ランタイムシム（cm_dir_*・cm_file_read_bytes/write_bytes・cm_args_init/cm_arg_*・cm_current_exe）のC関数単体テストを追加する
- regression: mainシグネチャ変更のMIR→LLVM署名検証（argc/argv受け取りと`cm_args_init`呼び出しの発行）を追加する
- integration: `tests/common/`へ`env/`・`process/`・`path/`・`bytes/`を新設し、`fs/`・`strings/`相当へバイナリI/O・split系ケースを追加する。native/jit両スイート（`make test-llvm`系）で実行し、期待出力を検証する。argvは`--`渡しのjitケースとコンパイル済みバイナリへの直接引数渡しのnativeケースの両方を置く
- 埋め込みNULを含む`utiny[]`のwrite→read往復一致、`read_dir`の順序決定性（ソート）、`split`の空要素・空セパレータ境界を必須ケースとする
- backend_support_matrix.mdへstd::env/std::process/std::fs拡張の対応状況（native/jit ✅、js/wasm ❌）を追記する

## 対象外（本バージョンでやらないこと）

- CmコンパイラそのもののCm実装（レクサ移植以降の全て）。1.0以降に別設計文書で扱う
- LLVM C APIのFFIバインディング。セルフホスト本体の設計判断（LLVMバインド vs 自前コード生成）に属するため先送りする
- cm_runtime.o（Cシム約5,700行）のCm書き直し。セルフホスト後も当面Cのまま残す方針
- js/wasmバックエンドへのOS連携API提供（wasmはWASI対応の検討自体を将来課題とする）
- Windows対応のパス・プロセスAPI

## セルフホスト本体の見通し（参考、1.0以降）

本バージョンの整備が済めば、セルフホストの残る論点は言語機能ではなくアーキテクチャ選択になる。

- 現行のnative/jitバックエンドはLLVM前提であり、nativeはLLVMでオブジェクトを直接出力して`clang`でリンク、jitはORC/LLJIT（実装は約349行）で完結している
- 第1候補はLLVM C APIをFFIバインドする構成（既存構成の忠実な移植で、jitもLLJIT C APIで再現できる）。自前の命令エンコーダ・Mach-O/ELFライタ・mmap/mprotect JITへの置き換えは、セルフホスト成立後の次フェーズに切り離す
- 移植順はレクサ→パーサ→AST/HIR→型検査→MIR→コード生成の順に、C++実装と出力を突き合わせながら段階的に進める（規模感: syntax約9,600行・types約8,500行・MIR約31,000行・codegen/llvm約30,700行）
