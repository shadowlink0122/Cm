# Cm Language Installer Design
**Date**: 2026-01-14
**Version**: v0.12.0

## 概要

Cm言語のマルチプラットフォームインストーラーシステムの設計。ワンクリック/ワンライナーで完全なCm開発環境を構築します。

## 設計目標

1. **シンプルなインストール体験**: 技術レベルを問わず誰でも使える
2. **完全な環境構築**: コンパイラ、ツール、IDE拡張を一括インストール
3. **オフライン対応**: オフラインインストーラーの提供
4. **エンタープライズ対応**: MSI、RPM、DEBパッケージのサポート
5. **自動アップデート**: セキュリティ更新の自動適用

## インストール方式

### 1. Webインストーラー（推奨）

```
┌─────────────────────────────────────┐
│        Web Installer (軽量)          │
├─────────────────────────────────────┤
│  1. Platform Detection              │
│  2. Download Components             │
│  3. Install & Configure            │
│  4. Verify Installation            │
└─────────────────────────────────────┘
```

**特徴**:
- ファイルサイズ: ~2MB
- 最新版を自動ダウンロード
- 必要なコンポーネントのみ選択可能

### 2. オフラインインストーラー

```
┌─────────────────────────────────────┐
│     Offline Installer (完全版)       │
├─────────────────────────────────────┤
│  - Cm Compiler                      │
│  - Standard Library                 │
│  - cpm, cmfmt, cmlint             │
│  - Documentation                    │
│  - Examples                        │
└─────────────────────────────────────┘
```

**特徴**:
- ファイルサイズ: ~200MB
- インターネット接続不要
- 企業環境向け

## プラットフォーム別実装

### macOS

#### 1. Homebrew Formula

```ruby
# Formula/cm.rb
class Cm < Formula
  desc "The Cm Programming Language"
  homepage "https://cm-lang.org"
  version "0.12.0"

  if Hardware::CPU.intel?
    url "https://github.com/cm-lang/cm/releases/download/v0.12.0/cm-x86_64-apple-darwin.tar.gz"
    sha256 "abc123..."
  else
    url "https://github.com/cm-lang/cm/releases/download/v0.12.0/cm-aarch64-apple-darwin.tar.gz"
    sha256 "def456..."
  end

  depends_on "llvm@17"

  def install
    bin.install "cm", "cpm", "cmfmt", "cmlint"
    lib.install Dir["lib/*"]
    include.install Dir["include/*"]

    # Install stdlib
    (share/"cm/stdlib").install Dir["stdlib/*"]

    # Install VSCode extension
    system "code", "--install-extension", "cm-lang.vscode-cm" if which("code")
  end

  def caveats
    <<~EOS
      Cm has been installed!

      Get started with:
        cm --version
        cpm new hello_world

      Documentation:
        https://docs.cm-lang.org
    EOS
  end

  test do
    system "#{bin}/cm", "--version"
  end
end
```

#### 2. PKG インストーラー（GUI）

```xml
<!-- Distribution.xml -->
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2.0">
    <title>Cm Programming Language</title>
    <welcome file="welcome.html"/>
    <license file="LICENSE"/>
    <readme file="README.html"/>

    <options customize="allow" require-scripts="false"/>

    <choices-outline>
        <line choice="cm.compiler"/>
        <line choice="cm.tools"/>
        <line choice="cm.stdlib"/>
        <line choice="cm.docs"/>
        <line choice="cm.vscode"/>
    </choices-outline>

    <choice id="cm.compiler" title="Cm Compiler" enabled="false">
        <pkg-ref id="com.cm-lang.compiler"/>
    </choice>

    <installation-check script="check_installation()"/>
    <volume-check script="check_disk_space()">
        <allowed-os-versions>
            <os-version min="10.15"/>
        </allowed-os-versions>
    </volume-check>

    <script>
        function check_installation() {
            // Check for existing installation
            return system.files.fileExistsAtPath('/usr/local/bin/cm')
                   ? false : true;
        }

        function check_disk_space() {
            return system.sysctl('hw.memsize') >= 200 * 1024 * 1024;
        }
    </script>
</installer-gui-script>
```

### Windows

#### 1. MSI インストーラー

