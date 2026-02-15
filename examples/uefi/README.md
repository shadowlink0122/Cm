# UEFI サンプル

Cm言語でUEFI (Unified Extensible Firmware Interface) アプリケーションを開発するサンプルです。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `hello_world.cm` | UEFI Hello World（画面出力） |
| `memory_test.cm` | メモリ管理テスト（AllocatePool/FreePool） |
| `libs/efi_core.cm` | SystemTableアクセスヘルパー |
| `libs/efi_text.cm` | テキスト出力（ASM経由） |
| `Makefile` | ビルド・QEMU実行 |
| `launch_qemu.sh` | QEMU起動スクリプト |

## 必要環境

- Cm コンパイラ（`--target=uefi` サポート）
- `lld-link`（PE/COFFリンカー）
- QEMU（`qemu-system-x86_64`）
- OVMFファームウェア（自動ダウンロード可）

## ビルド・実行

```bash
cd examples/uefi

# コンパイル＆リンク
make

# QEMUで実行（OVMF自動取得）
make run

# クリーン
make clean
```

## 仕組み

1. `cm compile --target=uefi` でオブジェクトファイル生成
2. `lld-link` でPE/COFF形式のEFIアプリケーションにリンク
3. ESP (EFI System Partition) ディレクトリ構造に配置
4. QEMUのOVMFファームウェアで起動
