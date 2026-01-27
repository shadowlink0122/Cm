# Gen - Cm Package Management System
**Date**: 2026-01-15
**Version**: v0.12.0

## 概要

`gen`はCm言語の軽量で高速なパッケージ管理システムです。Cmコンパイラのインストール管理と、GitHubからの直接的なパッケージ取得を中心に設計されています。

## 設計理念

1. **シンプルさ優先**: 複雑な依存解決よりも直接的なパッケージ管理
2. **GitHub中心**: GitHubをメインのパッケージソースとして活用
3. **高速インストール**: 並列ダウンロード、バイナリ配布優先
4. **ゼロ設定**: 追加設定なしで即使用可能
5. **クロスプラットフォーム**: macOS、Linux、Windows完全対応

## アーキテクチャ

### システム構成

```
┌────────────────────────────────────────────┐
│                gen CLI                      │
├───────────┬──────────┬─────────┬──────────┤
│  Cm       │  Package │  GitHub │  Cache   │
│  Installer│  Manager │  Client │  Manager │
├───────────┴──────────┴─────────┴──────────┤
│           Dependency Resolver              │
├────────────────────────────────────────────┤
│            Version Manager                 │
└────────────────────────────────────────────┘
```

### ディレクトリ構造

```
~/.gen/
├── bin/                    # gen実行ファイル
│   └── gen
├── cm/                     # Cmコンパイラ
│   ├── current -> 0.12.0  # 現在のバージョン
│   └── versions/
│       ├── 0.11.0/
│       └── 0.12.0/
├── packages/               # インストール済みパッケージ
│   ├── github.com/
│   │   └── user/
│   │       └── repo/
│   │           ├── manifest.json
│   │           └── src/
│   └── registry/          # 公式レジストリキャッシュ
├── cache/                  # ダウンロードキャッシュ
└── config.toml            # グローバル設定
```

## コマンド体系

### 基本コマンド

```bash
# Gen自体のインストール（Homebrew）
brew install gen

# Gen自体のインストール（APT）
sudo apt install gen

# Cmコンパイラのインストール
gen install cm              # 最新安定版
gen install cm@0.12.0       # 特定バージョン
gen install cm@latest       # 最新版
gen install cm@nightly      # ナイトリービルド

# Cmバージョン管理
gen use cm@0.12.0          # バージョン切り替え
gen list cm                 # インストール済みバージョン一覧
gen update cm              # 最新版に更新
gen uninstall cm@0.11.0    # 特定バージョンを削除

# パッケージ管理
gen add user/repo          # GitHubからパッケージを追加
gen add user/repo@v1.2.3   # 特定バージョン/タグ
gen add user/repo@main     # 特定ブランチ
gen add https://github.com/user/repo.git  # URL指定

# ローカルパッケージ
gen add ./local-package    # ローカルディレクトリ
gen link ./dev-package     # 開発用シンボリックリンク

# パッケージ操作
gen remove user/repo       # パッケージを削除
gen update                 # 全パッケージを更新
gen update user/repo       # 特定パッケージを更新
gen list                   # インストール済み一覧
gen info user/repo         # パッケージ情報表示

# 検索
gen search keyword         # パッケージ検索
gen trending               # トレンドパッケージ表示

# プロジェクト初期化
gen init                   # 新規プロジェクト作成
gen init --lib            # ライブラリプロジェクト
gen init --bin            # 実行可能プロジェクト
```

## プロジェクト設定ファイル

### gen.toml

```toml
[package]
name = "my-project"
version = "0.1.0"
authors = ["Your Name <email@example.com>"]
description = "A Cm project"
license = "MIT"
repository = "https://github.com/user/my-project"

[dependencies]
# GitHub パッケージ
"user/http-server" = "1.2.3"
"organization/json-parser" = { version = "2.0", features = ["streaming"] }

# Git URL
"custom-lib" = { git = "https://github.com/user/custom-lib.git", tag = "v1.0.0" }

# ローカルパス
"local-lib" = { path = "../local-lib" }

# 公式レジストリ（将来対応）
"std-extension" = { registry = "official", version = "1.0" }

[dev-dependencies]
"user/test-framework" = "3.0.0"

[build]
# ビルド設定
output = "bin/my-app"
flags = ["-O3", "-DRELEASE"]

[scripts]
# カスタムスクリプト
test = "cm test tests/*.cm"
build = "cm build -o bin/my-app src/main.cm"
run = "bin/my-app"
clean = "rm -rf bin/ target/"
```

## GitHub統合

### パッケージ取得フロー

