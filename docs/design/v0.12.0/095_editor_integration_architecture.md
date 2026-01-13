# Editor Integration Architecture for Cm Language
**Date**: 2026-01-14
**Version**: v0.12.0

## 概要

Cm言語の包括的なエディタ統合アーキテクチャ設計。LSPを中心に、各種エディタ/IDEとの統合方法と、エディタ固有の拡張機能を定義します。

## 設計目標

1. **統一された開発体験**: どのエディタでも同等の機能を提供
2. **LSPファースト**: Language Server Protocolを基盤とする
3. **エディタ固有最適化**: 各エディタの特性を活かした実装
4. **プラグアンドプレイ**: 簡単なインストールと設定
5. **拡張可能**: カスタマイズと拡張が容易

## 全体アーキテクチャ

```
┌──────────────────────────────────────────────────────┐
│                    Editors/IDEs                       │
├────────┬─────────┬──────────┬──────────┬────────────┤
│ VSCode │ Neovim  │  Sublime │ IntelliJ │    Emacs   │
├────────┴─────────┴──────────┴──────────┴────────────┤
│              LSP Client Implementations               │
└────────────────────┬─────────────────────────────────┘
                     │ LSP Protocol
┌────────────────────▼─────────────────────────────────┐
│                  cm-lsp Server                        │
├───────────────────────────────────────────────────────┤
│    Compiler Frontend │ Type System │ Diagnostics     │
└───────────────────────────────────────────────────────┘
                     │
┌────────────────────▼─────────────────────────────────┐
│              Additional Tools                         │
├─────────────┬──────────────┬─────────────────────────┤
│   cmfmt     │   cmlint     │      cm-debug           │
└─────────────┴──────────────┴─────────────────────────┘
```

## Neovim Integration

### 1. Lua プラグイン構成

```lua
-- lua/cm/init.lua
local M = {}

-- LSP設定
function M.setup_lsp()
    local lspconfig = require('lspconfig')
    local configs = require('lspconfig.configs')

    -- Cmサーバー設定
    if not configs.cm_lsp then
        configs.cm_lsp = {
            default_config = {
                cmd = {'cm-lsp', '--stdio'},
                filetypes = {'cm'},
                root_dir = function(fname)
                    return lspconfig.util.find_git_ancestor(fname)
                        or lspconfig.util.root_pattern('Cm.toml')(fname)
                        or vim.fn.getcwd()
                end,
                settings = {
                    cm = {
                        inlayHints = {
                            enable = true,
                            typeHints = true,
                            parameterHints = true,
                        },
                    },
                },
                init_options = {
                    -- カスタム初期化オプション
                },
            },
        }
    end

    -- サーバー起動
    lspconfig.cm_lsp.setup({
        on_attach = M.on_attach,
        capabilities = M.capabilities(),
    })
end

-- LSPアタッチ時の設定
function M.on_attach(client, bufnr)
    local opts = { buffer = bufnr }

    -- キーマッピング
    vim.keymap.set('n', 'gD', vim.lsp.buf.declaration, opts)
    vim.keymap.set('n', 'gd', vim.lsp.buf.definition, opts)
    vim.keymap.set('n', 'K', vim.lsp.buf.hover, opts)
    vim.keymap.set('n', 'gi', vim.lsp.buf.implementation, opts)
    vim.keymap.set('n', '<C-k>', vim.lsp.buf.signature_help, opts)
    vim.keymap.set('n', '<leader>rn', vim.lsp.buf.rename, opts)
    vim.keymap.set('n', '<leader>ca', vim.lsp.buf.code_action, opts)
    vim.keymap.set('n', 'gr', vim.lsp.buf.references, opts)
    vim.keymap.set('n', '<leader>f', function()
        vim.lsp.buf.format { async = true }
    end, opts)

    -- インレイヒント
    if client.server_capabilities.inlayHintProvider then
        vim.lsp.inlay_hint.enable(bufnr, true)
    end

    -- セマンティックトークン
    if client.server_capabilities.semanticTokensProvider then
        vim.lsp.semantic_tokens.start(bufnr, client.id)
    end
end

-- 機能設定
function M.capabilities()
    local capabilities = vim.lsp.protocol.make_client_capabilities()

    -- nvim-cmp統合
    local has_cmp, cmp_nvim_lsp = pcall(require, 'cmp_nvim_lsp')
    if has_cmp then
        capabilities = cmp_nvim_lsp.default_capabilities(capabilities)
    end

    -- スニペットサポート
    capabilities.textDocument.completion.completionItem.snippetSupport = true
    capabilities.textDocument.completion.completionItem.resolveSupport = {
        properties = { 'documentation', 'detail', 'additionalTextEdits' },
    }

    return capabilities
end

-- Treesitter設定
function M.setup_treesitter()
    require('nvim-treesitter.configs').setup({
        ensure_installed = { 'cm' },
        highlight = {
            enable = true,
            additional_vim_regex_highlighting = false,
        },
        indent = {
            enable = true,
        },
        incremental_selection = {
            enable = true,
            keymaps = {
                init_selection = 'gnn',
                node_incremental = 'grn',
                scope_incremental = 'grc',
                node_decremental = 'grm',
            },
        },
    })
end

-- デバッグアダプター設定
function M.setup_dap()
    local dap = require('dap')

    dap.adapters.cm = {
        type = 'executable',
        command = 'cm-debug',
        args = { '--stdio' },
    }

    dap.configurations.cm = {
        {
            type = 'cm',
            request = 'launch',
            name = 'Launch Cm Program',
            program = function()
                return vim.fn.input('Path to executable: ', vim.fn.getcwd() .. '/', 'file')
            end,
            cwd = '${workspaceFolder}',
            stopOnEntry = false,
        },
    }
end

return M
```

