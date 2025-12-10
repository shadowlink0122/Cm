# LLVM アーキテクチャ設計

## 全体構造

```
    Cm Source
        ↓
    Lexer/Parser
        ↓
      AST
        ↓
      HIR ← 既存共通部分
        ↓
    HIR → MIR Lowering
        ↓
      MIR (SSA形式)
        ↓
    MIR → LLVM IR ← 新規実装
        ↓
    LLVM Optimization
        ↓
    Target Generation
        ↓
   ┌────┼────┐
   ↓    ↓    ↓
Baremetal Native WASM
```

## コア/スタンダード分離設計

### 1. コア言語機能（no_std相当）

```cm
// core.cm - OS非依存の純粋な言語機能
#![no_std]

// 基本型
typedef u8 = unsigned char;
typedef u16 = unsigned short;
typedef u32 = unsigned int;
typedef u64 = unsigned long;
typedef i8 = signed char;
typedef i16 = signed short;
typedef i32 = signed int;
typedef i64 = signed long;
typedef f32 = float;
typedef f64 = double;
typedef usize = unsigned long;  // ポインタサイズ
typedef isize = signed long;

// メモリ操作（言語組み込み）
extern "intrinsic" {
    void* memcpy(void* dest, const void* src, usize n);
    void* memset(void* s, int c, usize n);
    int memcmp(const void* s1, const void* s2, usize n);
}

// アロケータインターフェース（実装は環境依存）
interface Allocator {
    void* alloc(usize size, usize align);
    void dealloc(void* ptr, usize size, usize align);
    void* realloc(void* ptr, usize old_size, usize new_size, usize align);
}

// パニックハンドラ（実装必須）
[[noreturn]] void panic(const char* msg);
```

### 2. 標準ライブラリ（std）

```cm
// std.cm - OS依存機能
#![requires_std]

// 標準出力（OSが必要）
namespace io {
    void print(const char* str);
    void println(const char* str);
    int getchar();
}

// ファイルシステム（OSが必要）
namespace fs {
    struct File;
    File* open(const char* path, const char* mode);
    void close(File* f);
    usize read(File* f, void* buf, usize size);
    usize write(File* f, const void* buf, usize size);
}

// デフォルトアロケータ（OS提供）
struct SystemAllocator : Allocator {
    void* alloc(usize size, usize align) {
        return aligned_alloc(align, size);  // POSIX
    }
    void dealloc(void* ptr, usize size, usize align) {
        free(ptr);
    }
}
```

### 3. ベアメタル実装

```cm
// baremetal.cm - OS無し環境
#![no_std]
#![no_main]

// カスタムアロケータ実装
struct BumpAllocator : Allocator {
    u8* heap_start;
    u8* heap_end;
    u8* current;

    void* alloc(usize size, usize align) {
        current = (u8*)((usize(current) + align - 1) & ~(align - 1));
        if (current + size > heap_end) {
            panic("Out of memory");
        }
        void* result = current;
        current += size;
        return result;
    }

    void dealloc(void* ptr, usize size, usize align) {
        // Bump allocatorは個別解放不可
    }
}

// パニック実装
[[noreturn]] void panic(const char* msg) {
    // UARTに出力（ハードウェア依存）
    volatile u32* uart = (u32*)0x10000000;
    while (*msg) {
        *uart = *msg++;
    }
    while (true) { }  // 停止
}

// エントリーポイント
extern "C" void _start() {
    // スタック設定、BSS初期化など
    main();
}
```

## ビルドターゲット設計

### 1. Baremetal Target

```toml
# target/baremetal-arm.toml
target = "arm-none-eabi"
features = ["no_std", "no_main", "panic_handler"]
link_script = "link.ld"
opt_level = "s"  # サイズ最適化

[llvm]
target_triple = "arm-none-eabi"
data_layout = "e-m:e-p:32:32"
features = "+thumb2"
relocation_model = "static"
code_model = "small"
```

### 2. Native Target

```toml
# target/native-x86_64.toml
target = "native"
features = ["std"]
link_libs = ["c", "m"]  # libc, libm
opt_level = "3"  # 速度最適化

[llvm]
target_triple = "x86_64-pc-linux-gnu"  # または自動検出
features = "+sse4.2,+avx2"
relocation_model = "pic"
code_model = "small"
```

### 3. WASM Target

```toml
# target/wasm32.toml
target = "wasm32-unknown"
features = ["no_std", "wasm_bindgen"]
opt_level = "z"  # 極小サイズ

[llvm]
target_triple = "wasm32-unknown-unknown"
features = "+simd128"
relocation_model = "static"
code_model = "default"
```

