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

```cm
import std::io::{input, input_int, input_double, input_bool};

string name = input("名前: ");        // 文字列入力
int age = input_int("年齢: ");        // 整数入力
double height = input_double("身長: "); // 浮動小数点入力
bool ok = input_bool("OK? ");          // 真偽値入力
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `input(prompt)` | `string` | 文字列入力 |
| `input_int(prompt)` | `int` | 整数入力 |
| `input_long(prompt)` | `long` | long入力 |
| `input_double(prompt)` | `double` | 浮動小数点入力 |
| `input_bool(prompt)` | `bool` | 真偽値入力 |
| `input_string(prompt)` | `string` | 文字列入力 |

---

## ファイル I/O

```cm
import std::io::{read_file, write_file};

// ファイル書き込み
write_file("output.txt", "Hello, File!");

// ファイル読み込み
string content = read_file("output.txt");
println(content);
```

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
