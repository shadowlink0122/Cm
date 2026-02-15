---
title: UEFIベアメタル開発
parent: Compiler
nav_order: 9
---

# UEFIベアメタル開発チュートリアル

**対象バージョン:** v0.14.0  
**難易度:** 🔴 上級  
**前提知識:** インラインアセンブリ、ポインタ操作

---

## 概要

Cm言語は `--target=uefi` オプションにより、OSを介さないベアメタル環境（UEFI）で動作するアプリケーションを生成できます。標準ライブラリ（std）を使用しない **no_std** モードで動作し、UEFI Boot Servicesを直接利用してハードウェアと対話します。

### no_std とは

通常のCmプログラムは `println` や `malloc` などの標準ライブラリ関数を使用しますが、これらはOSの機能（システムコール）に依存しています。UEFI環境ではOSが起動する前に実行されるため、標準ライブラリは使用できません。代わりに：

- **画面出力**: UEFI `ConOut->OutputString`
- **メモリ管理**: UEFI Boot Servicesの `AllocatePool`
- **入力**: UEFI `ConIn->ReadKeyStroke`

を直接呼び出す必要があります。

---

## 環境構築

### 必要なツール

| ツール | 用途 | インストール |
|--------|------|-------------|
| **QEMU** | x86_64エミュレータ | `brew install qemu` |
| **OVMF** | UEFIファームウェア | 自動ダウンロード（Makefile） |
| **lld-link** | PE/COFF リンカー | LLVM 17に付属 |
| **Cm** | コンパイラ | `make build` |

### ディレクトリ構成

```
tests/uefi/
├── Makefile              # ビルド・実行自動化
├── hello_world.cm        # エントリポイント
├── libs/
│   ├── efi_core.cm       # SystemTableアクセスヘルパー
│   └── efi_text.cm       # テキスト出力（ASM実装）
└── esp/                  # EFI System Partition（自動生成）
    └── EFI/BOOT/
        └── BOOTX64.EFI   # 生成されたEFIバイナリ
```

---

## Hello World

### エントリポイント

UEFIアプリケーションのエントリポイントは `efi_main` です。OSの `main` とは異なり、UEFIファームウェアから直接呼び出されます。

```cm
// hello_world.cm - UEFI Hello World
import ./libs/efi_core;
import ./libs/efi_text;

ulong efi_main(void* image_handle, void* system_table) {
    // 画面クリア
    efi_clear_screen(system_table);

    // メッセージ出力
    string msg = "Hello World from Cm!";
    efi_println(system_table, msg as void*);

    // 停止（画面を維持）
    while (true) {
        __asm__("hlt");
    }

    return 0;
}
```

**ポイント:**

- **`efi_main(void* image_handle, void* system_table)`**: UEFIの標準エントリポイント。`system_table`を通じて全てのUEFIサービスにアクセスします
- **`string msg`のvoid*キャスト**: Cmのstring型は内部表現が異なるため、`as void*`でポインタに変換して渡します
- **`__asm__("hlt")`**: CPUを停止し、画面を維持します

### UEFIサービスへのアクセス

UEFIの `SystemTable` はポインタのテーブルで、各サービスへのアドレスを保持しています：

```cm
// libs/efi_core.cm - SystemTableアクセスヘルパー

/// SystemTable から ConOut (offset 0x40) を取得
export void* efi_get_con_out(void* system_table) {
    ulong* st = system_table as ulong*;
    ulong con_out_addr = *(st + 8);  // 8 * 8 = offset 0x40
    return con_out_addr as void*;
}

/// SystemTable から BootServices (offset 0x60) を取得
export void* efi_get_boot_services(void* system_table) {
    ulong* st = system_table as ulong*;
    ulong bs_addr = *(st + 12);  // 12 * 8 = offset 0x60
    return bs_addr as void*;
}
```

### テキスト出力（ASM実装）

UEFI の `OutputString` は UCS-2エンコーディングが必要なため、ASCII→UCS-2変換をインラインASMで行います：

