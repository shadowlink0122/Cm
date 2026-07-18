---
title: Configuration File
parent: Compiler
nav_order: 3
---

# Configuration File (.cmconfig.yml)

`.cmconfig.yml` is the per-project compiler configuration file. It is searched from the current directory upwards through parent directories, and the first one found is used.

## Overview of settings

```yaml
# .cmconfig.yml
language: en          # Message language (en | ja; default: en)

compile:
  optimization: 2     # Default optimization level when -O is not given (0-3)
  target: native      # Default target when --target is not given (native/sv/js, etc.)

lint:
  preset: recommended # Rule preset (minimal | recommended | strict)
  rules:
    W001: disabled    # Per-rule level override (error | warning | hint | disabled)
  exclude:
    - tests/fixtures/ # Paths excluded from cm check/lint/fmt -r directory scans
```

## Message language (language)

All compiler messages (help, progress, diagnostic framing) default to English. To switch to Japanese, use any of the following (higher entries take precedence):

1. CLI option: `cm check --lang=ja main.cm`
2. Environment variable: `CM_LANG=ja cm check main.cm`
3. `.cmconfig.yml`: `language: ja`

```console
$ cm check bad.cm
=== Check complete ===
files: 1/1
errors: 1, warnings: 0

$ cm check bad.cm --lang=ja
=== チェック完了 ===
ファイル数: 1/1
エラー: 1, 警告: 0
```

## Compile defaults (compile)

Project-wide defaults can be fixed in the config so you do not have to pass CLI options every time. Values given explicitly on the CLI (`-O<n>` / `--target=`) always take precedence over the config.

```yaml
compile:
  optimization: 0   # Default to -O0 for debug-focused projects
  target: sv        # Default to the SV target for FPGA projects
```

- `optimization` accepts only integers 0-3. Invalid values are ignored and the built-in default (-O3) is used
- `target` takes the same values as `--target=` (native/wasm/js/web/sv/uefi, etc.)

## Lint rule settings (lint)

- `preset` switches the default level of the rule set at once (`minimal` = all disabled / `recommended` = warning / `strict` = error)
- `rules` overrides individual rules (`L001: disabled`, etc.). For per-line suppression, use the in-source comment `// @cm-disable-next-line L001`
- Paths listed under `exclude` are skipped by `cm check/lint/fmt -r` directory scans. Explicitly specified files are not affected by exclusion

See [Linter](linter.html) for details on exclusion settings.

---

<!-- nav -->
← Prev: [Formatter (cm fmt)](formatter.html) | [Contents](../index.html) | Next: [MIR最適化パス](optimization.html) →
