[English](wasm_execution.en.html)

# WebAssemblyファイルの実行方法

このドキュメントでは、Cm言語でコンパイルしたWebAssembly（WASM）ファイルの実行方法について説明します。

## WASMファイルのコンパイル

```bash
# デフォルトのファイル名（a.wasm）でコンパイル
./cm compile --target=wasm main.cm

# 出力ファイル名を指定
./cm compile --target=wasm main.cm -o myapp.wasm
```

## 実行方法

### 方法1: Node.js + WASI（推奨）

Node.jsのWASIサポートを使用する方法です。最も簡単で、多くの環境で動作します。

#### 必要なもの
- Node.js v12以降

#### 実行手順

1. WASIラッパースクリプトを作成：

```javascript
// run_wasm.js
const fs = require('fs');
const { WASI } = require('wasi');

const wasi = new WASI({
    version: 'preview1',
    args: process.argv.slice(2),
    env: process.env,
});

const wasmBuffer = fs.readFileSync(process.argv[2]);

(async () => {
    const wasm = await WebAssembly.compile(wasmBuffer);
    const instance = await WebAssembly.instantiate(wasm, {
        wasi_snapshot_preview1: wasi.wasiImport
    });
    wasi.start(instance);
})().catch(err => {
    console.error(err);
    process.exit(1);
});
```

2. WASMファイルを実行：

```bash
node --experimental-wasi-unstable-preview1 run_wasm.js myapp.wasm
```

### 方法2: Wasmtime（高性能）

Wasmtimeは高性能なWebAssemblyランタイムです。

#### インストール

```bash
# macOS
brew install wasmtime

# Linux
curl https://wasmtime.dev/install.sh -sSf | bash

# Windows
# https://github.com/bytecodealliance/wasmtime/releases からダウンロード
```

#### 実行

```bash
wasmtime myapp.wasm
```

### 方法3: Wasmer（多機能）

Wasmerは多機能なWebAssemblyランタイムです。

#### インストール

```bash
# macOS/Linux
curl https://get.wasmer.io -sSfL | sh

# Windows
# https://github.com/wasmerio/wasmer/releases からダウンロード
```

#### 実行

```bash
wasmer run myapp.wasm
```

### 方法4: ブラウザ

WebブラウザでWASMファイルを実行することも可能です。

```html
<!DOCTYPE html>
<html>
<head>
    <title>Cm WASM App</title>
</head>
<body>
    <div id="output"></div>
    <script>
        // 出力をキャプチャするための設定
        const output = document.getElementById('output');
        const decoder = new TextDecoder();

        // WASIの簡易実装
        const wasiImports = {
            wasi_snapshot_preview1: {
                fd_write: (fd, iovs_ptr, iovs_len, nwritten_ptr) => {
                    // 標準出力の処理
                    if (fd === 1) {
                        // メモリから文字列を読み取って表示
                        // （詳細な実装は省略）
                        return 0;
                    }
                    return -1;
                },
                proc_exit: (code) => {
                    console.log('Exit code:', code);
                }
            }
        };

        // WASMファイルを読み込んで実行
        fetch('myapp.wasm')
            .then(response => response.arrayBuffer())
            .then(bytes => WebAssembly.instantiate(bytes, wasiImports))
            .then(result => {
                // _start関数を呼び出す
                if (result.instance.exports._start) {
                    result.instance.exports._start();
                }
            })
            .catch(console.error);
    </script>
</body>
</html>
```

## トラブルシューティング

### エラー: "WASI is an experimental feature"
Node.jsでWASIを使用する際の警告です。`--experimental-wasi-unstable-preview1`フラグが必要です。

### エラー: "no such file or directory"
WASMファイルへのパスが正しいか確認してください。

### エラー: "unreachable executed"
プログラムにバグがある可能性があります。デバッグ出力を追加して確認してください。

## サンプルプログラム

### Hello World

```cm
// hello.cm
import std::io::println;

int main() {
    println("Hello from WebAssembly!");
    return 0;
}
```

コンパイルと実行：

```bash
# コンパイル
./cm compile --target=wasm hello.cm -o hello.wasm

# 実行（wasmtime）
wasmtime hello.wasm

# 実行（Node.js）
node --experimental-wasi-unstable-preview1 run_wasm.js hello.wasm
```

### 計算プログラム

```cm
// calc.cm
import std::io::println;

int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    println("Fibonacci(10) = {}", fibonacci(10));
    return 0;
}
```

## パフォーマンス比較

| ランタイム | 起動時間 | 実行速度 | メモリ使用量 |
|-----------|---------|---------|------------|
| Wasmtime | 速い | 最速 | 少ない |
| Wasmer | 普通 | 速い | 普通 |
| Node.js + WASI | 遅い | 普通 | 多い |
| ブラウザ | 最遅 | 速い | 多い |

## まとめ

- **開発時**: Node.js + WASIが手軽
- **本番環境**: Wasmtimeが高性能
- **Web配信**: ブラウザで実行
- **組み込み**: Wasmerの組み込み機能を活用

用途に応じて適切なランタイムを選択してください。