```cm
// libs/efi_text.cm - UCS-2変換 + OutputString呼出

export void efi_puts_raw(void* system_table, void* msg_data, ulong msg_len) {
    // ConOut と OutputString のアドレスを取得
    ulong* st = system_table as ulong*;
    ulong con_out_addr = *(st + 8);
    void* con_out = con_out_addr as void*;
    ulong* co = con_out as ulong*;
    ulong output_fn_addr = *(co + 1);

    ulong fn_val = output_fn_addr as ulong;
    ulong co_val = con_out as ulong;
    ulong md_val = msg_data as ulong;

    // pushq/popqパターンで安全にレジスタ転送
    must {
        __asm__(`
            pushq ${r:fn_val};
            pushq ${r:co_val};
            pushq ${r:md_val};
            pushq ${r:msg_len};
            popq %rbx;          // msg_len
            popq %r12;          // msg_data
            popq %r14;          // con_out
            popq %r13;          // OutputString
            subq $$512, %rsp;   // UCS-2バッファ確保
            xorq %rcx, %rcx;
            0:                  // ASCII→UCS-2変換ループ
                cmpq %rbx, %rcx;
                jge 1f;
                movzbl (%r12, %rcx), %eax;
                movw %ax, (%rsp, %rcx, 2);
                incq %rcx;
                jmp 0b;
            1:
                movw $$0, (%rsp, %rcx, 2);  // NUL終端
                movq %r14, %rcx;  // Win64 ABI: RCX = ConOut
                movq %rsp, %rdx;  // Win64 ABI: RDX = UCS-2文字列
                subq $$32, %rsp;  // シャドウスペース
                callq *%r13;      // OutputString呼出
                addq $$544, %rsp  // スタック復元
        `);
    }
}
```

---

## ビルドと実行

### Makefileを使う（推奨）

```bash
cd tests/uefi

# ビルド＆実行
make clean && make && make run

# 個別ステップ
make compile    # .cm → .o
make link       # .o → .EFI
make setup-esp  # ESP構造作成
make run        # QEMU起動
```

### 手動ビルド

```bash
# 1. コンパイル（UEFI PE/COFFオブジェクトファイル生成）
cm compile --target=uefi -o hello.o hello_world.cm

# 2. リンク（EFIアプリケーション生成）
lld-link /subsystem:efi_application /entry:efi_main /out:BOOTX64.EFI hello.o

# 3. ESP構造作成
mkdir -p esp/EFI/BOOT
cp BOOTX64.EFI esp/EFI/BOOT/BOOTX64.EFI

# 4. QEMU起動
qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file=OVMF.fd \
    -drive format=raw,file=fat:rw:esp \
    -net none -nographic -serial mon:stdio
```

---

## 注意事項

### Win64 ABI (Microsoft x64 Calling Convention)

UEFIはx86_64で **Win64 ABI** を使用します。引数は `RCX`, `RDX`, `R8`, `R9` の順に渡され、32バイトのシャドウスペースが必要です。

```
引数1: RCX
引数2: RDX
引数3: R8
引数4: R9
戻り値: RAX
シャドウスペース: RSP-32 (呼出前に確保)
```

### pushq/popq パターン

インラインASMでハードコードレジスタに値を転送する際は、必ず `pushq`/`popq` を使用してください。LLVMのレジスタ割り当てと競合する可能性があります。

```cm
// ❌ 危険: LLVMが同じレジスタを使用する可能性がある
__asm__("movq ${r:val}, %rcx");

// ✅ 安全: pushq/popqで競合を回避
__asm__(`
    pushq ${r:val};
    popq %rcx;
`);
```

### 自動クロバー検出

v0.14.0以降、コンパイラはインラインASM内のハードコードレジスタを自動的に検出し、LLVMのクロバーリストに追加します。これにより、インライン展開時にレジスタの値が不正に再利用されることを防止します。

---

## 関連リンク

- [インラインアセンブリ](../advanced/inline-asm.html) - `__asm__` の詳細
- [リリースノート v0.14.0](../../releases/v0.14.0.html) - UEFI対応の変更内容
- [UEFI仕様書](https://uefi.org/specs/UEFI/2.10/) - UEFI公式仕様
