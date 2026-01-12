# JITコンパイラ実装計画 - 概要設計

## 背景と動機

現在のCmコンパイラは以下の実行ターゲットをサポートしています：

| ターゲット | 特徴 | パフォーマンス |
|-----------|------|---------------|
| **LLVM (AOT)** | ネイティブコード生成 | 最高速 |
| **Interpreter** | MIR直接実行 | Python比100倍以上遅い |
| **JavaScript** | トランスパイル | 中程度 |
| **WASM** | WebAssembly出力 | 高速 |

インタープリターの深刻なパフォーマンス問題を解決するため、**JITコンパイラ**を導入し、インタープリターの代替とします。

---

## 技術選定: LLVM ORC JIT

### なぜORC JITか

```
┌─────────────────────────────────────────────────────────────────┐
│                     LLVM JIT API 進化                           │
├─────────────────────────────────────────────────────────────────┤
│  Legacy JIT (廃止) → MCJIT (レガシー) → ORC JIT (推奨)          │
└─────────────────────────────────────────────────────────────────┘
```

**ORC (On-Request Compilation)** は LLVM の第3世代JIT APIで、以下の利点があります：

| 機能 | 説明 |
|------|------|
| **遅延コンパイル** | 関数が呼び出されるまでコンパイルを遅延 |
| **並行コンパイル** | 複数関数を並列にコンパイル可能 |
| **モジュラー設計** | レイヤーアーキテクチャで柔軟にカスタマイズ |
| **LLJIT** | 簡単に使えるオフザシェルフJIT実装 |

---

## アーキテクチャ概要

### 現在の構造
```
src/codegen/
├── interpreter/     # MIR直接実行（遅い）
├── llvm/            # AOTコンパイル（既存）
│   └── core/
│       └── mir_to_llvm.cpp  # MIR → LLVM IR 変換
├── js/              # JavaScript出力
└── common/          # 共通ユーティリティ
```

### 提案する構造
```
src/codegen/
├── interpreter/     # 維持（テスト用・デバッグ用）
├── llvm/            # AOTコンパイル（既存）
├── jit/             # 【新規】JITコンパイラ
│   ├── jit_engine.hpp        # ORC JIT エンジン
│   ├── jit_engine.cpp
│   ├── jit_runtime.hpp       # ランタイムサポート
│   ├── jit_runtime.cpp
│   ├── jit_builtins.hpp      # 組み込み関数バインディング
│   └── jit_builtins.cpp
├── js/
└── common/
```

---

## 主要コンポーネント設計

### 1. JITEngine クラス

```cpp
namespace cm::codegen::jit {

class JITEngine {
public:
    JITEngine();
    ~JITEngine();
    
    // MIRプログラムをコンパイル＆実行
    ExecutionResult execute(const mir::MirProgram& program,
                           const std::string& entry_point = "main");
    
private:
    std::unique_ptr<llvm::orc::LLJIT> jit_;
    std::unique_ptr<llvm::LLVMContext> context_;
    
    // MIRToLLVM を再利用
    std::unique_ptr<llvm_backend::MIRToLLVM> converter_;
    
    // 組み込み関数を登録
    void registerBuiltins();
    
    // シンボル解決
    llvm::orc::SymbolMap resolveSymbols(const std::string& name);
};

} // namespace cm::codegen::jit
```

### 2. 既存コードの再利用

> [!IMPORTANT]
> 既存の `MIRToLLVM` クラスはMIR→LLVM IR変換をすべて実装済み。
> JITでは、生成されたIRをファイルに出力する代わりにメモリ上でコンパイル・実行します。

```
┌───────────────┐     ┌─────────────┐     ┌──────────────┐
│   MIR Program │────▶│ MIRToLLVM   │────▶│ LLVM Module  │
└───────────────┘     │ (既存再利用) │     └──────┬───────┘
                      └─────────────┘            │
                                                 ▼
┌───────────────┐     ┌─────────────┐     ┌──────────────┐
│   main()実行   │◀────│ ORC JIT     │◀────│ ネイティブ    │
└───────────────┘     │ (LLJIT)     │     │ コード       │
                      └─────────────┘     └──────────────┘
```

### 3. 組み込み関数サポート

インタープリターで実装済みの組み込み関数をJITに移植：

| 関数カテゴリ | 例 | JIT対応 |
|-------------|-----|---------|
| I/O | `cm_println`, `cm_print` | LLVMランタイムから呼び出し |
| 文字列 | `cm_string_concat`, `cm_format_replace_*` | リンク時解決 |
| 配列/スライス | `cm_slice_new`, `cm_array_to_slice` | リンク時解決 |
| メモリ | `cm_alloc`, `cm_free` | libc連携 |

---

## 実装フェーズ

### Phase 1: 基本JITエンジン（1-2日）
- [ ] ORC JIT (LLJIT) のセットアップ
- [ ] MIRToLLVM統合
- [ ] 単純なプログラム実行テスト

### Phase 2: 組み込み関数移植（2-3日）
- [ ] ランタイムライブラリのJIT向け再構築
- [ ] シンボル解決の実装
- [ ] 標準I/O関数の動作確認

### Phase 3: 最適化とテスト（1-2日）
- [ ] コンパイル時間の最適化
- [ ] 既存テストスイートでの検証
- [ ] ベンチマーク比較

---

## 期待される成果

| 指標 | インタープリター | JIT (予測) |
|------|-----------------|------------|
| Fibonacci(40) | ~60秒 | <1秒 |
| 起動時間 | 即座 | 数十ms |
| メモリ使用量 | 低 | 中（コード保持） |

---

## 次のステップ

1. **詳細設計書の作成** - `002_jit_detailed_design.md`
2. **JITエンジンスケルトン実装**
3. **ベンチマークによる検証**

---

## 参考資料

- [LLVM ORC JIT Design](https://llvm.org/docs/ORCv2.html)
- [Building an ORC-based JIT](https://llvm.org/docs/tutorial/BuildingAJIT1.html)
- [LLJIT API Reference](https://llvm.org/doxygen/classllvm_1_1orc_1_1LLJIT.html)