```rust
// GitHub パッケージマネージャー
pub struct GitHubPackageManager {
    client: GitHubClient,
    cache: PackageCache,
}

impl GitHubPackageManager {
    pub async fn fetch_package(&self, spec: &PackageSpec) -> Result<Package> {
        // 1. パッケージ仕様を解析
        let (owner, repo, version) = self.parse_spec(spec)?;

        // 2. GitHubからメタデータ取得
        let metadata = self.fetch_metadata(&owner, &repo).await?;

        // 3. バージョン解決
        let resolved_version = self.resolve_version(&metadata, &version)?;

        // 4. キャッシュチェック
        if let Some(cached) = self.cache.get(&owner, &repo, &resolved_version) {
            return Ok(cached);
        }

        // 5. パッケージダウンロード
        let package = self.download_package(&owner, &repo, &resolved_version).await?;

        // 6. 検証とインストール
        self.verify_package(&package)?;
        self.install_package(package).await?;

        Ok(package)
    }

    async fn fetch_metadata(&self, owner: &str, repo: &str) -> Result<RepoMetadata> {
        // GitHub API v3/v4 を使用
        let url = format!("https://api.github.com/repos/{}/{}", owner, repo);
        let response = self.client.get(&url).await?;

        // gen.toml を探す
        let gen_toml = self.client.get_file_content(owner, repo, "gen.toml").await
            .or_else(|_| self.client.get_file_content(owner, repo, "Cm.toml")).await?;

        Ok(RepoMetadata {
            stars: response.stargazers_count,
            description: response.description,
            manifest: toml::from_str(&gen_toml)?,
        })
    }
}
```

### GitHubアクション統合

```yaml
# .github/workflows/gen-publish.yml
name: Publish to Gen Registry

on:
  release:
    types: [created]

jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install gen
        run: |
          curl -sSf https://gen.cm-lang.org/install.sh | sh

      - name: Build package
        run: gen build --release

      - name: Publish package
        run: gen publish
        env:
          GEN_TOKEN: ${{ secrets.GEN_TOKEN }}
```

## Cmコンパイラ管理

### バージョン管理システム

```rust
pub struct CmVersionManager {
    versions_dir: PathBuf,
    current_version: Option<Version>,
}

impl CmVersionManager {
    pub async fn install_cm(&mut self, version_spec: &str) -> Result<()> {
        let version = self.resolve_version(version_spec).await?;

        // プラットフォーム検出
        let platform = detect_platform()?;

        // ダウンロードURL構築
        let url = format!(
            "https://github.com/cm-lang/cm/releases/download/v{}/cm-{}.tar.gz",
            version, platform
        );

        // ダウンロードと展開
        let archive = download_file(&url).await?;
        let install_dir = self.versions_dir.join(version.to_string());
        extract_archive(&archive, &install_dir)?;

        // 実行権限設定
        set_executable(&install_dir.join("bin/cm"))?;

        // デフォルトに設定
        self.set_current(version)?;

        Ok(())
    }

    pub fn set_current(&mut self, version: Version) -> Result<()> {
        let current_link = self.versions_dir.join("current");

        // 既存のシンボリックリンクを削除
        if current_link.exists() {
            std::fs::remove_file(&current_link)?;
        }

        // 新しいシンボリックリンクを作成
        let version_dir = self.versions_dir.join(version.to_string());
        std::os::unix::fs::symlink(&version_dir, &current_link)?;

        self.current_version = Some(version);

        Ok(())
    }
}
```

## インストーラー実装

### Homebrew Formula

```ruby
# Formula/gen.rb
class Gen < Formula
  desc "Package manager for the Cm programming language"
  homepage "https://gen.cm-lang.org"
  version "1.0.0"
  license "MIT"

  if OS.mac? && Hardware::CPU.intel?
    url "https://github.com/cm-lang/gen/releases/download/v1.0.0/gen-x86_64-apple-darwin.tar.gz"
    sha256 "abc123..."
  elsif OS.mac? && Hardware::CPU.arm?
    url "https://github.com/cm-lang/gen/releases/download/v1.0.0/gen-aarch64-apple-darwin.tar.gz"
    sha256 "def456..."
  elsif OS.linux? && Hardware::CPU.intel?
    url "https://github.com/cm-lang/gen/releases/download/v1.0.0/gen-x86_64-unknown-linux-gnu.tar.gz"
    sha256 "ghi789..."
  end

  def install
    bin.install "gen"

    # Bash completion
    bash_completion.install "completions/gen.bash"

    # Zsh completion
    zsh_completion.install "completions/_gen"

    # Fish completion
    fish_completion.install "completions/gen.fish"

    # Man page
    man1.install "doc/gen.1"
  end

  def post_install
    # 初期設定
    system "#{bin}/gen", "setup"
  end

  test do
    system "#{bin}/gen", "--version"
    system "#{bin}/gen", "list"
  end
end
```

### APTパッケージ

