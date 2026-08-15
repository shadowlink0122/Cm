---
title: std::io
---

# std::io — 入出力

Cmの標準入出力モジュール。コンソールI/O、ファイルI/O、ストリーム抽象化を提供します。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## コンソール出力

```cm
import std::io::println;
import std::io::print;
import std::io::eprintln;

int main() {
    println("Hello, World!");       // 改行付き出力
    print("no newline");            // 改行なし出力
    eprintln("error message");     // 標準エラー出力
    return 0;
}
```

### 文字列補間

```cm
int x = 42;
string name = "Cm";
println("x = {x}");           // x = 42
println("Hello, {name}!");    // Hello, Cm!
```

---

## コンソール入力

`std::io`ファサード経由で入力APIを選択importできます（再export解決に対応。プロンプトは`print`で別途表示します）:

```cm
import std::io::{print, input, input_int, input_double, input_bool};

print("名前: ");
string name = input();          // 文字列入力
print("年齢: ");
int age = input_int();          // 整数入力（不正入力は0）
print("身長: ");
double height = input_double(); // 浮動小数点入力
print("OK? ");
bool ok = input_bool();         // 真偽値入力
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `input()` | `string` | 文字列入力 |
| `input_int()` | `int` | 整数入力 |
| `input_long()` | `long` | long入力 |
| `input_double()` | `double` | 浮動小数点入力 |
| `input_bool()` | `bool` | 真偽値入力 |
| `input_string()` | `string` | 文字列入力 |
| `parse_int(s)` / `parse_long(s)` / `parse_double(s)` / `parse_bool(s)` | `Option<T>` | 文字列のパース（失敗は`None`） |

---

## ファイル I/O（std::fs）

**v0.16.0以降**: ファイル操作は `std::fs` モジュールで提供されます（Native/JITバックエンド。JS/WASMは未対応）。

```cm
import std::io::println;
import std::fs::{write_file, read_file, append_file, exists, remove, size};

// 基本API（bool/値返却）
write_file("output.txt", "Hello, File!");
string content = read_file("output.txt");
append_file("output.txt", "\nmore");
bool ok = exists("output.txt");
long bytes = size("output.txt");
remove("output.txt");
```

### Result API（推奨・Rust準拠）

エラーを `Result` で受け取れます。`?` 演算子でそのまま伝播できます。

```cm
import std::io::println;
import std::fs::{read_to_string, write_all, remove_file};

Result<string, string> load_config(string path) {
    string content = read_to_string(path)?;   // 存在しなければErrを伝播
    return Result::Ok(content);
}

int main() {
    match (load_config("config.txt")) {
        Result::Ok(c) => println("loaded: {c}"),
        Result::Err(e) => println("error: {e}"),
    }
    return 0;
}
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `read_file(path)` | `string` | ファイル全体を読み込み（失敗時は空文字列） |
| `read_lines(path)` | `string[]` | 行単位で読み込み（改行区切り・失敗時は空スライス。v0.17.2） |
| `write_file(path, content)` | `bool` | 上書き書き込み |
| `append_file(path, content)` | `bool` | 追記 |
| `exists(path)` | `bool` | 存在確認 |
| `remove(path)` | `bool` | 削除 |
| `size(path)` | `long` | サイズ（バイト。失敗時-1） |
| `read_to_string(path)` | `Result<string, string>` | 読み込み（Rustのfs::read_to_string相当） |
| `write_all(path, content)` | `Result<bool, string>` | 上書き書き込み |
| `append_all(path, content)` | `Result<bool, string>` | 追記 |
| `remove_file(path)` | `Result<bool, string>` | 削除（Rustのfs::remove_file相当） |

---

## ストリーム

| モジュール | 説明 |
|-----------|------|
| `Stdin` / `stdin()` | 標準入力ストリーム |
| `Stdout` / `stdout()` | 標準出力ストリーム |
| `Stderr` / `stderr()` | 標準エラーストリーム |
| `BufferedReader` | バッファ付きReader |
| `BufferedWriter` | バッファ付きWriter |

## インターフェース

| 名前 | 説明 |
|------|------|
| `Reader` | 読み取りインターフェース |
| `Writer` | 書き込みインターフェース |
| `Seek` / `SeekFrom` | シーク操作 |

## エラー型

| 名前 | 説明 |
|------|------|
| `IoResult` | I/O操作の結果型 |
| `IoError` | エラー情報 |
| `IoErrorKind` | エラー種別 |

---

**関連:** [メモリ管理](mem.html) · [数学関数](math.html) · [コアユーティリティ](core-utils.html)

---

<!-- nav -->
← 前: [Cm 標準ライブラリ (Native向け)](index.html) ｜ [目次](index.html) ｜ 次: [std::mem — メモリ管理](mem.html) →
