---
title: UEFI Bare-Metal Development
parent: Compiler
nav_order: 9
---

# UEFI Bare-Metal Development Tutorial

**Target version:** v0.17.1 **Level:** 🔴 Advanced **Prerequisites:** Inline assembly, pointer operations

---

## Overview

With `--target=uefi`, the Cm compiler produces applications that run in a bare-metal (UEFI) environment without an OS. They run in **no_std** mode —no standard library — and talk to hardware directly through UEFI Boot Services.

### What no_std means

Ordinary Cm programs use standard library functions such as `println` and `malloc`, which depend on OS system calls. UEFI applications run before any OS boots, so the standard library is unavailable. Instead you call:

- **Screen output**: UEFI `ConOut->OutputString`
- **Memory management**: `AllocatePool` from UEFI Boot Services
- **Input**: UEFI `ConIn->ReadKeyStroke`

---

### Features Rejected at Compile Time (enforcement hardened in v0.17.0)

The no_std constraint is checked at compile time and violations are errors:

- **Direct calls to OS-dependent functions**: `println`, `malloc`, file I/O, pthread, etc.
- **Taking the address of a forbidden function**: indirect calls through `&putchar`-style function pointers cannot bypass the restrictions
- **Language features that allocate on the heap**: string concatenation (`a + b`), number-to-string conversion, interpolation formatting, dynamic slice operations, and array higher-order methods

Floating point is available on UEFI (the x86_64 ABI enables SSE) but rejected with a dedicated diagnostic on `--target=baremetal-x86` (SSE disabled).

## Environment setup

### Required tools

| Tool | Purpose | Install |
|------|---------|---------|
| **QEMU** | x86_64 emulator | `brew install qemu` |
| **OVMF** | UEFI firmware | auto-downloaded (Makefile) |
| **lld-link** | PE/COFF linker | ships with LLVM 17 |
| **Cm** | compiler | `make build` |

### Directory layout

```
tests/uefi/
├── Makefile              # build/run automation
├── hello_world.cm        # entry point
├── libs/
│   ├── efi_core.cm       # SystemTable access helpers
│   └── efi_text.cm       # text output (ASM implementation)
└── esp/                  # EFI System Partition (generated)
    └── EFI/BOOT/
        └── BOOTX64.EFI   # generated EFI binary
```

---

## Hello World

### Entry point

The entry point of a UEFI application is `efi_main`. Unlike an OS `main`, it is called directly by the UEFI firmware.

```cm
// hello_world.cm - UEFI Hello World
import ./libs/efi_core;
import ./libs/efi_text;

ulong efi_main(void* image_handle, void* system_table) {
    // Clear the screen
    efi_clear_screen(system_table);

    // Print a message
    string msg = "Hello World from Cm!";
    efi_println(system_table, msg as void*);

    // Halt (keep the screen)
    while (true) {
        __asm__("hlt");
    }

    return 0;
}
```

**Key points:**

- **`efi_main(void* image_handle, void* system_table)`**: the standard UEFI entry point; `system_table` gives access to all UEFI services
- **`string` cast to `void*`**: Cm strings have their own internal representation, so pass them as pointers with `as void*`
- **`__asm__("hlt")`**: halts the CPU while keeping the display

### Accessing UEFI services

The UEFI `SystemTable` is a table of pointers to each service:

```cm
// libs/efi_core.cm - SystemTable access helpers

/// Get ConOut (offset 0x40) from the SystemTable
export void* efi_get_con_out(void* system_table) {
    ulong* st = system_table as ulong*;
    ulong con_out_addr = *(st + 8);  // 8 * 8 = offset 0x40
    return con_out_addr as void*;
}

/// Get BootServices (offset 0x60) from the SystemTable
export void* efi_get_boot_services(void* system_table) {
    ulong* st = system_table as ulong*;
    ulong bs_addr = *(st + 12);  // 12 * 8 = offset 0x60
    return bs_addr as void*;
}
```

### Text output (ASM implementation)

UEFI's `OutputString` requires UCS-2 encoding, so the ASCII→UCS-2 conversion is done in inline assembly (see the Japanese page for the full listing; the pattern pushes arguments with `pushq`/`popq`, builds a UCS-2 buffer on the stack, and calls `OutputString` with the Win64 ABI).

---

## Build and run

### Using the Makefile (recommended)

```bash
cd tests/uefi

# Build & run
make clean && make && make run

# Individual steps
make compile    # .cm → .o
make link       # .o → .EFI
make setup-esp  # create the ESP layout
make run        # launch QEMU
```

### Manual build

```bash
# 1. Compile (UEFI PE/COFF object file)
cm compile --target=uefi -o hello.o hello_world.cm

# 2. Link (EFI application)
lld-link /subsystem:efi_application /entry:efi_main /out:BOOTX64.EFI hello.o

# 3. Create the ESP layout
mkdir -p esp/EFI/BOOT
cp BOOTX64.EFI esp/EFI/BOOT/BOOTX64.EFI

# 4. Launch QEMU
qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file=OVMF.fd \
    -drive format=raw,file=fat:rw:esp \
    -net none -nographic -serial mon:stdio
```

---

## Caveats

### Win64 ABI (Microsoft x64 calling convention)

UEFI uses the **Win64 ABI** on x86_64. Arguments go in `RCX`, `RDX`, `R8`, `R9`, and a 32-byte shadow space is required.

```
arg1: RCX
arg2: RDX
arg3: R8
arg4: R9
return: RAX
shadow space: RSP-32 (reserve before the call)
```

### The pushq/popq pattern

When moving values into hard-coded registers in inline ASM, always use `pushq`/`popq` — otherwise you may clash with LLVM's register allocation.

```cm
// ❌ Dangerous: LLVM may be using the same register
__asm__("movq ${r:val}, %rcx");

// ✅ Safe: avoid conflicts with pushq/popq
__asm__(`
    pushq ${r:val};
    popq %rcx;
`);
```

### Automatic clobber detection

Since v0.14.0 the compiler detects hard-coded registers inside inline ASM and adds them to LLVM's clobber list automatically, preventing incorrect register reuse during inlining.

---

## Related links

- [Inline assembly](../../../ja/advanced/inline-asm.html) — details of `__asm__`
- [Release notes v0.14.0](../../../../releases/v0.14.0.html) — UEFI support changes
- [UEFI specification](https://uefi.org/specs/UEFI/2.10/) — official spec

---

<!-- nav -->
← Prev: [Compiler - LLVM Backend](index.html) | [Contents](index.html) | Next: [Compiler - WASM Backend](../wasm/index.html) →