```bash
#!/bin/bash
# debian/postinst - パッケージインストール後スクリプト

set -e

case "$1" in
    configure)
        # genユーザーの作成（必要な場合）
        if ! getent passwd gen >/dev/null; then
            adduser --system --group --home /var/lib/gen gen
        fi

        # ディレクトリ作成
        mkdir -p /var/lib/gen/cache
        mkdir -p /etc/gen

        # パーミッション設定
        chown -R gen:gen /var/lib/gen
        chmod 755 /usr/bin/gen

        # 初期設定
        gen setup --system

        # systemdサービス再読み込み（必要な場合）
        if [ -d /run/systemd/system ]; then
            systemctl daemon-reload
        fi
        ;;

    abort-upgrade|abort-remove|abort-deconfigure)
        ;;

    *)
        echo "postinst called with unknown argument '$1'" >&2
        exit 1
        ;;
esac

exit 0
```

### debian/control

```
Source: gen
Section: devel
Priority: optional
Maintainer: Cm Language Team <team@cm-lang.org>
Build-Depends: debhelper-compat (= 13),
               cargo,
               rustc (>= 1.70),
               libssl-dev,
               pkg-config
Standards-Version: 4.6.0
Homepage: https://gen.cm-lang.org
Vcs-Browser: https://github.com/cm-lang/gen
Vcs-Git: https://github.com/cm-lang/gen.git

Package: gen
Architecture: any
Depends: ${shlibs:Depends},
         ${misc:Depends},
         git,
         curl | wget
Recommends: cm
Suggests: build-essential
Description: Package manager for the Cm programming language
 Gen is a fast and lightweight package manager for Cm. It handles
 installing the Cm compiler and managing packages from GitHub.
 .
 Features:
  - Install and manage multiple Cm compiler versions
  - Download packages directly from GitHub
  - Simple dependency management
  - Cross-platform support
```

## キャッシュとパフォーマンス

### インテリジェントキャッシュ

```rust
pub struct CacheManager {
    cache_dir: PathBuf,
    max_cache_size: u64,
    cache_ttl: Duration,
}

impl CacheManager {
    pub fn get(&self, key: &str) -> Option<Vec<u8>> {
        let cache_path = self.cache_dir.join(key);

        if !cache_path.exists() {
            return None;
        }

        // TTLチェック
        if let Ok(metadata) = cache_path.metadata() {
            if let Ok(modified) = metadata.modified() {
                if modified.elapsed().unwrap_or(Duration::MAX) > self.cache_ttl {
                    // 期限切れ
                    let _ = std::fs::remove_file(&cache_path);
                    return None;
                }
            }
        }

        std::fs::read(&cache_path).ok()
    }

    pub fn put(&self, key: &str, data: &[u8]) -> Result<()> {
        // キャッシュサイズチェック
        self.ensure_cache_size()?;

        let cache_path = self.cache_dir.join(key);
        if let Some(parent) = cache_path.parent() {
            std::fs::create_dir_all(parent)?;
        }

        std::fs::write(cache_path, data)?;
        Ok(())
    }

    fn ensure_cache_size(&self) -> Result<()> {
        let current_size = self.calculate_cache_size()?;

        if current_size > self.max_cache_size {
            // LRU削除
            self.evict_lru(current_size - self.max_cache_size)?;
        }

        Ok(())
    }
}
```

### 並列ダウンロード

```rust
pub async fn download_packages_parallel(packages: Vec<PackageSpec>) -> Result<Vec<Package>> {
    use futures::future::join_all;

    let download_tasks = packages.into_iter().map(|spec| {
        async move {
            download_package(spec).await
        }
    });

    let results = join_all(download_tasks).await;

    // エラーチェック
    let mut packages = Vec::new();
    for result in results {
        packages.push(result?);
    }

    Ok(packages)
}
```

## セキュリティ

### パッケージ検証

```rust
pub struct PackageVerifier {
    trusted_keys: Vec<PublicKey>,
}

impl PackageVerifier {
    pub fn verify_package(&self, package: &Package) -> Result<()> {
        // 1. チェックサム検証
        let computed_hash = sha256::digest(&package.data);
        if computed_hash != package.expected_hash {
            return Err(Error::ChecksumMismatch);
        }

        // 2. 署名検証（オプション）
        if let Some(signature) = &package.signature {
            let verified = self.trusted_keys.iter().any(|key| {
                ed25519::verify(signature, &package.data, key).is_ok()
            });

            if !verified {
                return Err(Error::InvalidSignature);
            }
        }

        // 3. マルウェアスキャン（基本的なチェック）
        if contains_suspicious_patterns(&package.data) {
            return Err(Error::SuspiciousContent);
        }

        Ok(())
    }
}
```