### 2. プラグインマネージャー設定

```lua
-- lazy.nvim 設定例
return {
    {
        'cm-lang/cm.nvim',
        ft = { 'cm' },
        dependencies = {
            'neovim/nvim-lspconfig',
            'nvim-treesitter/nvim-treesitter',
            'hrsh7th/nvim-cmp',
            'mfussenegger/nvim-dap',
        },
        config = function()
            local cm = require('cm')
            cm.setup_lsp()
            cm.setup_treesitter()
            cm.setup_dap()
        end,
    },
}
```

## Sublime Text Integration

### 1. パッケージ構成

```python
# cm_language_server.py
import sublime
import sublime_plugin
from LSP.plugin import AbstractPlugin, register_plugin, unregister_plugin
from LSP.plugin.core.typing import Dict, List, Optional, Tuple

class CmLanguageServer(AbstractPlugin):
    @classmethod
    def name(cls) -> str:
        return "cm-lsp"

    @classmethod
    def configuration(cls) -> Tuple[sublime.Settings, str]:
        settings = sublime.load_settings("LSP-cm.sublime-settings")
        settings_path = "Packages/User/LSP-cm.sublime-settings"
        return settings, settings_path

    @classmethod
    def can_start(
        cls,
        window: sublime.Window,
        initiating_view: sublime.View,
        workspace_folders: List[str],
        configuration: Dict
    ) -> Optional[str]:
        # Cm.tomlがあるディレクトリをルートとする
        for folder in workspace_folders:
            if os.path.exists(os.path.join(folder, "Cm.toml")):
                return folder
        return workspace_folders[0] if workspace_folders else None

def plugin_loaded() -> None:
    register_plugin(CmLanguageServer)

def plugin_unloaded() -> None:
    unregister_plugin(CmLanguageServer)
```

### 2. 設定ファイル

