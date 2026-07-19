---
title: サニタイザ
parent: Compiler
---

[English](../../../en/compiler/common/sanitizer.html)

# サニタイザ（実行時メモリ検査）

**学習目標:** `--sanitize` オプションを使って境界外アクセスなどのメモリバグを実行時に検出する方法を学びます。
**所要時間:** 15分
**難易度:** 🟡 中級

---

## 概要

サニタイザは、コンパイル時にプログラムへ検査コードを挿入（計装）し、実行時にメモリバグを検出する仕組みです。
C/C++/Rustエコシステムで標準的なLLVMのサニタイザ基盤を利用しており、`cm compile` / `cm run` に `--sanitize=<種類>` を付けるだけで有効になります。

```bash
cm compile --sanitize=bounds -O0 main.cm -o main    # 境界チェック付きネイティブビルド
cm compile --sanitize=address -O0 main.cm -o main   # AddressSanitizer付きビルド
cm run --sanitize=bounds -O0 main.cm                # JIT実行に境界チェックを計装
cm compile --target=wasm --sanitize=bounds -O0 main.cm -o main.wasm
```

---

## サポートされるサニタイザ

| 種類 | 検出内容 | native | wasm | jit（cm run） |
|------|---------|--------|------|---------------|
| `bounds` | コンパイル時にサイズが確定するオブジェクトへの境界外アクセス | ○ | ○ | ○ |
| `address` | ヒープ/スタック/グローバルの境界外アクセス・use-after-free・二重解放 | ○ | × | × |

複数指定はカンマ区切りです: `--sanitize=address,bounds`

---

## bounds: 境界チェック

`bounds` はLLVMの `BoundsCheckingPass`（clangの `-fsanitize=local-bounds` 相当）で、固定長配列などサイズが静的に分かるメモリアクセスに検査を挿入します。
違反を検出するとtrap命令で即座に停止します（終了コードは非0。macOSでは133、wasmtimeでは134など環境依存）。
ランタイムライブラリが不要なため、native・wasm・JITのすべての実行系で使えます。

```cm
int main() {
    int[4] arr;
    int n = 4;
    for (int i = 0; i <= n; i++) {  // i == 4 で境界外書き込み
        arr[i] = i;
    }
    println("done {arr[0]}");
    return 0;
}
```

```bash
$ cm compile --sanitize=bounds -O0 oob.cm -o oob
$ ./oob
zsh: trap: illegal hardware instruction  ./oob   # 境界外書き込みで即停止

$ cm compile --target=wasm --sanitize=bounds -O0 oob.cm -o oob.wasm
$ wasmtime oob.wasm
Error: wasm trap: wasm `unreachable` instruction executed
```

検出できるのはLLVMがオブジェクトサイズを静的に決定できるアクセスに限られます（部分的カバレッジ）。ヒープや関数境界をまたぐアクセスの検査には `address` を使ってください。

---

## address: AddressSanitizer

`address` はLLVMの `AddressSanitizerPass` でメモリアクセスを計装し、ASanランタイムをリンクします。
境界外アクセスに加えてuse-after-free・二重解放も検出でき、違反時には詳細なレポートが出力されます。

```bash
$ cm compile --sanitize=address -O0 oob.cm -o oob
$ ./oob
==12345==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x...
```

ASanランタイムが必要なため `cm compile --target=native` 専用です。
wasm（wasm32-wasi向けランタイムが存在しない）とJIT（cmプロセス内実行のためランタイムを後付けできない）では使えません。JITで試したい場合は `cm compile --sanitize=address --run main.cm` を使ってください。

---

## 最適化レベルとの関係

サニタイザは**最適化の後**に計装されるため、高い最適化レベルでは境界外アクセス自体が（未定義動作として）最適化で消えることがあり、検出できない場合があります。
バグ調査では `-O0` または `-O1` との併用を推奨します（clang等の一般的なサニタイザ運用と同じ指針です）。

---

## エラーメッセージ

非対応の組み合わせや未知の値は明確なエラーになります:

```bash
$ cm compile --sanitize=foo main.cm
error: unknown sanitizer 'foo'
valid sanitizers: address (native only), bounds (native/wasm/jit)

$ cm run --sanitize=address main.cm
error: sanitizer 'address' is not supported on target 'jit'
```

---

<!-- nav -->
← 前: [MIR最適化パス](optimization.html) ｜ [目次](../index.html) ｜ 次: [コンパイラ編 - LLVMバックエンド](../native/index.html) →
