# Cm Version Management Tool Design
**Date**: 2026-01-14
**Version**: v0.12.0

## 概要

`cmup`はCm言語の公式バージョン管理ツールです。複数バージョンのCmコンパイラを管理し、プロジェクトごとに適切なバージョンを自動的に選択します。

## 設計目標

1. **簡単なインストール**: ワンライナーでインストール可能
2. **自動バージョン切り替え**: プロジェクトディレクトリに応じた自動切り替え
3. **ツールチェーン管理**: コンパイラ、リンター、フォーマッタの統一管理
4. **クロスプラットフォーム**: macOS、Linux、Windows対応
5. **オフライン対応**: ローカルキャッシュによる高速切り替え

## アーキテクチャ

### ディレクトリ構造

```
~/.cmup/
├── bin/                      # シンボリックリンク
│   ├── cm -> current/cm
│   ├── cmfmt -> current/cmfmt
│   └── cmlint -> current/cmlint
├── versions/                 # インストール済みバージョン
│   ├── 0.11.0/
│   │   ├── cm
│   │   ├── cmfmt
│   │   ├── cmlint
│   │   └── manifest.json
│   └── 0.12.0/
├── downloads/               # ダウンロードキャッシュ
├── current -> versions/0.12.0/  # 現在のバージョン
└── config.toml             # グローバル設定
```

### コンポーネント

```
┌─────────────────────────────────────────────┐
│                   cmup CLI                    │
├───────────────┬───────────────┬──────────────┤
│  Version      │   Toolchain   │   Config     │
│  Manager      │   Installer   │   Manager    │
├───────────────┴───────────────┴──────────────┤
│              Version Resolver                 │
├───────────────────────────────────────────────┤
│           Download/Cache Manager              │
└───────────────────────────────────────────────┘
```

## 主要機能

### 1. バージョン管理

```bash
# 最新安定版をインストール
cmup install stable

# 特定バージョンをインストール
cmup install 0.11.0

# ナイトリービルドをインストール
cmup install nightly

# インストール済みバージョンの一覧
cmup list

# デフォルトバージョンの設定
cmup default 0.12.0

# 現在のバージョンを表示
cmup version
```

### 2. プロジェクトローカル設定

`.cmversion`ファイルによるプロジェクトごとのバージョン指定：

```toml
# .cmversion
version = "0.11.0"
channel = "stable"  # stable, beta, nightly

[toolchain]
components = ["cm", "cmfmt", "cmlint", "cm-docs"]
targets = ["x86_64-apple-darwin", "wasm32-unknown"]
```

### 3. 自動バージョン切り替え

```rust
// Version Resolver の実装
struct VersionResolver {
    fn resolve_version(&self, cwd: &Path) -> Version {
        // 1. .cmversionファイルを探索（親ディレクトリも含む）
        if let Some(local_version) = find_local_version(cwd) {
            return local_version;
        }

        // 2. CMUP_VERSION環境変数をチェック
        if let Some(env_version) = env::var("CMUP_VERSION") {
            return Version::parse(env_version);
        }

        // 3. デフォルトバージョンを使用
        return self.default_version;
    }
}
```

### 4. ツールチェーンコンポーネント

```toml
# manifest.json - 各バージョンのメタデータ
{
  "version": "0.12.0",
  "date": "2026-01-20",
  "components": {
    "cm": {
      "binary": "cm",
      "version": "0.12.0",
      "features": ["jit", "native", "wasm"]
    },
    "cmfmt": {
      "binary": "cmfmt",
      "version": "0.12.0"
    },
    "cmlint": {
      "binary": "cmlint",
      "version": "0.12.0"
    },
    "cm-lsp": {
      "binary": "cm-lsp",
      "version": "0.12.0"
    }
  },
  "targets": [
    "x86_64-apple-darwin",
    "x86_64-unknown-linux-gnu",
    "aarch64-apple-darwin",
    "wasm32-unknown"
  ]
}
```

## インストールプロセス

### 1. ワンライナーインストール

```bash
# macOS/Linux
curl -sSf https://cm-lang.org/install.sh | sh

# Windows PowerShell
iwr https://cm-lang.org/install.ps1 -useb | iex
```

### 2. インストールスクリプト