```json
// LSP-cm.sublime-settings
{
    "enabled": true,
    "command": ["cm-lsp", "--stdio"],
    "settings": {
        "cm": {
            "inlayHints": {
                "enable": true,
                "typeHints": true,
                "parameterHints": true
            },
            "format": {
                "indentSize": 4
            }
        }
    },
    "initializationOptions": {},
    "languages": [
        {
            "languageId": "cm",
            "scopes": ["source.cm"],
            "syntaxes": ["Packages/Cm/Cm.sublime-syntax"]
        }
    ]
}
```

### 3. シンタックス定義

```yaml
# Cm.sublime-syntax
%YAML 1.2
---
name: Cm
file_extensions:
  - cm
scope: source.cm

contexts:
  main:
    - include: comments
    - include: strings
    - include: keywords
    - include: types
    - include: functions
    - include: numbers
    - include: operators

  comments:
    - match: '//'
      scope: punctuation.definition.comment.cm
      push:
        - meta_scope: comment.line.double-slash.cm
        - match: $\n?
          pop: true

    - match: '/\*'
      scope: punctuation.definition.comment.begin.cm
      push:
        - meta_scope: comment.block.cm
        - match: '\*/'
          scope: punctuation.definition.comment.end.cm
          pop: true

  strings:
    - match: '"'
      scope: punctuation.definition.string.begin.cm
      push:
        - meta_scope: string.quoted.double.cm
        - match: '\\.'
          scope: constant.character.escape.cm
        - match: '"'
          scope: punctuation.definition.string.end.cm
          pop: true

  keywords:
    - match: '\b(if|else|while|for|return|break|continue|match|case)\b'
      scope: keyword.control.cm
    - match: '\b(struct|enum|union|typedef|impl|trait|with)\b'
      scope: keyword.declaration.cm
    - match: '\b(const|static|extern|inline|volatile)\b'
      scope: storage.modifier.cm
    - match: '\b(move|sizeof|typeof|alignof)\b'
      scope: keyword.operator.cm

  types:
    - match: '\b(void|bool|char|int|float|double|int8|int16|int32|int64|uint8|uint16|uint32|uint64)\b'
      scope: storage.type.primitive.cm
    - match: '\b[A-Z][A-Za-z0-9_]*\b'
      scope: entity.name.type.cm

  functions:
    - match: '\b([a-z_][a-zA-Z0-9_]*)\s*(?=\()'
      captures:
        1: entity.name.function.cm

  numbers:
    - match: '\b(0x[0-9a-fA-F]+|0b[01]+|0o[0-7]+|\d+\.?\d*)\b'
      scope: constant.numeric.cm
```

## IntelliJ IDEA Integration

### 1. プラグイン構造

```kotlin
// src/main/kotlin/com/cmlang/intellij/CmLanguage.kt
package com.cmlang.intellij

import com.intellij.lang.Language

object CmLanguage : Language("Cm") {
    override fun getDisplayName(): String = "Cm"
}

// src/main/kotlin/com/cmlang/intellij/CmFileType.kt
class CmFileType : LanguageFileType(CmLanguage) {
    override fun getName(): String = "Cm"
    override fun getDescription(): String = "Cm language file"
    override fun getDefaultExtension(): String = "cm"
    override fun getIcon(): Icon = CmIcons.FILE

    companion object {
        @JvmStatic
        val INSTANCE = CmFileType()
    }
}

// src/main/kotlin/com/cmlang/intellij/lsp/CmLspServerSupport.kt
class CmLspServerSupport : LspServerSupportProvider {
    override fun createLspServerDescriptor(
        project: Project,
        virtualFile: VirtualFile
    ): LspServerDescriptor {
        return CmLspServerDescriptor(project, virtualFile)
    }
}

class CmLspServerDescriptor(
    project: Project,
    private val root: VirtualFile
) : ProjectWideLspServerDescriptor(project, "Cm") {
    override fun isSupportedFile(file: VirtualFile): Boolean {
        return file.extension == "cm"
    }

    override fun createCommandLine(): GeneralCommandLine {
        return GeneralCommandLine("cm-lsp", "--stdio")
            .withWorkDirectory(root.path)
    }
}
```

