# Bug #13: インライン展開時のレジスタ上書き（UEFIクラッシュ）

> Cm v0.14.1 / `--target=uefi` (x86_64) / 2026-02-17確認
>
> 関連: [cm_compiler_bugs_open.md](./cm_compiler_bugs_open.md) Bug #11, #12

---

## 概要

Cm v0.14.1へのアップデート後、**全てのUEFIカーネルビルドがQEMU起動時に
`#UD - Invalid Opcode`で即座にクラッシュ**するようになった。

根本原因は、`impl`メソッド/コンストラクタのインライン展開時に
`efi_main`関数の引数レジスタ（`rcx`, `rdx`）がインライン展開されたコードで
上書きされ、制御フローが破壊されること。

---

## 症状

```
!!!! X64 Exception Type - 06(#UD - Invalid Opcode)  CPU Apic ID - 00000000 !!!!
RIP  - 0000000006630C23
(ImageBase=0000000006574000, EntryPoint=00000000065D66EC)
```

- シリアル出力、デバッグポート出力が一切出ない（`efi_main`の最初の行の前にクラッシュ）
- 最小テスト（`impl`不使用、直接`outb`命令）では正常動作
- `impl`メソッドを1つでも使用するとクラッシュ

---

## 再現手順

### ✅ 正常動作するケース（impl不使用）

```cm
//! platform: uefi

ulong efi_main(void* image_handle, void* system_table) {
    ulong port = 0x3F8;
    ulong ch = 0x48;
    __asm__(`
        movq ${r:port}, %rdx;
        movq ${r:ch}, %rax;
        outb %al, %dx
    `);
    while (true) { __asm__("hlt"); }
    return 0;
}
```

→ QEMUシリアルに `H` が出力される ✅

### ❌ クラッシュするケース（impl使用）

```cm
//! platform: uefi
import ./drivers/serial;

ulong efi_main(void* image_handle, void* system_table) {
    SerialPort serial(0x3F8);     // ← implコンストラクタ
    serial.putc(0x48);            // ← implメソッド呼び出し
    while (true) { __asm__("hlt"); }
    return 0;
}
```

→ `#UD - Invalid Opcode` クラッシュ ❌

### ビルド・実行手順

```bash
cm compile --target=uefi -o .tmp/build/test.o test.cm
lld-link /subsystem:efi_application /entry:efi_main /out:.tmp/build/TEST.EFI .tmp/build/test.o
# QEMUでESPにコピーして起動
```

---

## 原因分析

### 逆アセンブル解析

`efi_main`のエントリポイント逆アセンブル（impl使用版）:

```asm
efi_main:
  ;; MS ABI: rcx = image_handle, rdx = system_table
  sub  rsp, 0x10           ; スタックフレーム確保
  lea  rax, [rsp+8]        ; rax = &serial（スタック上のSerialPort）
  mov  [rsp], rax           ; ローカル変数にselfポインタ保存
  nop

  ;; --- ここからSerialPortコンストラクタがインライン展開 ---
  sub  rsp, 0x18           ; コンストラクタのスタックフレーム
  mov  [rsp+8], rdx        ; ❌ rdx(system_table)をport引数として保存
  mov  rax, [rsp+8]
  mov  [rcx], rax           ; ❌ rcx(image_handle)のアドレスへ書き込み！
```

### 問題の本質

1. `SerialPort serial(0x3F8)`のコンストラクタ`self(port)`がインライン展開される
2. インライン展開されたコードが`rcx`をselfポインタ、`rdx`をport引数として使用
3. しかし`efi_main`の呼び出し規約（MS ABI）では:
   - `rcx` = `image_handle`（第1引数）
   - `rdx` = `system_table`（第2引数）
4. **コンストラクタ引数の定数`0x3F8`がrdxから読み取られるが、rdxにはsystem_tableが入っている**
5. **selfへの書き込み(`mov [rcx], rax`)がimage_handleのアドレスに書き込む**
6. UEFIデータ構造が破壊され、後続のコードが不正なアドレスにジャンプ → `#UD`

### 正常なケースとの比較

```asm
;; impl不使用の最小テスト:
efi_main:
  sub  rsp, 0x10
  mov  qword [rsp+8], 0x3F8  ; ✅ 即値でport設定（rcx/rdx非依存）
  mov  qword [rsp], 0x48      ; ✅ 即値でch設定
  ...
  outb %al, %dx               ; ✅ 正常実行
```