```xml
<!-- cm-installer.wxs -->
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
    <Product Id="*" Name="Cm Programming Language"
             Language="1033" Version="0.12.0.0"
             Manufacturer="Cm Language Team"
             UpgradeCode="12345678-1234-1234-1234-123456789012">

        <Package InstallerVersion="200" Compressed="yes" InstallScope="perMachine"/>

        <MajorUpgrade DowngradeErrorMessage="A newer version is already installed."/>

        <MediaTemplate EmbedCab="yes"/>

        <Feature Id="ProductFeature" Title="Cm Language" Level="1">
            <ComponentGroupRef Id="BinComponents"/>
            <ComponentGroupRef Id="StdlibComponents"/>
            <ComponentRef Id="PathEnvironment"/>
            <ComponentRef Id="VSCodeExtension"/>
        </Feature>

        <!-- Directory structure -->
        <Directory Id="TARGETDIR" Name="SourceDir">
            <Directory Id="ProgramFiles64Folder">
                <Directory Id="INSTALLFOLDER" Name="Cm">
                    <Directory Id="BIN" Name="bin"/>
                    <Directory Id="LIB" Name="lib"/>
                    <Directory Id="INCLUDE" Name="include"/>
                    <Directory Id="STDLIB" Name="stdlib"/>
                </Directory>
            </Directory>
        </Directory>

        <!-- PATH environment variable -->
        <Component Id="PathEnvironment" Guid="87654321-4321-4321-4321-210987654321">
            <Environment Id="PATH" Name="PATH" Value="[INSTALLFOLDER]bin"
                        Permanent="no" Part="last" Action="set" System="yes"/>
        </Component>

        <!-- Custom actions -->
        <CustomAction Id="InstallVSCodeExtension"
                     Execute="deferred"
                     Script="vbscript">
            <![CDATA[
                Set shell = CreateObject("WScript.Shell")
                shell.Run "code --install-extension cm-lang.vscode-cm", 0, True
            ]]>
        </CustomAction>
    </Product>
</Wix>
```

#### 2. Chocolatey パッケージ

```powershell
# chocolateyInstall.ps1
$ErrorActionPreference = 'Stop'

$packageName = 'cm-lang'
$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$url64 = 'https://github.com/cm-lang/cm/releases/download/v0.12.0/cm-windows-x64.zip'

$packageArgs = @{
  packageName   = $packageName
  unzipLocation = $toolsDir
  url64bit      = $url64
  checksum64    = 'abc123...'
  checksumType64= 'sha256'
}

Install-ChocolateyZipPackage @packageArgs

# Add to PATH
$cmPath = Join-Path $toolsDir 'bin'
Install-ChocolateyPath $cmPath

# Install Visual C++ Redistributable if needed
if (!(Test-Path "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64")) {
    Install-ChocolateyPackage 'vcredist140' 'exe' '/quiet' `
        'https://aka.ms/vs/17/release/vc_redist.x64.exe'
}

# Install VSCode extension
if (Get-Command code -ErrorAction SilentlyContinue) {
    & code --install-extension cm-lang.vscode-cm
}
```

### Linux

#### 1. APT リポジトリ（Debian/Ubuntu）

```bash
#!/bin/bash
# install-cm-debian.sh

# Add Cm repository
curl -fsSL https://cm-lang.org/debian/gpg | sudo gpg --dearmor -o /usr/share/keyrings/cm-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/cm-archive-keyring.gpg] https://cm-lang.org/debian stable main" | \
    sudo tee /etc/apt/sources.list.d/cm.list

# Update and install
sudo apt-get update
sudo apt-get install -y cm

# Optional: Install development tools
sudo apt-get install -y cm-dev cm-doc
```

#### 2. RPM パッケージ（RHEL/Fedora）

```spec
# cm.spec
Name:           cm
Version:        0.12.0
Release:        1%{?dist}
Summary:        The Cm Programming Language

License:        MIT and Apache-2.0
URL:            https://cm-lang.org
Source0:        https://github.com/cm-lang/cm/archive/v%{version}.tar.gz

BuildRequires:  llvm-devel >= 17
BuildRequires:  cmake >= 3.20
Requires:       llvm >= 17

%description
Cm is a modern systems programming language with ownership semantics.