### 2. plugin.xml

```xml
<idea-plugin>
    <id>com.cmlang.intellij</id>
    <name>Cm Language Support</name>
    <version>0.12.0</version>
    <vendor>Cm Language Team</vendor>

    <description><![CDATA[
        Official Cm language support for IntelliJ IDEA.
        Features:
        <ul>
            <li>Syntax highlighting</li>
            <li>Code completion</li>
            <li>Go to definition</li>
            <li>Find usages</li>
            <li>Refactoring</li>
            <li>Debugging</li>
        </ul>
    ]]></description>

    <depends>com.intellij.modules.platform</depends>
    <depends>com.redhat.devtools.lsp4ij</depends>

    <extensions defaultExtensionNs="com.intellij">
        <fileType
            name="Cm"
            implementationClass="com.cmlang.intellij.CmFileType"
            fieldName="INSTANCE"
            language="Cm"
            extensions="cm"/>

        <lang.parserDefinition
            language="Cm"
            implementationClass="com.cmlang.intellij.CmParserDefinition"/>

        <lang.syntaxHighlighterFactory
            language="Cm"
            implementationClass="com.cmlang.intellij.CmSyntaxHighlighterFactory"/>

        <completion.contributor
            language="Cm"
            implementationClass="com.cmlang.intellij.CmCompletionContributor"/>

        <lang.braceMatcher
            language="Cm"
            implementationClass="com.cmlang.intellij.CmBraceMatcher"/>

        <lang.commenter
            language="Cm"
            implementationClass="com.cmlang.intellij.CmCommenter"/>

        <lang.foldingBuilder
            language="Cm"
            implementationClass="com.cmlang.intellij.CmFoldingBuilder"/>

        <runConfigurationProducer
            implementation="com.cmlang.intellij.run.CmRunConfigurationProducer"/>

        <debugger.programRunner
            implementation="com.cmlang.intellij.debug.CmDebugRunner"/>

        <!-- LSP Support -->
        <lsp.serverSupportProvider
            implementation="com.cmlang.intellij.lsp.CmLspServerSupport"/>
    </extensions>

    <actions>
        <action id="Cm.NewFile"
                class="com.cmlang.intellij.actions.CreateCmFileAction"
                text="Cm File">
            <add-to-group group-id="NewGroup" anchor="after" relative-to-action="NewFile"/>
        </action>
    </actions>
</idea-plugin>
```

## Emacs Integration

### 1. cm-mode.el

```elisp
;;; cm-mode.el --- Major mode for Cm language -*- lexical-binding: t; -*-

(require 'lsp-mode)
(require 'dap-mode)
(require 'flycheck)

;;;###autoload
(define-derived-mode cm-mode prog-mode "Cm"
  "Major mode for editing Cm source code."
  :syntax-table cm-mode-syntax-table

  ;; コメント
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "//+\\s-*")

  ;; インデント
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width 4)
  (setq-local c-basic-offset 4)

  ;; フォントロック
  (setq font-lock-defaults '(cm-font-lock-keywords))

  ;; LSP
  (lsp-deferred))

;; シンタックステーブル
(defvar cm-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; コメント
    (modify-syntax-entry ?/ ". 124" table)
    (modify-syntax-entry ?* ". 23b" table)
    (modify-syntax-entry ?\n ">" table)

    ;; 文字列
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\' "\"" table)

    ;; 演算子
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?* "." table)
    (modify-syntax-entry ?/ "." table)
    (modify-syntax-entry ?% "." table)
    (modify-syntax-entry ?& "." table)
    (modify-syntax-entry ?| "." table)
    (modify-syntax-entry ?^ "." table)
    (modify-syntax-entry ?! "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)

    table)
  "Syntax table for `cm-mode'.")