## CLI実装

### Rustによる実装

```rust
// src/main.rs
use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "gen")]
#[command(about = "Package manager for Cm", version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    #[arg(global = true, long, short)]
    verbose: bool,

    #[arg(global = true, long)]
    config: Option<PathBuf>,
}

#[derive(Subcommand)]
enum Commands {
    /// Install Cm compiler or packages
    Install {
        package: String,
        #[arg(long)]
        global: bool,
    },

    /// Add a package dependency
    Add {
        package: String,
        #[arg(long)]
        dev: bool,
    },

    /// Remove a package
    Remove {
        package: String,
    },

    /// Update packages
    Update {
        package: Option<String>,
    },

    /// List installed packages
    List {
        #[arg(long)]
        global: bool,
    },

    /// Search for packages
    Search {
        query: String,
    },

    /// Initialize a new project
    Init {
        #[arg(long)]
        lib: bool,
        #[arg(long)]
        bin: bool,
        name: Option<String>,
    },

    /// Use a specific Cm version
    Use {
        version: String,
    },
}

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();

    // ログ設定
    if cli.verbose {
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("debug")).init();
    } else {
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
    }

    // コマンド実行
    match cli.command {
        Commands::Install { package, global } => {
            if package.starts_with("cm") {
                install_cm(&package).await?;
            } else {
                install_package(&package, global).await?;
            }
        }
        Commands::Add { package, dev } => {
            add_dependency(&package, dev).await?;
        }
        Commands::Remove { package } => {
            remove_package(&package).await?;
        }
        Commands::Update { package } => {
            if let Some(pkg) = package {
                update_package(&pkg).await?;
            } else {
                update_all().await?;
            }
        }
        Commands::List { global } => {
            list_packages(global).await?;
        }
        Commands::Search { query } => {
            search_packages(&query).await?;
        }
        Commands::Init { lib, bin, name } => {
            init_project(lib, bin, name).await?;
        }
        Commands::Use { version } => {
            use_cm_version(&version).await?;
        }
    }

    Ok(())
}
```

## プロジェクトテンプレート

### gen init による生成

```rust
pub async fn init_project(is_lib: bool, is_bin: bool, name: Option<String>) -> Result<()> {
    let project_name = name.unwrap_or_else(|| {
        std::env::current_dir()
            .ok()
            .and_then(|p| p.file_name().map(|n| n.to_string_lossy().to_string()))
            .unwrap_or_else(|| "my_project".to_string())
    });

    // gen.toml作成
    let gen_toml = format!(r#"[package]
name = "{}"
version = "0.1.0"
authors = ["Your Name <you@example.com>"]
description = "A new Cm project"
license = "MIT"

[dependencies]

[dev-dependencies]

[scripts]
test = "cm test tests/*.cm"
build = "cm build -o bin/{} src/main.cm"
run = "bin/{}"
"#, project_name, project_name, project_name);

    std::fs::write("gen.toml", gen_toml)?;

    // ディレクトリ構造作成
    std::fs::create_dir_all("src")?;
    std::fs::create_dir_all("tests")?;

    // main.cm または lib.cm作成
    if is_lib {
        std::fs::write("src/lib.cm", r#"// Library entry point

pub int add(int a, int b) {
    return a + b;
}
"#)?;
    } else {
        std::fs::write("src/main.cm", r#"// Application entry point

int main(int argc, char** argv) {
    println("Hello, Cm!");
    return 0;
}
"#)?;
    }

    // .gitignore作成
    std::fs::write(".gitignore", r#"# Build outputs
/bin/
/target/
*.o
*.a
*.so
*.dylib

# Gen
/.gen/
/gen.lock

# IDE
.vscode/
.idea/
*.swp
*.swo
*~

# OS
.DS_Store
Thumbs.db
"#)?;

    // README.md作成
    std::fs::write("README.md", format!(r#"# {}

A new Cm project.

## Build

```bash
gen build
```

## Run

```bash
gen run
```

## Test

```bash
gen test
```
"#, project_name))?;

    println!("✨ Created new Cm project: {}", project_name);
    println!("   Run 'gen build' to build the project");

    Ok(())
}
```

## 今後の拡張計画

### Phase 1 (v1.0.0) - 基本機能
- Cmコンパイラのインストール/管理
- GitHubからのパッケージ取得
- 基本的な依存管理
- Homebrew/APTサポート

### Phase 2 (v1.1.0) - 拡張機能
- プライベートリポジトリサポート
- パッケージ署名と検証
- バージョン制約の高度な解決
- ワークスペース機能

### Phase 3 (v1.2.0) - エコシステム統合
- 公式レジストリ立ち上げ
- パッケージ公開機能
- CI/CD統合
- IDE統合強化