%package devel
Summary:        Development files for Cm
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Development files and headers for the Cm programming language.

%prep
%autosetup

%build
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCM_USE_LLVM=ON
cmake --build build

%install
cmake --install build --prefix %{buildroot}%{_prefix}

%files
%license LICENSE
%doc README.md
%{_bindir}/cm
%{_bindir}/cpm
%{_bindir}/cmfmt
%{_bindir}/cmlint
%{_libdir}/libcm_runtime.so*
%{_datadir}/cm/

%files devel
%{_includedir}/cm/
%{_libdir}/libcm_runtime.a
%{_libdir}/pkgconfig/cm.pc
```

#### 3. AppImage（ユニバーサル）

```yaml
# cm.appimage.yml
version: 1

AppDir:
  path: ./AppDir

  app_info:
    id: org.cm-lang.cm
    name: Cm
    icon: cm
    version: 0.12.0
    exec: bin/cm

  files:
    include:
      - usr/bin/cm
      - usr/bin/cpm
      - usr/bin/cmfmt
      - usr/bin/cmlint
      - usr/lib/libcm_runtime.so*
      - usr/share/cm/

  runtime:
    env:
      CM_HOME: $APPDIR/usr/share/cm

AppImage:
  arch: x86_64
  update-information: gh-releases-zsync|cm-lang|cm|latest|Cm-*x86_64.AppImage.zsync
```

## インストールスクリプト

### ユニバーサルインストーラー

```bash
#!/bin/sh
# Universal installer script

set -e

# Configuration
CM_VERSION="${CM_VERSION:-latest}"
CM_INSTALL_DIR="${CM_INSTALL_DIR:-$HOME/.cm}"
DOWNLOAD_URL="https://github.com/cm-lang/cm/releases"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Functions
info() {
    printf "${GREEN}==>${NC} $1\n"
}

error() {
    printf "${RED}Error:${NC} $1\n" >&2
    exit 1
}

detect_platform() {
    OS="$(uname -s)"
    ARCH="$(uname -m)"

    case "$OS" in
        Linux*)
            OS="linux"
            # Detect distribution
            if [ -f /etc/os-release ]; then
                . /etc/os-release
                DISTRO="$ID"
            fi
            ;;
        Darwin*)
            OS="darwin"
            ;;
        MINGW*|CYGWIN*|MSYS*)
            OS="windows"
            ;;
        *)
            error "Unsupported operating system: $OS"
            ;;
    esac

    case "$ARCH" in
        x86_64|amd64)
            ARCH="x86_64"
            ;;
        aarch64|arm64)
            ARCH="aarch64"
            ;;
        *)
            error "Unsupported architecture: $ARCH"
            ;;
    esac

    PLATFORM="${OS}-${ARCH}"
}

download_cm() {
    local url="$DOWNLOAD_URL/$CM_VERSION/cm-${PLATFORM}.tar.gz"
    local tmp_file="/tmp/cm-${PLATFORM}.tar.gz"

    info "Downloading Cm from $url"

    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$tmp_file"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$url" -O "$tmp_file"
    else
        error "Neither curl nor wget found. Please install one of them."
    fi

    info "Extracting to $CM_INSTALL_DIR"
    mkdir -p "$CM_INSTALL_DIR"
    tar -xzf "$tmp_file" -C "$CM_INSTALL_DIR"
    rm "$tmp_file"
}

setup_path() {
    local shell_rc=""
    local shell_name="$(basename "$SHELL")"

    case "$shell_name" in
        bash)
            shell_rc="$HOME/.bashrc"
            ;;
        zsh)
            shell_rc="$HOME/.zshrc"
            ;;
        fish)
            shell_rc="$HOME/.config/fish/config.fish"
            ;;
        *)
            shell_rc="$HOME/.profile"
            ;;
    esac

    local export_line="export PATH=\"\$HOME/.cm/bin:\$PATH\""

    if ! grep -q "\.cm/bin" "$shell_rc" 2>/dev/null; then
        info "Adding Cm to PATH in $shell_rc"
        echo "" >> "$shell_rc"
        echo "# Cm Programming Language" >> "$shell_rc"
        echo "$export_line" >> "$shell_rc"
    fi

    info "Please run: source $shell_rc"
}