```bash
#!/bin/bash
# install.sh

set -e

CMUP_HOME="${CMUP_HOME:-$HOME/.cmup}"
CMUP_BIN="$CMUP_HOME/bin"

# OS/アーキテクチャの検出
detect_platform() {
    local os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local arch=$(uname -m)

    case "$arch" in
        x86_64) arch="x86_64" ;;
        arm64|aarch64) arch="aarch64" ;;
        *) echo "Unsupported architecture: $arch"; exit 1 ;;
    esac

    case "$os" in
        darwin) os="apple-darwin" ;;
        linux) os="unknown-linux-gnu" ;;
        *) echo "Unsupported OS: $os"; exit 1 ;;
    esac

    echo "${arch}-${os}"
}

# cmupのダウンロードとインストール
install_cmup() {
    local platform=$(detect_platform)
    local url="https://github.com/cm-lang/cmup/releases/latest/download/cmup-${platform}"

    echo "Installing cmup for $platform..."

    mkdir -p "$CMUP_BIN"
    curl -sSfL "$url" -o "$CMUP_BIN/cmup"
    chmod +x "$CMUP_BIN/cmup"

    # 最新安定版のCmをインストール
    "$CMUP_BIN/cmup" install stable
}

# PATHの設定
setup_path() {
    local shell_rc=""

    case "$SHELL" in
        */bash) shell_rc="$HOME/.bashrc" ;;
        */zsh) shell_rc="$HOME/.zshrc" ;;
        */fish) shell_rc="$HOME/.config/fish/config.fish" ;;
        *) shell_rc="$HOME/.profile" ;;
    esac

    echo "export PATH=\"\$HOME/.cmup/bin:\$PATH\"" >> "$shell_rc"
    echo "Please run: source $shell_rc"
}

# メイン処理
main() {
    install_cmup
    setup_path

    echo "✅ cmup installed successfully!"
    echo "Run 'cmup help' to get started"
}

main
```

## アップデート戦略

### 1. セルフアップデート

```bash
# cmup自体のアップデート
cmup self update

# 自動アップデートの設定
cmup config auto-update true
```

### 2. バージョンチャンネル

- **stable**: 安定版リリース（推奨）
- **beta**: ベータ版（次期リリースのテスト）
- **nightly**: ナイトリービルド（最新機能）
- **custom**: カスタムビルド（開発者向け）

### 3. ロールバック機能

```bash
# 前のバージョンに戻す
cmup rollback

# 特定バージョンの削除
cmup uninstall 0.10.0
```

## セキュリティ

### 1. 署名検証

```rust
// バイナリの署名検証
impl SignatureVerifier {
    fn verify_binary(&self, binary: &[u8], signature: &[u8]) -> Result<()> {
        // Ed25519による署名検証
        let public_key = self.get_public_key()?;
        ed25519::verify(signature, binary, &public_key)?;
        Ok(())
    }
}
```

### 2. チェックサム検証

```toml
# releases.toml
[[releases]]
version = "0.12.0"
date = "2026-01-20"

[releases.checksums]
"x86_64-apple-darwin" = "sha256:abc123..."
"x86_64-unknown-linux-gnu" = "sha256:def456..."
```

## プロキシサポート

```bash
# HTTPプロキシの設定
cmup config proxy.http "http://proxy.example.com:8080"
cmup config proxy.https "https://proxy.example.com:8080"

# プロキシ認証
cmup config proxy.user "username"
cmup config proxy.password "password"
```

## CI/CD統合

### GitHub Actions

```yaml
# .github/workflows/ci.yml
- name: Install Cm
  uses: cm-lang/setup-cm@v1
  with:
    version: '0.12.0'
    components: 'cm,cmfmt,cmlint'
```

### Docker

```dockerfile
FROM cm-lang/cm:0.12.0 as builder
COPY . /app
WORKDIR /app
RUN cm build --release
```

## エラーハンドリング

```rust
enum CmupError {
    VersionNotFound(String),
    NetworkError(String),
    CorruptedBinary(String),
    InsufficientPermissions(String),
    PlatformNotSupported(String),
}

impl CmupError {
    fn user_message(&self) -> String {
        match self {
            Self::VersionNotFound(v) =>
                format!("Version {} not found. Run 'cmup list-remote' to see available versions", v),
            Self::NetworkError(e) =>
                format!("Network error: {}. Check your internet connection or proxy settings", e),
            // ...
        }
    }
}
```

## 実装スケジュール

1. **Phase 1** (v0.12.0): 基本的なバージョン管理
2. **Phase 2** (v0.13.0): ツールチェーン管理、自動更新
3. **Phase 3** (v0.14.0): CI/CD統合、エンタープライズ機能

## テスト戦略

```rust
#[cfg(test)]
mod tests {
    #[test]
    fn test_version_resolution() {
        // .cmversionファイルの優先順位テスト
    }

    #[test]
    fn test_platform_detection() {
        // OS/アーキテクチャ検出のテスト
    }

    #[test]
    fn test_signature_verification() {
        // バイナリ署名検証のテスト
    }
}
```