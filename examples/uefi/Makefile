# UEFI サンプル Makefile
# 使い方:
#   make          - コンパイル＆リンク
#   make compile  - コンパイルのみ
#   make link     - リンクのみ
#   make run      - QEMUで実行（OVMFを自動ダウンロード）
#   make clean    - 生成ファイル削除

CM ?= ../../cm
LLD ?= lld-link
QEMU ?= qemu-system-x86_64

# OVMFファームウェアの配置先
OVMF_DIR ?= ../../third-party/ovmf
OVMF_FW ?= $(OVMF_DIR)/OVMF.fd

# OVMF ダウンロードURL (Arch: x86_64, Release build)
OVMF_URL ?= https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd

SRC = main.cm
OBJ = main.o
EFI = BOOTX64.EFI
ESP_DIR = esp
ESP_BOOT = $(ESP_DIR)/EFI/BOOT

.PHONY: all compile link run clean setup-esp download-ovmf

all: $(EFI)

# Cmソースをコンパイル（uefiターゲット）
compile: $(OBJ)
$(OBJ): $(SRC)
	$(CM) compile --target=uefi -o $(OBJ) $(SRC)

# PE/COFF EFIアプリケーションにリンク
link: $(EFI)
$(EFI): $(OBJ)
	$(LLD) /subsystem:efi_application /entry:efi_main /out:$(EFI) $(OBJ)

# ESP (EFI System Partition) ディレクトリ構造を作成
setup-esp: $(EFI)
	mkdir -p $(ESP_BOOT)
	cp $(EFI) $(ESP_BOOT)/BOOTX64.EFI

# OVMFファームウェアをダウンロード
download-ovmf:
	@if [ ! -f "$(OVMF_FW)" ]; then \
		echo "OVMFファームウェアをダウンロード中..."; \
		mkdir -p $(OVMF_DIR); \
		curl -L -o "$(OVMF_FW)" "$(OVMF_URL)" 2>/dev/null || \
		wget -q -O "$(OVMF_FW)" "$(OVMF_URL)" 2>/dev/null || \
		{ echo "エラー: ダウンロードに失敗しました"; \
		  echo "手動で取得してください: $(OVMF_URL)"; \
		  echo "配置先: $(OVMF_FW)"; \
		  exit 1; }; \
		echo "ダウンロード完了: $(OVMF_FW)"; \
	else \
		echo "OVMF: $(OVMF_FW) (キャッシュ済み)"; \
	fi

# QEMUでUEFIアプリケーションを実行
run: setup-esp download-ovmf
	@echo "=== UEFI QEMU 起動 ==="
	@echo "OVMF: $(OVMF_FW)"
	@echo "EFI:  $(EFI)"
	@echo "======================"
	$(QEMU) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_FW) \
		-drive format=raw,file=fat:rw:$(ESP_DIR) \
		-net none \
		-device virtio-gpu-pci \
		-display default,show-cursor=on \
		-serial mon:stdio

clean:
	rm -f $(OBJ) $(EFI) *.lib
	rm -rf $(ESP_DIR)
