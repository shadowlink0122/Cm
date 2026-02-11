# ベアメタル(no_std) & UEFI 環境サポート設計

## 1. 概要

Cm言語でOSなし（ベアメタル）環境とUEFI環境のプログラミングを実現する。
既存の `no_std` 基盤（`TargetConfig`, `setupNoStd()`, `__cm_alloc/__cm_panic`）を拡張し、
実際にコンパイル〜実行まで動作する環境を整備する。

### ゴール

| 環境 | エントリポイント | 出力形式 | リンカ | ターゲット例 |
|------|-----------------|---------|--------|-------------|
| ベアメタル ARM | `_start` | ELF `.o` → `.elf` | `arm-none-eabi-ld` | Cortex-M3/M4, RISC-V |
| ベアメタル x86 | `_start` | ELF `.o` → flat binary | `ld` | ブートローダ, カーネル |
| UEFI | `efi_main` | PE/COFF `.efi` | `lld-link` / `clang` | UEFI アプリ/ブートローダ |

---

## 2. アーキテクチャ

### 2.1 BuildTarget の拡張

```cpp
enum class BuildTarget {
    Baremetal,     // no_std, no_main (ARM)
    BaremetalX86,  // no_std, no_main (x86_64 freestanding)
    BaremetalUEFI, // UEFI Application (ベアメタルサブカテゴリ)
    Native,        // std, OS依存
    Wasm,          // WebAssembly
};
```

### 2.2 TargetConfig プロファイル

#### ベアメタル ARM (既存 + 改善)
```cpp
static TargetConfig getBaremetalARM() {
    return {
        .target = BuildTarget::Baremetal,
        .triple = "thumbv7m-none-eabi",
        .cpu = "cortex-m3",
        .features = "+thumb2",
        .noStd = true,
        .noMain = true,
        .optLevel = -1  // サイズ最適化
    };
}
```

#### ベアメタル x86_64 (新規)
```cpp
static TargetConfig getBaremetalX86() {
    return {
        .target = BuildTarget::Baremetal,
        .triple = "x86_64-unknown-none-elf",
        .cpu = "generic",
        .features = "-sse,-sse2,-mmx",  // カーネル/ブートローダではSSE無効
        .noStd = true,
        .noMain = true,
        .optLevel = 2
    };
}
```

#### UEFI x86_64 (新規)
```cpp
static TargetConfig getUEFI() {
    return {
        .target = BuildTarget::UEFI,
        .triple = "x86_64-unknown-windows-gnu",  // PE/COFF生成用
        .cpu = "generic",
        .features = "+sse,+sse2",  // UEFI環境ではSSE利用可能
        .noStd = true,
        .noMain = true,     // efi_mainを使用
        .optLevel = 2
    };
}
```

### 2.3 エントリポイント

```cpp
void setupEntryPoint(llvm::Function* mainFunc) {
    switch (targetConfig.target) {
        case BuildTarget::Baremetal:
            mainFunc->setName("_start");
            break;
        case BuildTarget::UEFI:
            mainFunc->setName("efi_main");
            // UEFI呼び出し規約: Microsoft x64 ABI
            mainFunc->setCallingConv(llvm::CallingConv::Win64);
            break;
        default:
            mainFunc->setName("main");
            break;
    }
}
```

---

## 3. メモリ管理

### 3.1 no_std アロケータ契約 (既存)

ユーザーが以下のシンボルを提供する：

```cm
// ユーザーが実装するアロケータ
extern fn __cm_alloc(size: long, align: long) -> void*;
extern fn __cm_dealloc(ptr: void*, size: long, align: long);
extern fn __cm_panic(msg: void*);
```

### 3.2 ベアメタル用シンプルアロケータ (標準提供)

`std/mem/bump_allocator.cm` として提供し、リンク時に使用可能にする：

```cm
// バンプアロケータ（ベアメタル用）
// リンカスクリプトで定義された _heap_start, _heap_end を使用
static long heap_ptr = 0;

fn __cm_alloc(size: long, align: long) -> void* {
    // アライメント調整 + 線形割り当て
    // ...
}
```

### 3.3 UEFI用アロケータ

UEFI Boot ServicesのAllocatePoolを使用：

```cm
// UEFI Boot Services経由のメモリ割り当て
fn __cm_alloc(size: long, align: long) -> void* {
    void* ptr = null;
    // gBS->AllocatePool(EfiLoaderData, size, &ptr)
    uefi_allocate_pool(2, size, &ptr);  // 2 = EfiLoaderData
    return ptr;
}
```

---

## 4. リンカ統合

### 4.1 `emitExecutable()` の拡張

```cpp
if (context->getTargetConfig().target == BuildTarget::Baremetal) {
    // ベアメタル: リンカスクリプト指定
    std::string ldScript = options.linkerScript.empty()
        ? "link.ld" : options.linkerScript;
    
    if (triple contains "arm" or "thumb") {
        linkCmd = "arm-none-eabi-ld -T " + ldScript + " " + objFile;
    } else if (triple contains "riscv") {
        linkCmd = "riscv64-unknown-elf-ld -T " + ldScript + " " + objFile;
    } else {
        // x86_64ベアメタル
        linkCmd = "ld -T " + ldScript + " --oformat=binary " + objFile;
    }
} else if (context->getTargetConfig().target == BuildTarget::UEFI) {
    // UEFI: PE/COFF出力
    linkCmd = "clang -target x86_64-unknown-windows-gnu"
              " -nostdlib -Wl,--subsystem,efi_application"
              " -fuse-ld=lld-link " + objFile
              + " -o " + options.outputFile;
}
```

