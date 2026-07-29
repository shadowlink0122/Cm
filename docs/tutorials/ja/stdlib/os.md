---
title: OS連携 (env / process / path / bytes)
---

# OS連携モジュール — std::env / std::process / std::path / std::bytes

コンパイラのようなCLIツールを書くためのOS連携APIです（セルフホスト準備・v0.17.0）。

> **対応バックエンド:** env / process は Native・JIT のみ。path / bytes / strings::split は全バックエンド共通（純Cm実装）

名前空間形式（`env::get(...)`）は未対応のため、選択的importとエイリアスを使います。

## std::env — 環境変数・コマンドライン引数・実行パス

```cm
import std::io::println;
import std::env::{get as env_get, set as env_set, args, current_exe};

int main() {
    env_set("MY_VAR", "hello");
    match (env_get("MY_VAR")) {
        Option::Some(v) => println(v),
        Option::None => println("(unset)"),
    }

    // コマンドライン引数（先頭は実行ファイル名/スクリプトパス）
    const string[] argv = args();
    for (long i = 0; i < argv.len(); i++) {
        println("arg[{i}] = {argv[i]}");
    }

    // 自分自身の実行ファイルの絶対パス
    println(current_exe());
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `Option<string> get(string name)` | 環境変数を取得（未設定はNone） |
| `bool set(string name, string value)` | 環境変数を設定（上書き） |
| `string[] args()` | コマンドライン引数（先頭は実行ファイル名。未初期化環境では空） |
| `string current_exe()` | 実行ファイル自身の絶対パス（取得できなければ空文字列） |

JIT実行（`cm run`）では`--`より後ろがスクリプトへの引数になり、`args()`の先頭は入力ファイルパスです。

```bash
cm run tool.cm -- input.cm -o out    # args() = ["tool.cm", "input.cm", "-o", "out"]
./tool input.cm -o out               # ネイティブバイナリはOSのargv取得と同じ
```

## std::process — サブプロセス

```cm
import std::io::println;
import std::process::{run, output};

int main() {
    const int code = run("clang --version > /dev/null");  // 終了コード
    println(code);
    match (output("echo hi")) {                            // 標準出力を収集
        Result::Ok(text) => println(text.len()),
        Result::Err(e) => println(e),
    }
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `int run(string cmd)` | シェル経由で実行し終了コードを返す（起動失敗は-1） |
| `Result<string, string> output(string cmd)` | 標準出力を文字列で返す（起動失敗のみErr） |

## std::fs拡張 — ディレクトリ列挙・バイナリ安全I/O

```cm
import std::io::println;
import std::fs::{read_dir, read_bytes, write_bytes};

int main() {
    // エントリ名を名前昇順で列挙（"."と".."は除外）
    const string[] entries = read_dir("src");
    for (long i = 0; i < entries.len(); i++) {
        println(entries[i]);
    }

    // 埋め込みNUL・非UTF-8を含むデータも欠損しない読み書き
    match (read_bytes("input.o")) {
        Result::Ok(data) => {
            println("read {data.len()} bytes");
            write_bytes("copy.o", data);
        },
        Result::Err(e) => println(e),
    }
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `string[] read_dir(string path)` | ディレクトリ内のエントリ名（名前昇順、開けなければ空） |
| `Result<utiny[], string> read_bytes(string path)` | ファイル全体をバイト列で読む（存在しなければErr） |
| `bool write_bytes(string path, utiny[] data)` | バイト列を書き込む（長さ明示のため埋め込みNULで切れない） |

## std::path — パス操作（純Cm）

```cm
import std::path::{join, dirname, basename, extension, with_extension};

join("src", "main.cm");            // "src/main.cm"
dirname("src/main.cm");            // "src"
basename("src/main.cm");           // "main.cm"
extension("main.cm");              // "cm"
with_extension("main.cm", "o");    // "main.o"
```

区切りは`/`固定です（対応プラットフォームはmacOS/Linux）。

## std::bytes — エンディアン指定のバイト詰め（純Cm）

```cm
import std::bytes::{push_u32_le, read_u32_le};

utiny[] buf = [];
push_u32_le(buf, 0xFEEDFACF);          // リトルエンディアンで4バイト追記
const uint magic = read_u32_le(buf, 0);
```

`push_u16_le/u32_le/u64_le`とビッグエンディアン版（`*_be`）、対応する`read_*`を提供します。
64ビットのread/writeはJSバックエンドでは53bit精度制限のため非対応です。

## std::strings::split / lines — 文字列分割（純Cm）

```cm
import std::strings::{split, lines};

string[] parts = split("a,b,,c", ",");   // ["a", "b", "", "c"]（空要素保持）
string[] ls = lines("x\r\ny\n");         // ["x", "y"]（\r\n正規化・末尾空要素除去）
```

`split`のセパレータが空文字列の場合はコードポイント1文字ずつに分割します。

`from_bytes(utiny[])`はバイト列から文字列を構築します（v0.17.0）。埋め込みNUL（0x00）を含むバイト列も`byte_len()`・`substring()`・連結で正しく保持されます。

```cm
import std::strings::from_bytes;

utiny[] raw = [72, 105];          // "Hi"
string s = from_bytes(raw);
```