impl不使用版ではレジスタ引数を上書きしないため、efi_mainの引数が保持される。

---

## 検証マトリクス

| テスト | impl使用 | 結果 | 備考 |
|--------|---------|------|------|
| 直接outb (`__asm__`) | なし | ✅ 正常 | 即値でポート/文字を設定 |
| `import serial` のみ（未使用） | なし | ✅ 正常 | 未使用コードは除去される |
| `SerialPort serial(0x3F8)` + `serial.putc()` | あり | ❌ クラッシュ | コンストラクタ展開でrcx/rdx上書き |
| フルカーネル（全モジュール） | あり | ❌ クラッシュ | 同一RIPで毎回クラッシュ |
| 空ESP（EFI無し） | - | ✅ 正常 | OVMFファームウェア自体は正常 |

---

## 影響範囲

**Cm v0.14.1のUEFIターゲットで`impl`メソッドまたはコンストラクタを使用する
全てのプログラムが影響を受ける。**

- CosmOSカーネル全体（SerialPort, DebugPort, PIC, IDT, ISR, PMM, VMM, Heap, PIT等はすべてimplを使用）
- テストスイート（test_boot, test_serial等）
- `Cm/examples/uefi/`のサンプルプログラム

---

## 回避策

### 案1: impl不使用（フリー関数パターン）

全モジュールからimplを除去し、フリー関数で実装する。
ただしCosmOSの多数のモジュールが影響を受けるため、大規模な変更が必要。

### 案2: efi_main引数の即時退避

`efi_main`の最初の行でrcx/rdxをスタックに退避する:

```cm
ulong efi_main(void* image_handle, void* system_table) {
    // 引数を即座にローカル変数に退避
    void* ih = image_handle;
    void* st = system_table;
    
    // 以降はih/stを使用
    SerialPort serial(0x3F8);
    // ...
}
```

→ **効果未検証**。Cmの最適化パスが退避をelide（除去）する可能性がある。

### 案3: Cmコンパイラの修正待ち

インライン展開時のレジスタ保存/復元を正しく実装するCm更新を待つ。

---

## 技術詳細

### PEバイナリ分析

| 項目 | 値 |
|------|-----|
| PE Format | PE32+ (x86_64) |
| ImageBase | `0x140000000` |
| EntryPoint RVA | `0xa000` |
| .text | RVA `0x1000`, サイズ `0x9c8d`（フルカーネル） |
| Subsystem | 10 (EFI_APPLICATION) |
| リロケーション | なし（全REL32=PC相対） |
| call命令 | 6個（全てASM経由 `callq *%r10`） |
| ret命令 | 319個 |
| 関数インライン化 | **全関数がインライン展開** |

### QEMU例外情報

```
Exception Type - 06 (#UD - Invalid Opcode)
RIP  - 0x6630C23  (オフセット 0xBCC23: .textセクション外)
RSP  - 0x7E9C6A8
RBP  - 0x7E9C800
RAX  - 0x92  (シリアルポートのステータスバイト)
RDX  - 0x49  ('I'のASCIIコード)
```

RIPが`.text`セクション外（`0x1000`〜`0xAC8D`）にあることから、
不正なアドレスへのジャンプ（破壊されたUEFIデータ構造から読み取った
関数ポインタの間接呼び出し等）が発生していると推定。

---

## 関連バグ

| Bug | 内容 | 関連性 |
|-----|------|--------|
| #10 | ポインタ経由implメソッドでself変更が消失 | impl LLVM IR生成の問題 |
| #11 | インライン展開によるASMレジスタ割当変更 | レジスタ管理の問題 |
| #12 | インライン展開時のret先不在 | インライン展開の制御フロー |
| **#13** | **インライン展開時のレジスタ上書き** | **本バグ** |

---

## 修正に必要な対応（コンパイラ側）

1. **インライン展開時のレジスタ保存**: 関数がインライン展開される際に、
   呼び出し元の引数レジスタ（rcx, rdx, r8, r9）を事前にスタックに退避し、
   展開コード完了後に復元する
2. **コンストラクタ引数の即値化**: 定数引数（`0x3F8`等）はレジスタ渡しではなく
   即値として生成する
3. **Self書き込みの安全化**: コンストラクタ内の`self.field = value`で、
   selfのアドレスが呼び出し元の引数レジスタと衝突しないことを保証する
