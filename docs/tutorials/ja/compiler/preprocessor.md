---
title: プリプロセッサ
parent: Compiler
nav_order: 7
---

[English](../../en/compiler/preprocessor.html)

# コンパイラ編 - プリプロセッサ（条件付きコンパイル）

**難易度:** 🟡 中級  
**所要時間:** 15分

## 📚 この章で学ぶこと

- 条件付きコンパイルの基本（`#ifdef` / `#ifndef` / `#else` / `#end`）
- 組み込み定数一覧（アーキテクチャ・OS・コンパイラ情報）
- マルチプラットフォーム対応の実践例

---

## 条件付きコンパイルとは

Cmのプリプロセッサは、**ソースコードをコンパイル前にフィルタリング**する仕組みです。
ターゲットのアーキテクチャやOSに応じてコードを切り替えることができます。

```cm
import std::io::println;

int main() {
    #ifdef __x86_64__
        println("x86_64アーキテクチャで実行中");
    #end

    #ifdef __arm64__
        println("ARM64アーキテクチャで実行中");
    #end

    return 0;
}
```

---

## ディレクティブ一覧

| ディレクティブ | 説明 |
|-------------|------|
| `#ifdef SYMBOL` | シンボルが定義されていれば、以下のコードを有効化 |
| `#ifndef SYMBOL` | シンボルが定義されていなければ、以下のコードを有効化 |
| `#else` | 直前の `#ifdef` / `#ifndef` の条件が偽の場合に有効化 |
| `#end` | 条件ブロックを終了 |

> ⚠️ C/C++の `#endif` ではなく `#end` であることに注意してください。

---

## 基本的な使い方

### ifdef / end

```cm
#ifdef __macos__
    // macOSでのみ実行されるコード
    println("macOS detected");
#end
```

### ifdef / else / end

```cm
#ifdef __x86_64__
    println("x86_64アーキテクチャ");
#else
    println("その他のアーキテクチャ");
#end
```

### ifndef

```cm
#ifndef __DEBUG__
    // リリースビルドでのみ実行
    println("Release mode");
#end
```

### ネスト

```cm
#ifdef __macos__
    #ifdef __arm64__
        println("Apple Silicon Mac");
    #else
        println("Intel Mac");
    #end
#end
```

---

## 組み込み定数一覧

### アーキテクチャ

| 定数 | 説明 |
|-----|------|
| `__x86_64__` | x86_64（64ビットIntel/AMD） |
| `__x86__` | x86（32ビット互換含む） |
| `__arm64__` | ARM64（Apple Silicon等） |
| `__aarch64__` | AArch64（`__arm64__`と同義） |
| `__i386__` | i386（32ビットx86） |
| `__riscv__` | RISC-V |

### OS

| 定数 | 説明 |
|-----|------|
| `__macos__` | macOS |
| `__apple__` | Apple系OS |
| `__linux__` | Linux |
| `__windows__` | Windows |
| `__freebsd__` | FreeBSD |
| `__unix__` | Unix系（macOS/Linux/FreeBSD） |

### コンパイラ・ビルド情報

| 定数 | 説明 |
|-----|------|
| `__CM__` | Cmコンパイラ（常に定義） |
| `__DEBUG__` | デバッグビルド時に定義 |
| `__64BIT__` | 64ビット環境 |
| `__32BIT__` | 32ビット環境 |

---

## 実践例

### マルチアーキテクチャ対応のインラインアセンブリ

```cm
import std::io::println;

int main() {
    int x = 50;

    #ifdef __x86_64__
        // x86_64: AT&T構文
        __asm__("addl $$10, ${+r:x}");
    #else
        // ARM64: AArch64構文
        __asm__("add ${+r:x}, ${+r:x}, #10");
    #end

    println("Result: {x}");  // 60
    return 0;
}
```

### OS固有の処理

```cm
int main() {
    #ifdef __macos__
        println("macOS版の処理");
    #end

    #ifdef __linux__
        println("Linux版の処理");
    #end

    #ifdef __windows__
        println("Windows版の処理");
    #end

    return 0;
}
```

### デバッグ専用コード

```cm
import std::io::println;

void process_data(int value) {
    #ifdef __DEBUG__
        println("デバッグ: process_data({value})");
    #end

    // ... 本処理 ...
}
```

---

## 処理の仕組み

プリプロセッサは**レキサーの前**にソースコードレベルで動作します：

```
ソースコード → ImportPreprocessor → ConditionalPreprocessor → Lexer → Parser
```

- ディレクティブ行は**空行に置換**されるため、行番号が保持されます
- エラーメッセージの行番号はソースファイルと一致します

---

## 注意事項

- `#ifdef` / `#end` は必ず**対応**させてください。閉じ忘れるとエラーになります
- ディレクティブは**行頭**に記述してください（先頭の空白は許容）
- 組み込み定数は**ホストコンパイラの環境**を検出します

---

## 次のステップ

✅ 条件付きコンパイルの基本を理解した  
✅ 組み込み定数の使い方がわかった  
✅ マルチプラットフォーム対応の方法を学んだ  
⏭️ 次は [最適化](optimization.html) を学びましょう

---

**前の章:** [Wasm](wasm.html)  
**次の章:** [最適化](optimization.html)

## 関連項目

- [mustキーワード](../advanced/must.html) - デッドコード削除の防止
- [マクロ](../advanced/macros.html) - コンパイル時定数

---

**最終更新:** 2026-02-08
