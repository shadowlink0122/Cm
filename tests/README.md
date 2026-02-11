# Cm言語テストシステム

## 概要

Cm言語の統一テストシステムです。同一のテストプログラム（`.cm`ファイル）を使用して、JIT、LLVM Native、WebAssembly、JavaScriptなど複数のバックエンドの動作を検証します。

## ディレクトリ構造

```
tests/
├── programs/              # テストプログラム
│   ├── common/            # 全バックエンド共通テスト
│   │   ├── basic/         # 基本機能
│   │   ├── types/         # 型システム
│   │   ├── control_flow/  # 制御構造
│   │   ├── generics/      # ジェネリクス
│   │   ├── collections/   # コレクション
│   │   └── ...            # その他50+カテゴリ
│   ├── llvm/              # LLVM/JIT固有テスト
│   │   ├── asm/           # インラインアセンブリ
│   │   └── ffi/           # FFI（外部関数呼び出し）
│   ├── jit/               # JIT固有テスト
│   ├── wasm/              # WebAssembly固有テスト
│   ├── js/                # JavaScript固有テスト
│   │   └── js_specific/
│   ├── bm/                # ベアメタル固有テスト
│   │   └── baremetal/
│   └── uefi/              # UEFI固有テスト
│       └── uefi_compile/
│
├── bench_marks/           # ベンチマーク
├── linter/                # リンターテスト
├── unit/                  # C++ユニットテスト
├── unified_test_runner.sh # 統一テストランナー
└── README.md              # このファイル
```

### バックエンド別テストディレクトリの対応

| バックエンド | 実行されるディレクトリ |
|------------|----------------------|
| `jit` | `common/`, `llvm/`, `jit/` |
| `llvm` | `common/`, `llvm/` |
| `interpreter` | `common/` |
| `llvm-wasm` | `common/`, `wasm/` |
| `js` | `common/`, `js/` |
| `llvm-uefi` | `uefi/` |
| `llvm-baremetal` | `bm/` |

## クイックスタート

```bash
# JITテスト（最も頻繁に使用）
make tip      # JIT O3 パラレル

# LLVM ネイティブテスト
make tlp      # LLVM O3 パラレル

# WebAssembly テスト
make tlwp     # WASM O3 パラレル

# JavaScript テスト
make tjp      # JS O3 パラレル

# UEFI / ベアメタル テスト
make test-uefi
make test-baremetal
```

## テストプログラムの形式

### `.cm` ファイル（ソースコード）

```cm
// tests/programs/common/basic/example.cm
int main() {
    int x = 42;
    println(x);
    return 0;
}
```

### `.expect` ファイル（期待される出力）

```
42
```

### 特殊ファイル

- `.skip` - テストをスキップ（空: 全バックエンド、内容あり: 指定バックエンドのみ）
- `.timeout` - テスト別タイムアウト秒数
- `.error` - エラーが期待されるテスト
- `.expect.<backend>` - バックエンド固有の期待出力

## 新しいテストの追加方法

1. テストプログラムを作成:
   ```bash
   vim tests/programs/common/basic/new_test.cm
   ```

2. 期待される出力を定義:
   ```bash
   vim tests/programs/common/basic/new_test.expect
   ```

3. テストを実行:
   ```bash
   make tip
   ```