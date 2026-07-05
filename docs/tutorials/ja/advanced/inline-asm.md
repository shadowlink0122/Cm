---
title: インラインアセンブリ
parent: Advanced
---

# インラインアセンブリ

**学習目標:** `__asm__` を使ってCm内部からアセンブリ命令を実行する方法を学びます。  
**所要時間:** 20分  
**難易度:** 🔴 上級

> **対応バックエンド:** Native (LLVM) のみ

---

## 基本構文

```cm
__asm__("命令");
```

### NOP（何もしない命令）

```cm
__asm__("nop");
```

### コンパイラバリア

```cm
// メモリ操作の並び替えを防止
__asm__("" ::: "memory");
```

---

## オペランド制約

Cm独自の構文で変数とレジスタを紐付けます。

### 読み書きオペランド `${+r:変数名}`

```cm
int x = 90;
__asm__("addl $$10, ${+r:x}");  // x86_64: x += 10 → x = 100
```

`+r` は「読み書き可能なレジスタ」を意味します。

### 複数オペランドの使用

```cm
int val = 12345;
__asm__("xorl ${+r:val}, ${+r:val}");  // x86_64: val = 0
```

---

## アーキテクチャ分岐

`#ifdef` と組み合わせて、プラットフォーム固有のコードを記述します。

```cm
int result = 10;

#ifdef __x86_64__
    // x86_64: add命令
    __asm__("addl $$5, ${+r:result}");
#endif

#ifdef __ARM64__
    // ARM64: add命令
    __asm__("add ${+r:result}, ${+r:result}, #5");
#endif

println(result);  // 15
```

---

## 実用例

### 算術演算 (x86_64)

```cm
int val = 10;
__asm__("addl $$5, ${+r:val}");    // 加算: 15
__asm__("sall $$1, ${+r:val}");    // 左シフト: 30
__asm__("subl $$10, ${+r:val}");   // 減算: 20
__asm__("negl ${+r:val}");         // 符号反転: -20
```

### 算術演算 (ARM64)

```cm
int val = 10;
__asm__("add ${+r:val}, ${+r:val}, #5");    // 加算: 15
__asm__("lsl ${+r:val}, ${+r:val}, #1");    // 左シフト: 30
__asm__("sub ${+r:val}, ${+r:val}, #10");   // 減算: 20
__asm__("neg ${+r:val}, ${+r:val}");        // 符号反転: -20
```

### ビット演算

```cm
int bits = 0xF0;

#ifdef __x86_64__
    __asm__("orl $$0x0F, ${+r:bits}");     // OR: 0xFF (255)
    __asm__("shll $$4, ${+r:bits}");       // 左シフト
#endif
#ifdef __ARM64__
    __asm__("orr ${+r:bits}, ${+r:bits}, #0x0F");  // OR: 0xFF
    __asm__("lsl ${+r:bits}, ${+r:bits}, #4");      // 左シフト
#endif
```

### メモリバリア

```cm
#ifdef __x86_64__
    __asm__("mfence" ::: "memory");
#endif
#ifdef __ARM64__
    __asm__("dmb sy" ::: "memory");
#endif
```

---

## 即値の記法

| アーキテクチャ | 即値表記 | 例 |
|--------------|---------|-----|
| x86_64 | `$$値` | `addl $$10, ${+r:x}` |
| ARM64 | `#値` | `add ${+r:x}, ${+r:x}, #10` |

---

## 注意事項

- インラインアセンブリは **LLVM バックエンド** でのみ使用可能です
- `#ifdef` で **必ずアーキテクチャを分岐** してください
- 不正な命令はLLVMのアセンブラエラーになります
- パフォーマンスクリティカルな箇所でのみ使用を推奨します

---

## 次のステップ

- [extern宣言](extern.html) — C/C++関数の呼び出し
- [FFI](ffi.html) — use libc / 構造体の受け渡し
- [プリプロセッサ](../compiler/common/preprocessor.html) — `#ifdef`/`#ifndef`/`#end`

---

**最終更新:** 2026-02-08
