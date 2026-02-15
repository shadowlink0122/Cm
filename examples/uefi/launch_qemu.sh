#!/bin/bash
# UEFI QEMU 起動スクリプト
# OVMFファームウェアを使用してUEFIアプリケーションを実行
#
# 使い方: ./launch_qemu.sh [OVMF_PATH]
#   OVMF_PATH: OVMFファームウェアのパス（デフォルト: ../../third-party/ovmf/RELEASEX64_OVMF.fd）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OVMF_FW="${1:-$SCRIPT_DIR/../../third-party/ovmf/RELEASEX64_OVMF.fd}"
ESP_DIR="$SCRIPT_DIR/esp"
EFI_FILE="$SCRIPT_DIR/BOOTX64.EFI"

# EFIファイルのチェック
if [ ! -f "$EFI_FILE" ]; then
    echo "エラー: EFIファイルが見つかりません: $EFI_FILE"
    echo "先にビルドしてください: make"
    exit 1
fi

# OVMFファームウェアのチェック
if [ ! -f "$OVMF_FW" ]; then
    echo "エラー: OVMFファームウェアが見つかりません: $OVMF_FW"
    echo ""
    echo "取得方法:"
    echo "  macOS: brew install qemu  (OVMFが含まれる場合あり)"
    echo "  手動: https://github.com/tianocore/edk2/releases からダウンロード"
    echo "  配置先: ../../third-party/ovmf/RELEASEX64_OVMF.fd"
    exit 1
fi

# ESPディレクトリ作成
mkdir -p "$ESP_DIR/EFI/BOOT"
cp "$EFI_FILE" "$ESP_DIR/EFI/BOOT/BOOTX64.EFI"

echo "=== UEFI QEMU 起動 ==="
echo "OVMF: $OVMF_FW"
echo "EFI:  $EFI_FILE"
echo "ESP:  $ESP_DIR"
echo "======================"

# QEMU起動（グラフィカルモード: GOPフレームバッファ使用）
# 注意: -vga stdではOVMFがGOPプロトコルを提供しない場合がある
# virtio-gpu-pciを使用することでGOPフレームバッファが確実に利用可能
qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_FW" \
    -drive format=raw,file=fat:rw:"$ESP_DIR" \
    -net none \
    -device virtio-gpu-pci \
    -display default,show-cursor=on \
    -serial stdio
