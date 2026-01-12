[English](compile_options_redesign.en.html)

# コンパイルオプション再設計

## 現状の問題
- `--target=uefi` のような具体的すぎるターゲット指定
- アーキテクチャとABIが分離されていない
- プラットフォーム固有の設定が不明確

## 新しいオプション体系

### 基本構造
```
cm compile [OPTIONS] <input.cm>

必須オプション（または自動検出）：
  --target=<env>     実行環境
  --arch=<arch>      CPUアーキテクチャ
  --abi=<abi>        呼び出し規約

オプション：
  --cpu=<cpu>        特定CPU最適化
  --features=<list>  CPU機能の有効/無効
  --link-script=<file> リンカスクリプト
```

### 使用例

#### 1. UEFI アプリケーション
```bash
cm compile \
  --target=no_std \
  --arch=x86_64 \
  --abi=ms \
  --link-script=uefi.ld \
  -o bootx64.efi \
  main.cm
```

生成されるコード特性：
- PE32+フォーマット
- MS ABIの呼び出し規約
- エントリポイント: `efi_main`
- UTF-16文字列サポート

#### 2. Linux カーネルモジュール
```bash
cm compile \
  --target=no_std \
  --arch=x86_64 \
  --abi=sysv \
  --features=-sse,-mmx,+soft-float \
  -o module.ko \
  driver.cm
```

#### 3. ARM Cortex-M（組み込み）
```bash
cm compile \
  --target=no_std \
  --arch=arm \
  --cpu=cortex-m4 \
  --abi=eabi \
  -o firmware.elf \
  main.cm
```

#### 4. 通常のアプリケーション（自動検出）
```bash
# archとabiは自動検出
cm compile --target=native main.cm

# 明示的に指定も可能
cm compile \
  --target=native \
  --arch=x86_64 \
  --abi=sysv \
  main.cm
```

## ソースコード内での指定

```cm
// ファイル先頭で指定
#![no_std]
#![target_arch = "x86_64"]
#![target_abi = "ms"]

// 条件コンパイル
#[cfg(target_arch = "x86_64")]
void x64_specific() { }

#[cfg(target_abi = "ms")]
void windows_uefi_specific() { }

#[cfg(all(target = "no_std", target_arch = "arm"))]
void embedded_arm_specific() { }
```

## 文字列リテラルの扱い

```cm
// デフォルト（UTF-8）
string s = "Hello";

// UTF-16（Windows/UEFI用）
#[cfg(target_abi = "ms")]
uint16[] wide = u"Hello, UEFI!\r\n";

// プラットフォーム非依存の抽象化
typedef PlatformString = #[cfg(target_abi = "ms")] uint16[]
                       | #[cfg(not(target_abi = "ms"))] string;

PlatformString greeting = platform_string!("Hello");
```

## 設定ファイル（Cm.toml）

```toml
[package]
name = "my-os"
version = "0.1.0"

[target.x86_64-uefi]
target = "no_std"
arch = "x86_64"
abi = "ms"
link_script = "uefi.ld"
entry_point = "efi_main"

[target.arm-embedded]
target = "no_std"
arch = "arm"
cpu = "cortex-m4"
abi = "eabi"
features = ["+thumb2", "+fpu"]

[target.linux-kernel]
target = "no_std"
arch = "x86_64"
abi = "sysv"
features = ["-sse", "-mmx", "+soft-float"]
```

## 実装優先順位

### Phase 1: 基本分離（2週間）
1. `--target` と `--arch` の分離
2. 自動検出ロジック
3. 基本的な組み合わせバリデーション

### Phase 2: ABI対応（3週間）
1. `--abi` オプション追加
2. calling convention の実装
3. 文字列エンコーディング対応

### Phase 3: 高度な機能（1ヶ月）
1. CPU features の細かい制御
2. リンカスクリプト統合
3. Cm.toml での設定

## まとめ

この設計により：
- **明確な責任分離**: 環境、アーキテクチャ、ABIが独立
- **柔軟な組み合わせ**: UEFI on ARM64 なども可能
- **将来の拡張性**: 新しいアーキテクチャやABIの追加が容易
- **既存コードとの互換性**: `--target=native` はそのまま動作