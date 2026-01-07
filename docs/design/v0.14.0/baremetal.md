[English](baremetal.en.html)

# Bare-metal / OS開発 設計

## 概要

Cmをオペレーティングシステムや組み込みシステム開発に使用するための設計です。

## no_std モード

### 有効化

```toml
# Cm.toml
[package]
name = "my-kernel"

[target.baremetal]
std = false              # 標準ライブラリ無効
backend = "rust"         # Rust経由
arch = "x86_64"          # ターゲットアーキテクチャ
```

### 基本構造

```cpp
// Cm (no_std)
#![no_std]
#![no_main]

extern "C" {
    fn _start() -> !;
}

#[entry]
void _start() -> ! {
    // カーネル初期化
    init_gdt();
    init_idt();
    init_memory();
    
    // メインループ
    loop {
        halt();
    }
}

#[panic_handler]
void panic(PanicInfo info) -> ! {
    // パニック処理
    loop { halt(); }
}
```

---

## メモリ管理

### 手動メモリ管理

```cpp
// Cm
#[allocator]
struct BumpAllocator {
    *byte heap_start;
    *byte heap_end;
    *byte next;
    
    *byte alloc(size_t size) {
        *byte result = this.next;
        this.next += size;
        if (this.next > this.heap_end) {
            return null;
        }
        return result;
    }
}

// グローバルアロケータ設定
#[global_allocator]
static ALLOCATOR: BumpAllocator = BumpAllocator {
    heap_start: 0x1000_0000 as *byte,
    heap_end: 0x2000_0000 as *byte,
    next: 0x1000_0000 as *byte,
};
```

---

## ハードウェアアクセス

### ポートI/O (x86)

```cpp
// Cm
#[inline(always)]
void outb(uint16 port, uint8 value) {
    asm volatile (
        "outb %0, %1"
        :: "a"(value), "Nd"(port)
    );
}

#[inline(always)]
uint8 inb(uint16 port) -> uint8 {
    uint8 result;
    asm volatile (
        "inb %1, %0"
        : "=a"(result)
        : "Nd"(port)
    );
    return result;
}

// VGA text mode
void print_char(char c) {
    *uint16 vga = 0xB8000 as *uint16;
    *vga = (0x0F << 8) | (c as uint16);
}
```

### MMIO

```cpp
// Cm
struct Volatile<T> {
    *T ptr;
    
    T read() {
        return asm volatile ("" : : : "memory"), *this.ptr;
    }
    
    void write(T value) {
        *this.ptr = value;
        asm volatile ("" : : : "memory");
    }
}

// UART例
struct Uart {
    Volatile<uint8> data;
    Volatile<uint8> status;
    
    void write_byte(uint8 byte) {
        while ((this.status.read() & 0x20) == 0) {}
        this.data.write(byte);
    }
}
```

---

## 割り込み処理

```cpp
// Cm
#[interrupt]
void timer_interrupt(InterruptFrame* frame) {
    // タイマー割り込み処理
    acknowledge_interrupt(0x20);
}

#[interrupt]
void page_fault(InterruptFrame* frame, uint64 error_code) {
    // ページフォルト処理
    uint64 addr = read_cr2();
    panic("Page fault at {}", addr);
}

// IDT設定
void setup_idt() {
    IDT[0x20] = InterruptDescriptor::new(timer_interrupt);
    IDT[0x0E] = InterruptDescriptor::new(page_fault);
}
```

---

## リンカスクリプト

```ld
/* kernel.ld */
ENTRY(_start)

SECTIONS {
    . = 0x100000;  /* 1MB */
    
    .text : {
        *(.text)
    }
    
    .rodata : {
        *(.rodata)
    }
    
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
    }
}
```

```toml
# Cm.toml
[target.baremetal]
linker_script = "kernel.ld"
```

---

## ターゲット定義

### x86_64

```toml
[target.x86_64-unknown-none]
arch = "x86_64"
features = ["-mmx", "-sse", "+soft-float"]
linker = "ld.lld"
```

### ARM (Raspberry Pi)

```toml
[target.aarch64-unknown-none]
arch = "aarch64"
cpu = "cortex-a53"
linker = "aarch64-none-elf-ld"
```

### RISC-V

```toml
[target.riscv64gc-unknown-none-elf]
arch = "riscv64"
linker = "riscv64-unknown-elf-ld"
```

---

## cm-core ライブラリ

no_stdでも使用可能な最小限のライブラリ:

```cpp
// Cm
import cm_core::mem;      // memcpy, memset
import cm_core::fmt;      // 文字列フォーマット（allocなし）
import cm_core::sync;     // SpinLock, Mutex
import cm_core::ptr;      // ポインタ操作
```

---

## 制限事項

| 項目 | 状態 |
|------|------|
| no_std | ✅ 対応 |
| インラインasm | ✅ Rust経由 |
| MMIO | ✅ Volatile型 |
| 割り込み | ✅ #[interrupt] |
| 動的メモリ | ⚠️ カスタムアロケータ必要 |
| async | ❌ no_stdでは非対応 |