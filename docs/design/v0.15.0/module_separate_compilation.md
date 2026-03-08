# モジュール単位分離コンパイル — 設計書

## 概要

Cmコンパイラの大規模プロジェクト向けビルド高速化のための分離コンパイル設計。

## 現状分析

### コンパイルパイプライン

```
ソースファイル群
    ↓
ImportPreprocessor  — 全モジュールをインライン展開（単一ソースへ）
    ↓
Lexer → Parser → TypeChecker → HIR → MIR → MIR最適化
    ↓                                         (全体で約20%)
LLVM IR生成 → LLVM最適化 → マシンコード → リンク
                                              (全体で約80%)
```

### プロファイリング結果（01_prime.cm — 2025/02/15）

| フェーズ | 時間 | 割合 |
|---|---|---|
| プリプロセス | 8ms | 2% |
| パース+型チェック | <1ms | <1% |
| HIR+MIR変換 | <1ms | <1% |
| MIR最適化 | <1ms | <1% |
| LLVM codegen | 272ms | 82% |
| **合計** | **333ms** | **100%** |

### 現在の差分ビルドの限界

- **全ファイル無変更**: キャッシュヒット → LLVM codegen完全スキップ（~72ms）
- **1ファイル変更**: キャッシュミス → 全体再コンパイル（変更の影響範囲に関係なく）

## 提案するアーキテクチャ（v0.15.0以降）

### フェーズ1: MIRレベル分割

`MirProgram`を`module_path`ごとにサブプログラムに分割：

```
MirProgram (全体)
    ├── main モジュール: {main(), helper_func(), ...}
    ├── efi モジュール: {efi_draw(), efi_print(), ...}
    └── std::io モジュール: {println(), print(), ...}
```

### フェーズ2: モジュール別LLVM codegen

各サブプログラムを独立したLLVMモジュールとしてコンパイル：

```
main.o  ← mainモジュールのLLVM codegen結果
efi.o   ← efiモジュールのLLVM codegen結果
io.o    ← std::ioモジュールのLLVM codegen結果
```

### フェーズ3: キャッシュ+リンク

変更モジュールのみ再コンパイル、残りはキャッシュ:

```
main.cm変更 → main.o再生成、efi.o/io.oはキャッシュから
→ ld main.o efi.o io.o → 実行ファイル
```

## 技術的課題

### 1. シンボル可視性

現在の`MIRToLLVM`コンバータは全シンボルが同一モジュール内にあることを前提。
分割時にはextern宣言の生成が必要。

### 2. 型情報の共有

構造体定義やenum定義は複数モジュールから参照される。
各LLVMモジュールに重複して定義するか、型情報ヘッダを生成する必要がある。

### 3. グローバル状態

vtable、グローバル変数、static変数は特定モジュールに配置し、
他モジュールからはextern参照する。

## 実装優先度

1. **コンパイル段階別プロファイリング** ✅ 実装済み
2. **既存キャッシュの改善** — 変更検出精度向上
3. **MIRレベルモジュール分割**
4. **LLVM codegen分割**
5. **モジュール別キャッシュ+リンク**

## 関連ファイル

- `src/main.cpp` — コンパイルパイプライン制御
- `src/preprocessor/import.hpp` — ImportPreprocessor（`module_ranges`、`resolved_files`）
- `src/mir/nodes.hpp` — MIRProgram（`MirFunction.module_path`）
- `src/codegen/llvm/native/codegen.cpp` — LLVMCodeGen
- `src/codegen/llvm/native/mir_to_llvm.cpp` — MIRToLLVMコンバータ
- `src/common/cache_manager.hpp` — CacheManager
