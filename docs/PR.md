[English](PR.en.html)

# v0.13.0 Release - Cm言語コンパイラ

## 概要

v0.13.0は**std::io完全整理、Tagged Union/Match式強化、libc直接呼び出しアーキテクチャ**に焦点を当てた大規模アップデートです。標準ライブラリからC++ランタイム依存を排除し、純粋なCm実装を達成しました。

## 🎯 主要な新機能

### 1. std::io モジュール階層的再構成

標準I/Oモジュールを機能別にサブモジュール化しました。

```
std/io/
├── mod.cm             # エントリ（再エクスポート）
├── error.cm           # IoResult, IoError, IoErrorKind
├── traits.cm          # Reader, Writer, Seek インターフェース
├── console/           # コンソールI/O
│   ├── input.cm       # input(), input_int(), etc.
│   └── output.cm      # print, println, eprint, eprintln
├── stream/            # ストリームI/O
│   ├── stdin.cm       # Stdin構造体
│   ├── stdout.cm      # Stdout, Stderr構造体
│   └── buffered.cm    # BufferedReader, BufferedWriter
└── file/              # ファイルI/O
    ├── mod.cm         # 再エクスポート
    └── io.cm          # read_file, write_file
```

#### 使用例

```cm
import std::io::println;
import std::io::file::{read_file, write_file};

int main() {
    // ファイル書き込み
    write_file("test.txt", "Hello, Cm!");
    
    // ファイル読み込み
    string content = read_file("test.txt");
    println(content);
    
    return 0;
}
```

### 2. libc直接呼び出しアーキテクチャ

`use libc`構文でlibcの関数を直接呼び出すことが可能になりました。C++ランタイム依存を完全に排除しました。

```cm
use libc {
    int write(int fd, void* buf, long count);
    void* malloc(long size);
    void free(void* ptr);
}
```

### 3. Tagged Union (Enum) の強化

関連データを持つenumの実装を強化しました。

```cm
enum Result<T, E> {
    Ok(T),
    Err(E)
}

fn divide(a: int, b: int) -> Result<int, string> {
    if (b == 0) {
        return Result::Err("Division by zero");
    }
    return Result::Ok(a / b);
}
```

### 4. Match式の改善

ガード式とより柔軟なパターンマッチングをサポート。

```cm
match value {
    x if x > 0 => println("positive"),
    0 => println("zero"),
    _ => println("negative"),
}
```

### 5. std::fs 削除

`std::fs`モジュールを削除し、`std::io::file`に統合しました。

## 🔧 改善点

### テストインフラストラクチャ

- 開発中テストは`make tip`または個別実行を推奨
- 9件のスキップテストにexpectファイルを追加

### 開発ルール追加

- `.agent/rules/testing.md`: 開発中テストルール
- `.agent/workflows/feature-implementation.md`: 検証フェーズ手順

## 📁 変更ファイル一覧（主要）

### std::io再構成
| ファイル | 変更内容 |
|---------|---------|
| `std/io/mod.cm` | 再エクスポート構成 |
| `std/io/console/*.cm` | コンソールI/O |
| `std/io/stream/*.cm` | ストリームI/O |
| `std/io/file/*.cm` | ファイルI/O |
| `std/io/error.cm` | エラー型定義 |
| `std/io/traits.cm` | I/Oインターフェース |

### コンパイラ
| ファイル | 変更内容 |
|---------|---------|
| `src/frontend/types/checking/decl.cpp` | output::パス対応削除 |
| `src/hir/lowering/decl.cpp` | 同上 |

### 削除ファイル
| ファイル | 理由 |
|---------|------|
| `std/fs/` | std::io::fileに統合 |
| `std/io/file/file.cm` | io.cmに簡素化 |

## 🧪 テスト状況

| カテゴリ | 通過 | 失敗 | スキップ |
|---------|-----|------|---------| 
| インタプリタ | 321 | 0 | 15 |

## 📊 統計

- **追加テスト**: 9件（expect追加）
- **削除モジュール**: std::fs

## ⚠️ 既知の問題と対策

### モジュール内const定数がMIRで0に評価される問題（対策済み）

**問題**: モジュール内で定義した`const`定数が、MIR生成時に0として評価される

**例**:
```cm
// std/io/file.cm
const int SEEK_END = 2;
lseek(fd, 0, SEEK_END);  // MIRでは lseek(fd, 0, 0) になる
```

**対策**: file.cm, output.cmでリテラル値を直接使用するように修正済み

### const定数のビット演算問題

**問題**: `O_WRONLY | O_CREAT | O_TRUNC`のようなconst定数のビット演算が0になる

**回避策**: リテラル値を直接使用（例: `0x0601`）

### Octalリテラル非対応

**問題**: `0644`がCのようにoctal(8進数)として解釈されず、decimal(10進数)として解釈される

**回避策**: 16進数を使用（例: `0644 octal` → `0x1A4`）

## ✅ テスト結果

```
Total:   336
Passed:  322
Failed:  0
Skipped: 14
```

## 🚀 今後の予定

- モジュール内const定数評価問題の根本修正
- const定数ビット演算の修正
- Octalリテラルサポート
- std::netモジュール実装
- async/await対応

---

**リリース日**: 2026年2月3日
**バージョン**: v0.13.0
**コードネーム**: "Zero-libc I/O"