;; フォントロック
(defvar cm-font-lock-keywords
  `(
    ;; キーワード
    (,(regexp-opt '("if" "else" "while" "for" "return" "break" "continue"
                    "match" "case" "default" "struct" "enum" "union"
                    "typedef" "impl" "trait" "with" "overload")
                  'words)
     . font-lock-keyword-face)

    ;; 型
    (,(regexp-opt '("void" "bool" "char" "int" "float" "double"
                    "int8" "int16" "int32" "int64"
                    "uint8" "uint16" "uint32" "uint64")
                  'words)
     . font-lock-type-face)

    ;; 修飾子
    (,(regexp-opt '("const" "static" "extern" "inline" "volatile" "restrict")
                  'words)
     . font-lock-builtin-face)

    ;; 関数名
    ("\\([a-z_][a-zA-Z0-9_]*\\)\\s-*(" 1 font-lock-function-name-face)

    ;; 型名（大文字開始）
    ("\\b[A-Z][A-Za-z0-9_]*\\b" . font-lock-type-face)

    ;; 定数（大文字）
    ("\\b[A-Z_][A-Z0-9_]*\\b" . font-lock-constant-face)

    ;; 数値
    ("\\b\\(0x[0-9a-fA-F]+\\|0b[01]+\\|0o[0-7]+\\|[0-9]+\\)\\b"
     . font-lock-constant-face))
  "Font lock keywords for `cm-mode'.")

;; LSP設定
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(cm-mode . "cm"))

  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection "cm-lsp")
    :activation-fn (lsp-activate-on "cm")
    :server-id 'cm-lsp
    :initialization-options
    (lambda ()
      `(:inlayHints (:enable t :typeHints t :parameterHints t)))
    :initialized-fn
    (lambda (workspace)
      (with-lsp-workspace workspace
        (lsp--set-configuration
         (lsp-configuration-section "cm")))))))

;; DAP設定
(with-eval-after-load 'dap-mode
  (dap-register-debug-template
   "Cm Debug"
   (list :type "cm"
         :request "launch"
         :name "Cm Debug"
         :program (read-file-name "Executable: ")
         :cwd (lsp-workspace-root)
         :stopOnEntry nil)))

;; Flycheck設定
(with-eval-after-load 'flycheck
  (flycheck-define-checker cm-lint
    "A Cm linter using cmlint."
    :command ("cmlint" source)
    :error-patterns
    ((error line-start (file-name) ":" line ":" column ": error: " (message) line-end)
     (warning line-start (file-name) ":" line ":" column ": warning: " (message) line-end))
    :modes cm-mode)

  (add-to-list 'flycheck-checkers 'cm-lint))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.cm\\'" . cm-mode))

(provide 'cm-mode)
;;; cm-mode.el ends here
```

## Vim Integration

### 1. ftplugin/cm.vim

```vim
" Vim filetype plugin for Cm
" Language: Cm
" Maintainer: Cm Language Team

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

" 基本設定
setlocal comments=://
setlocal commentstring=//\ %s
setlocal formatoptions-=t formatoptions+=croql

" インデント
setlocal expandtab
setlocal shiftwidth=4
setlocal softtabstop=4
setlocal tabstop=4

" パス設定
setlocal include=^\\s*import\\s*
setlocal suffixesadd=.cm

" マッピング
nnoremap <buffer> <silent> gd :CmGotoDefinition<CR>
nnoremap <buffer> <silent> K :CmHover<CR>
nnoremap <buffer> <silent> <leader>rn :CmRename<CR>
nnoremap <buffer> <silent> <leader>ca :CmCodeAction<CR>
nnoremap <buffer> <silent> <leader>f :CmFormat<CR>

" ALE設定
let b:ale_linters = ['cm-lsp', 'cmlint']
let b:ale_fixers = ['cmfmt']

" CoC設定
if exists('*CocAction')
  augroup cm_coc
    autocmd!
    autocmd CursorHold *.cm silent call CocActionAsync('doHover')
    autocmd User CocJumpPlaceholder call CocActionAsync('showSignatureHelp')
  augroup END
endif
```

### 2. coc-settings.json

```json
{
  "languageserver": {
    "cm": {
      "command": "cm-lsp",
      "args": ["--stdio"],
      "filetypes": ["cm"],
      "rootPatterns": ["Cm.toml", ".git"],
      "initializationOptions": {
        "inlayHints": {
          "enable": true,
          "typeHints": true,
          "parameterHints": true
        }
      },
      "settings": {
        "cm": {
          "format": {
            "indentSize": 4
          }
        }
      }
    }
  },
  "cm.trace.server": "verbose"
}
```

## クロスエディタ機能

### 共通LSP機能マトリックス

| 機能 | VSCode | Neovim | Sublime | IntelliJ | Emacs | Vim |
|------|--------|--------|---------|----------|-------|-----|
| 補完 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ホバー | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 定義ジャンプ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 参照検索 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| リネーム | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| フォーマット | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| コードアクション | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| インレイヒント | ✅ | ✅ | ⚠️ | ✅ | ⚠️ | ⚠️ |
| セマンティックトークン | ✅ | ✅ | ⚠️ | ✅ | ⚠️ | ⚠️ |
| デバッグ | ✅ | ✅ | ⚠️ | ✅ | ✅ | ⚠️ |

✅: 完全サポート, ⚠️: 部分サポート/プラグイン依存

## テスト戦略

### エディタ統合テスト

```typescript
// test/integration/editor-test.ts
import { describe, it, expect } from '@jest/globals';
import { TestClient } from './test-client';

describe('Editor Integration Tests', () => {
    const clients = [
        new VSCodeTestClient(),
        new NeovimTestClient(),
        new SublimeTestClient(),
        // ...
    ];

    for (const client of clients) {
        describe(`${client.name} Integration`, () => {
            it('should provide completions', async () => {
                await client.openFile('test.cm');
                await client.typeText('struct Point { int x; int y; }\nPoint p; p.');

                const completions = await client.getCompletions();
                expect(completions).toContainEqual(
                    expect.objectContaining({ label: 'x' })
                );
                expect(completions).toContainEqual(
                    expect.objectContaining({ label: 'y' })
                );
            });

            it('should navigate to definition', async () => {
                await client.openFile('test.cm');
                const location = await client.gotoDefinition(5, 10);
                expect(location.line).toBe(1);
            });

            // More tests...
        });
    }
});
```

## パフォーマンス最適化

### 1. 非同期処理

```rust
// LSPサーバー側の最適化
impl LanguageServer for CmLanguageServer {
    async fn completion(&self, params: CompletionParams) -> Result<CompletionResponse> {
        // 高優先度タスクを即座に返す
        let quick_completions = self.get_quick_completions(&params)?;

        // 詳細な補完は非同期で計算
        let detailed_completions = tokio::spawn(async move {
            self.get_detailed_completions(&params).await
        });

        // クイック結果を即座に返す
        Ok(CompletionResponse::Array(quick_completions))
    }
}
```

### 2. キャッシュ戦略

```typescript
// クライアント側のキャッシュ
class CompletionCache {
    private cache = new Map<string, CompletionItem[]>();
    private maxAge = 5000; // 5秒

    async get(key: string, fetcher: () => Promise<CompletionItem[]>): Promise<CompletionItem[]> {
        const cached = this.cache.get(key);
        if (cached && this.isValid(cached)) {
            return cached.items;
        }

        const items = await fetcher();
        this.cache.set(key, { items, timestamp: Date.now() });
        return items;
    }
}
```

## 実装優先順位

1. **Phase 1** (v0.12.0): VSCode、Neovim基本サポート
2. **Phase 2** (v0.13.0): その他エディタ対応、高度な機能
3. **Phase 3** (v0.14.0): クラウドIDE、Web エディタ対応