install_ide_extensions() {
    # VSCode
    if command -v code >/dev/null 2>&1; then
        info "Installing VSCode extension"
        code --install-extension cm-lang.vscode-cm || true
    fi

    # Vim/Neovim
    if [ -d "$HOME/.vim" ] || [ -d "$HOME/.config/nvim" ]; then
        info "Installing Vim/Neovim plugin"
        # Install vim plugin
        mkdir -p "$HOME/.vim/pack/cm/start"
        git clone https://github.com/cm-lang/vim-cm.git \
            "$HOME/.vim/pack/cm/start/vim-cm" 2>/dev/null || true
    fi
}

verify_installation() {
    if "$CM_INSTALL_DIR/bin/cm" --version >/dev/null 2>&1; then
        info "✅ Cm installed successfully!"
        "$CM_INSTALL_DIR/bin/cm" --version
    else
        error "Installation verification failed"
    fi
}

# Main
main() {
    info "Cm Language Installer"

    detect_platform
    info "Detected platform: $PLATFORM"

    download_cm
    setup_path
    install_ide_extensions
    verify_installation

    cat <<EOF

${GREEN}Installation complete!${NC}

To get started:
  1. Reload your shell configuration
  2. Run: cm --version
  3. Create a new project: cpm new my_project

Documentation: https://docs.cm-lang.org
Examples: https://github.com/cm-lang/examples

EOF
}

# Run installer
main "$@"
```

## アップデートシステム

### 自動アップデート

```rust
// Auto-updater daemon
struct AutoUpdater {
    config: UpdateConfig,
    notifier: Notifier,
}

impl AutoUpdater {
    async fn check_updates(&self) -> Result<Option<Update>> {
        let current = Version::current();
        let latest = self.fetch_latest_version().await?;

        if latest > current {
            // セキュリティアップデートは自動適用
            if latest.is_security_update() && self.config.auto_security {
                self.apply_update(latest).await?;
            } else {
                // 通常アップデートは通知
                self.notifier.notify_update_available(latest)?;
            }
            return Ok(Some(latest));
        }

        Ok(None)
    }

    async fn apply_update(&self, version: Version) -> Result<()> {
        // 1. ダウンロード
        let binary = self.download_version(version).await?;

        // 2. 検証
        self.verify_signature(&binary)?;

        // 3. バックアップ
        self.backup_current()?;

        // 4. アトミックな置き換え
        self.atomic_replace(binary)?;

        Ok(())
    }
}
```

## アンインストール

### クリーンアンインストールスクリプト

```bash
#!/bin/bash
# uninstall-cm.sh

echo "Uninstalling Cm Language..."

# Remove binaries
sudo rm -rf /usr/local/bin/cm
sudo rm -rf /usr/local/bin/cpm
sudo rm -rf /usr/local/bin/cmfmt
sudo rm -rf /usr/local/bin/cmlint

# Remove libraries
sudo rm -rf /usr/local/lib/libcm*
sudo rm -rf /usr/local/share/cm

# Remove user data (optional)
read -p "Remove user configuration and packages? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -rf ~/.cm
    rm -rf ~/.cpm
fi

# Remove from PATH
for rc in ~/.bashrc ~/.zshrc ~/.profile; do
    if [ -f "$rc" ]; then
        sed -i.bak '/\.cm\/bin/d' "$rc"
    fi
done

echo "✅ Cm has been uninstalled"
```

## テレメトリとクラッシュレポート

```rust
// Opt-in telemetry
struct Telemetry {
    enabled: bool,
    endpoint: String,
}

impl Telemetry {
    fn report_installation(&self, info: InstallInfo) {
        if !self.enabled {
            return;
        }

        // Anonymous installation statistics
        let report = json!({
            "version": info.version,
            "platform": info.platform,
            "components": info.components,
            "timestamp": Utc::now(),
            // No PII collected
        });

        self.send_async(report);
    }
}
```

## 実装優先順位

1. **Phase 1** (v0.12.0): 基本インストーラー、Homebrew/APT対応
2. **Phase 2** (v0.13.0): GUI インストーラー、自動アップデート
3. **Phase 3** (v0.14.0): エンタープライズ機能、サイレントインストール