---
title: OS連携 (env / process / path / bytes)
---

# OS連携モジュール — std::env / std::process / std::path / std::bytes

コンパイラのようなCLIツールを書くためのOS連携APIです（セルフホスト準備・v0.17.0）。

> **対応バックエンド:** env / process は Native・JIT のみ。path / bytes / strings::split は全バックエンド共通（純Cm実装）

名前空間形式（`env::get(...)`）は未対応のため、選択的importとエイリアスを使います。

## std::env — 環境変数

```cm
import std::io::println;
import std::env::{get as env_get, set as env_set};

int main() {
    env_set("MY_VAR", "hello");
    match (env_get("MY_VAR")) {
        Option::Some(v) => println(v),
        Option::None => println("(unset)"),
    }
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `Option<string> get(string name)` | 環境変数を取得（未設定はNone） |
| `bool set(string name, string value)` | 環境変数を設定（上書き） |

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
