# Cm Linter テスト Makefile
# Usage: make test

.PHONY: all test clean

SCRIPTS_DIR = scripts

# デフォルトターゲット
all: test

# テスト実行
test:
	@chmod +x $(SCRIPTS_DIR)/run_tests.sh
	@$(SCRIPTS_DIR)/run_tests.sh

# クリーンアップ
clean:
	@rm -rf workspace/*
	@echo "Cleaned workspace"