## LLVM MIR 変換設計

### MIR → LLVM IR マッピング

| MIR | LLVM IR | 備考 |
|-----|---------|------|
| `mir::Function` | `llvm::Function` | 関数定義 |
| `mir::BasicBlock` | `llvm::BasicBlock` | 基本ブロック |
| `mir::Local` | `llvm::AllocaInst` | ローカル変数 |
| `mir::Alloca` | `llvm::AllocaInst` | スタック割り当て |
| `mir::Load` | `llvm::LoadInst` | メモリ読み込み |
| `mir::Store` | `llvm::StoreInst` | メモリ書き込み |
| `mir::BinaryOp` | `llvm::BinaryOperator` | 二項演算 |
| `mir::Call` | `llvm::CallInst` | 関数呼び出し |
| `mir::Branch` | `llvm::BranchInst` | 条件分岐 |
| `mir::Jump` | `llvm::BranchInst` | 無条件ジャンプ |
| `mir::Return` | `llvm::ReturnInst` | リターン |
| `mir::Phi` | `llvm::PHINode` | PHIノード |

### 型システムマッピング

| Cm型 | LLVM型 | サイズ |
|------|--------|--------|
| `bool` | `i1` | 1 bit |
| `i8/u8` | `i8` | 8 bits |
| `i16/u16` | `i16` | 16 bits |
| `i32/u32` | `i32` | 32 bits |
| `i64/u64` | `i64` | 64 bits |
| `f32` | `float` | 32 bits |
| `f64` | `double` | 64 bits |
| `*T` | `T*` | ポインタ |
| `[T; N]` | `[N x T]` | 配列 |
| `struct` | `%struct` | 構造体 |

## 実装の優先順位

### Phase 1: 最小限の動作（1週間）
- [x] 基本型（int, float, bool）
- [x] 算術演算（+, -, *, /）
- [x] 制御フロー（if, while, for）
- [x] 関数定義と呼び出し
- [ ] LLVM IR生成
- [ ] ネイティブコード出力

### Phase 2: メモリ管理（1週間）
- [ ] ポインタ操作
- [ ] 配列
- [ ] 構造体
- [ ] スタック/ヒープ割り当て
- [ ] カスタムアロケータ

### Phase 3: プラットフォーム対応（1週間）
- [ ] Baremetal サポート
- [ ] WASM サポート
- [ ] リンカスクリプト対応
- [ ] 組み込み関数（intrinsics）

### Phase 4: 最適化（1週間）
- [ ] インライン化
- [ ] デッドコード除去
- [ ] 定数畳み込み
- [ ] ループ最適化

## ディレクトリ構造

```
src/
├── codegen/
│   ├── llvm/
│   │   ├── context.hpp       # LLVM コンテキスト管理
│   │   ├── mir_to_llvm.hpp   # MIR → LLVM IR 変換
│   │   ├── type_lowering.hpp # 型変換
│   │   ├── intrinsics.hpp    # 組み込み関数
│   │   └── target.hpp        # ターゲット管理
│   └── targets/
│       ├── baremetal.hpp     # ベアメタル設定
│       ├── native.hpp        # ネイティブ設定
│       └── wasm.hpp          # WASM設定
├── core/                      # no_std コアライブラリ
│   ├── types.cm
│   ├── mem.cm
│   └── panic.cm
└── std/                       # 標準ライブラリ
    ├── io.cm
    ├── fs.cm
    └── alloc.cm
```

## ビルドコマンド

```bash
# ベアメタル（ARM Cortex-M）
cm build --target=baremetal-arm --no-std program.cm -o program.elf

# ネイティブ（現在のOS）
cm build --target=native program.cm -o program

# WebAssembly
cm build --target=wasm32 program.cm -o program.wasm

# デバッグビルド
cm build --target=native --debug program.cm

# カスタムターゲット
cm build --target=custom.toml program.cm
```

## テスト計画

```bash
# 基本構文テスト
tests/llvm/
├── basic/
│   ├── hello_world.cm     # 最小限のプログラム
│   ├── arithmetic.cm      # 算術演算
│   └── control_flow.cm    # 制御構造
├── baremetal/
│   ├── no_std.cm         # no_std環境
│   ├── panic.cm          # パニックハンドラ
│   └── allocator.cm      # カスタムアロケータ
└── targets/
    ├── native_test.cm     # ネイティブ機能
    ├── wasm_test.cm       # WASM機能
    └── cross_compile.cm   # クロスコンパイル
```