### 4.2 リンカスクリプト例（ARM Cortex-M）

```ld
/* link.ld - Cortex-M3用 */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

SECTIONS {
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
    } > FLASH

    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } > RAM AT > FLASH

    .bss : {
        _bss_start = .;
        *(.bss*)
        _bss_end = .;
    } > RAM

    _heap_start = .;
    _heap_end = ORIGIN(RAM) + LENGTH(RAM);
}
```

---

## 5. プリプロセッサ定数

### 5.1 新規定数

| 定数 | 条件 | 用途 |
|------|------|------|
| `__NO_STD__` | `noStd == true` | libc非依存コードの分岐 |
| `__BAREMETAL__` | `target == Baremetal` | ベアメタル固有コード |
| `__UEFI__` | `target == UEFI` | UEFI固有コード |
| `__EFI__` | `target == UEFI` | `__UEFI__`のエイリアス |

### 5.2 使用例

```cm
#ifdef __BAREMETAL__
    // UART出力
    fn print(msg: string) {
        uart_write(msg);
    }
#endif

#ifdef __UEFI__
    // UEFI ConOut出力
    fn print(msg: string) {
        uefi_console_write(msg);
    }
#endif
```

---

## 6. CLIインターフェース

### 6.1 コマンド例

```bash
# ベアメタル ARM
cm compile --target=baremetal-arm -o firmware.elf main.cm

# ベアメタル ARM (オブジェクトファイルのみ)
cm compile --target=baremetal-arm --emit=obj -o firmware.o main.cm

# ベアメタル x86_64
cm compile --target=baremetal-x86 -o kernel.bin main.cm

# UEFI アプリケーション
cm compile --target=uefi -o BOOTX64.EFI main.cm

# カスタムリンカスクリプト指定
cm compile --target=baremetal-arm --linker-script=stm32f103.ld -o firmware.elf main.cm
```

### 6.2 target 名規則

| `--target` 値 | BuildTarget | プロファイル |
|-----|-------------|------------|
| `native` | Native | ホストOS (既存) |
| `wasm` | Wasm | WebAssembly (既存) |
| `js` / `web` | — | JS backend (既存) |
| `bm` / `baremetal-arm` | Baremetal | ARM Cortex-M |
| `bm-x86` / `baremetal-x86` | BaremetalX86 | x86_64 freestanding |
| `uefi` | BaremetalUEFI | UEFI x86_64 |

---

## 7. 実装フェーズ

### Phase 1: no_std基盤の実証（最優先）

1. CLIで `--target=baremetal-arm` / `baremetal-x86` / `uefi` を受け付ける
2. `BuildTarget::UEFI` と対応する `TargetConfig::getUEFI()` を追加
3. プリプロセッサ定数 `__NO_STD__`, `__BAREMETAL__`, `__UEFI__` を定義
4. `setupEntryPoint()` に UEFI の `efi_main` サポートを追加
5. テスト：最小限の `_start` / `efi_main` 関数をコンパイルして `.o` ファイルを生成

### Phase 2: リンカ統合

1. `emitExecutable()` にベアメタルx86/UEFIのリンカコマンドを追加
2. リンカスクリプトテンプレートを `docs/examples/baremetal/` に配置
3. UEFI向け PE/COFF 出力チェーン（clang + lld-link）を検証

### Phase 3: ランタイムとサンプル

1. `std/mem/bump_allocator.cm` の実装
2. UEFI Boot Services バインディングの提供
3. サンプルプログラム：
   - ベアメタル ARM: LED点滅（STM32）
   - ベアメタル x86: VGAテキスト出力
   - UEFI: "Hello from Cm!" コンソール出力
4. チュートリアル更新

---

## 8. テスト戦略

### 8.1 コンパイルテスト（CI対応）

ベアメタル/UEFIテストはCIでも実行可能なレベルに限定：

```bash
# オブジェクトファイル生成のみ（リンカ不要）
cm compile --target=baremetal-arm --emit=obj -o test.o test.cm

# LLVM IR生成テスト
cm compile --target=uefi --emit=llvm-ir -o test.ll test.cm
```

### 8.2 QEMUテスト（ローカル/CI拡張）

```bash
# ARM: QEMU Cortex-M3 エミュレータ
qemu-system-arm -M lm3s6965evb -nographic -kernel firmware.elf

# UEFI: QEMU + OVMF
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:esp -nographic
```

---

## 9. ディレクトリ構造

```
src/codegen/llvm/core/
├── context.hpp        # TargetConfig拡張（UEFI追加）
├── context.cpp        # setupNoStd/setupUEFI
docs/examples/
├── baremetal/
│   ├── arm/
│   │   ├── blink.cm          # LED点滅サンプル
│   │   └── link.ld           # リンカスクリプト
│   ├── x86/
│   │   ├── vga_hello.cm      # VGAテキスト出力
│   │   └── link.ld
│   └── uefi/
│       ├── hello.cm           # UEFI Hello World
│       └── uefi_types.cm      # UEFI型定義
tests/test_programs/baremetal/
├── minimal_start.cm           # 最小_startテスト
├── minimal_start.expect
├── uefi_entry.cm              # 最小efi_mainテスト
└── uefi_entry.expect
```
