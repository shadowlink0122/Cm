# Cmコンパイラ プラットフォーム別最適化分析

本ドキュメントは、Cmコンパイラのプラットフォーム別最適化要件を詳細に分析したものです。

**作成日**: 2025-12-31  
**調査対象**: src/mir/optimizations/, src/codegen/

---

## エグゼクティブサマリー

### 現在の実装状況

Cmコンパイラは、**MIRレベルの汎用最適化**と**プラットフォーム固有のコード生成最適化**の2層構造を採用しています。

| 層 | 実装箇所 | 特徴 |
|---|---------|------|
| **MIR最適化（汎用）** | `src/mir/optimizations/` | すべてのプラットフォームに共通 |
| **コード生成最適化** | `src/codegen/{llvm,js,interpreter}/` | プラットフォーム固有 |

### 実装済みMIR最適化パス（Phase 1-5完了）

```
Phase 1: SCCP, ConstantFolding
   ↓
Phase 2: GVN, CopyPropagation
   ↓
Phase 3: DSE
   ↓
Phase 4: SimplifyControlFlow, FunctionInlining
   ↓
Phase 5: LICM
   ↓
[Fixpoint Convergence Loop] (Opt Level 2以上で複数回実行)
```

---

## 1. 実装済み最適化の詳細

### 1.1 MIRレベル最適化（共通基盤）

すべてのプラットフォームの前段で実行される最適化パスです。

#### Phase 1: 基礎最適化

**SCCP (Sparse Conditional Constant Propagation)**
- ファイル: `src/mir/optimizations/sccp.hpp`
- 到達可能性とデータフローを同時に解析
- 条件分岐を考慮した定数伝播
- 整数演算、比較演算の定数化

**ConstantFolding**
- ファイル: `src/mir/optimizations/constant_folding.hpp`
- 行数: 400行 (実装規模が最大級)
- **対応する定数化**:
  - 整数演算: Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr
  - 比較演算: Eq, Ne, Lt, Le, Gt, Ge
  - 単項演算: Neg, BitNot, Not
  - 型変換: Int ↔ Float ↔ Bool ↔ Char
  - フォーマット変換 (FormatConvert)

#### Phase 2: データフロー最適化

**GVN (Global Value Numbering)**
- ファイル: `src/mir/optimizations/gvn.hpp`
- ブロック内で同一の計算結果を再利用
- CSE (Common Subexpression Elimination) の効果

**CopyPropagation**
- ファイル: `src/mir/optimizations/copy_propagation.hpp`
- `x = y` 形式のコピーを検出
- 使用箇所をコピー元で直接置換
- 複数回代入変数の除外対応

#### Phase 3: 冗長性排除

**DSE (Dead Store Elimination)**
- ファイル: `src/mir/optimizations/dse.hpp`
- 同一ブロック内で上書きされるストア削除
- 読み出しのないストア命令を削除
- 副作用を考慮した保守的な解析

#### Phase 4: 制御フロー最適化

**SimplifyControlFlow**
- ファイル: `src/mir/optimizations/simplify_cfg.hpp`
- Unreachable Block Elimination: 到達不能ブロック削除
- Block Merging: 直線的ブロック統合
- Empty Block Elimination: 空ブロック短絡

**FunctionInlining**
- ファイル: `src/mir/optimizations/inlining.hpp`
- 小規模関数（命令数閾値以下）の展開
- 呼び出しオーバーヘッド削減

#### Phase 5: ループ最適化

**LICM (Loop Invariant Code Motion)**
- ファイル: `src/mir/optimizations/licm.hpp` (323行)
- **依存技術**: Dominator Tree, Loop Analysis
- ループ内不変式をループ外に移動
- Pre-header自動作成/利用
- ポインタアリアスの安全性考慮

### 1.2 プラットフォーム別コード生成

#### インタプリタ (MIRレベル直接実行)

**実装**: `src/codegen/interpreter/`
- **最適化アプローチ**: 最適化不要（直接実行）
- **利点**:
  - デバッグ対応性が高い
  - 開発効率が良い
- **実装内容**:
  - 構造体フィールドアクセス
  - 組み込み関数（String, Array, I/O, Format）
  - メモリ管理（自動）

#### JavaScript コード生成

**実装**: `src/codegen/js/codegen.hpp`, `codegen.cpp`
- **特徴**: 直接JavaScriptを出力 (LLVM IRを経由しない)

**JavaScript固有の最適化手法**:
| 最適化手法 | 実装 | 効果 |
|-----------|------|------|
| **変数インライン化** | 小さい式を直接展開 | 変数宣言削減 |
| **変数使用回数追跡** | `use_counts` map | 1回使用のみの場合インライン |
| **静的フロー分析** | ControlFlowAnalyzer | 到達可能性判定 |
| **ローカル宣言最適化** | `declare_on_assign` | 宣言と代入を同時実行 |
| **CSS構造体特別化** | `isCssStruct()` | ケバブケース フィールド名 |
| **ボクシング分析** | `boxed_locals_` | アドレス取得変数の特別扱い |
| **Runtime Helper最小化** | 依存関係展開 | 使用済みヘルパーのみ出力 |
| **オブジェクトリテラル最適化** | `tryEmitObjectLiteralReturn()` | return構文の最適化 |

**生成される構造化制御フロー**:
```javascript
// MIRのブロック分岐 → JavaScriptのネイティブ制御構造
if (condition) {
    // ...
} else {
    // ...
}

while (loopCondition) {
    // ...
}
```

#### LLVM Native コード生成

**実装**: `src/codegen/llvm/native/`
- **パイプライン**:
  ```
  MIR → LLVM IR → PassBuilder最適化 → ネイティブコード
  ```

**LLVM PassBuilder による最適化** (最適化レベル 0-3):
- **O1 (Less)**: 基本最適化のみ
- **O2 (Default)**: 汎用最適化フルセット
- **O3 (Aggressive)**: キャッシュ局所性、ベクトル化まで
- **Oz (Size)**: コードサイズ優先

**ターゲット固有の設定**: `src/codegen/llvm/native/target.hpp`
```cpp
// ネイティブターゲット
TargetConfig::getNative():
  - triple: llvm::sys::getDefaultTargetTriple()
  - cpu: llvm::sys::getHostCPUName()
  - features: 自動検出
  - optLevel: 2 (デフォルト)
```

**LLVM最適化パス例**:
- Function inlining, dead code elimination
- Loop optimizations, vectorization
- Register allocation, instruction scheduling
- Branch prediction optimization

#### WASM コード生成

**実装**: `src/codegen/llvm/wasm/`
- **パイプライン**:
  ```
  MIR → LLVM IR (wasm32-unknown-emscripten) → WASM
  ```

**WASM固有の最適化戦略**: `src/codegen/llvm/native/codegen.hpp` L221-225
```cpp
if (context->getTargetConfig().target == BuildTarget::Wasm) {
    // WebAssembly用の最適化（サイズ優先）
    MPM = passBuilder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::Oz  // コードサイズ最小化
    );
}
```

**最適化アプローチ**:
| 最適化 | 理由 | 効果 |
|-------|------|------|
| **コードサイズ優先 (Oz)** | ダウンロード時間 | 20-40%削減 |
| **間接呼び出し最適化** | テーブルインデックス最小化 | 実行速度向上 |
| **メモリページング** | WASM線形メモリ制約 | 動的アロケーション削減 |
| **浮動小数点最適化** | FPU遅延 | fastmath有効化 |
| **テーブルサイズ最小化** | インポート関数削減 | バイナリサイズ削減 |

#### ベアメタル (no_std) コード生成

**実装**: `src/codegen/llvm/native/target.hpp` L40-41, L78-88
- **ターゲット**: ARM Cortex-M (stm32)
- **スタートアップコード自動生成**: `generateStartupCode()`

**ベアメタル固有の特別処理**:
```cpp
// スタックポインタ初期化
TargetManager::generateStartupCode():
  - _estack シンボル設定
  - MSPレジスタ設定（インラインアセンブリ）
  - データセクション初期化 (memcpy)
  - BSSセクション ゼロクリア (memset)
```

**ベアメタル最適化戦略**: `src/codegen/llvm/native/codegen.hpp` L226-230
```cpp
} else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
    // ベアメタル用の最適化（サイズ優先）
    MPM = passBuilder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::Os  // コードサイズ優先
    );
}
```

**ベアメタル固有の要件**:
| 要件 | 実装 | 理由 |
|------|------|------|
| **スタック使用量制限** | LICM活用で無限ループ回避 | スタック溢れ防止 |
| **メモリセクション管理** | リンカスクリプト自動生成 | Flash/RAM分離 |
| **インライン化制約** | 選別的インライン化 | スタック圧力軽減 |
| **動的メモリ禁止** | スタック割り当てのみ | メモリフラグメンテーション防止 |
| **割り込みハンドラ対応** | 予約関数シンボル | 予測可能性 |

---

## 2. プラットフォーム固有で必要な最適化

### 2.1 LLVM Native (x86_64, ARM64)

**MIRレベルでは実装不可の最適化**:

| 最適化 | 実装層 | 必要な情報 | 理由 |
|--------|-------|---------|------|
| **レジスタ割り当て** | LLVM Codegen | マシンレジスタ数 | アーキテクチャ依存 |
| **キャッシュ局所性** | LLVM Codegen | キャッシュ階層 | メモリ階層特性依存 |
| **命令スケジューリング** | LLVM Codegen | 命令遅延 | CPUパイプライン依存 |
| **ベクトル化** | LLVM Codegen | SIMD命令セット | CPUマイクロアーキテクチャ依存 |
| **分岐予測最適化** | LLVM Codegen | CPU予測器 | 分岐パターン依存 |

**LLVM が処理する最適化**:
- InstCombine (命令結合)
- MemorySSA (メモリSSA形式)
- GVN-Hoist (式のホイスト)
- NewGVN (新しいGVN実装)
- LoopStrengthReduce (ループ強度削減)
- LoopUnroll (ループ展開)
- SLPVectorizer (SLP vectorization)

### 2.2 JavaScript

**MIRレベルでは実装不可の最適化**:

| 最適化 | 実装層 | 必要な情報 | 理由 |
|--------|-------|---------|------|
| **型推測** | JS Engine (V8/SpiderMonkey) | 動的型情報 | 実行時の型フィードバック |
| **インラインキャッシュ** | JS Engine | メソッド呼び出し統計 | ポリモーフィック呼び出し最適化 |
| **JIT コンパイル** | JS Engine | ホットパス検出 | プロファイル駆動最適化 |
| **オブジェクト形状最適化** | JS Engine | フィールドアクセスパターン | Hidden Class最適化 |
| **文字列インターニング** | JS Engine | 文字列生成パターン | メモリ効率化 |

**Cm が実装する最適化**:
- ローカル変数インライン化
- 死コード削除 (MIRレベル)
- 変数使用回数の追跡と最適化
- CSS特別化 (ケバブケース変換)

**JavaScript生成コード例**:
```javascript
// 最適化前: MIR表現をそのまま翻訳
const _temp1 = compute_something();
const _temp2 = _temp1 + 5;
if (_temp2 > 10) {
    return _temp2;
}

// 最適化後: インライン化とDCE
if ((compute_something() + 5) > 10) {
    return compute_something() + 5;  // ※実際にはmemo化される
}
```

### 2.3 WASM

**MIRレベルでは実装不可の最適化**:

| 最適化 | 実装層 | 必要な情報 | 理由 |
|--------|-------|---------|------|
| **間接呼び出し最適化** | LLVM WASM Backend | 関数テーブル解析 | Table element inlining |
| **メモリアクセス最適化** | LLVM WASM Backend | メモリページング | Linear memory bounds check |
| **データセグメント最小化** | WASM バイナリ生成 | 到達可能データ | Data section compression |
| **テーブルサイズ最適化** | WASM バイナリ生成 | 実装関数数 | Indirect call table最小化 |

**Cm が実装する最適化**:
- コードサイズ優先 (Oz)
- 死コード削除 (MIRレベル)
- 関数インライン化 (小規模関数)

### 2.4 ベアメタル (no_std)

**MIRレベルでは実装不可の最適化**:

| 最適化 | 実装層 | 必要な情報 | 理由 |
|--------|-------|---------|------|
| **スタック使用量解析** | リンカスクリプト/デバッガ | 全関数の動的コール情報 | 静的解析では不完全 |
| **割り込みハンドラ順序化** | リンク時最適化 | ベクトルテーブル配置 | VT(Vector Table)エントリ最適化 |
| **インラインアセンブリ融合** | LLVM Codegen | ISA知識 | 命令合成最適化 |

**Cm が実装する最適化**:
- LICM (ループ内のスタック圧力軽減)
- 関数インライン化 (スタック使用量抑制)
- コードサイズ優先 (Os)
- リンカスクリプト自動生成

---

## 3. MIRレベルで対応できない理由の詳細分析

### 3.1 アーキテクチャ固有情報の不足

```
MIR表現
  ↓
  └─ マシンリソース不可視
     - レジスタ数・種類
     - キャッシュサイズ・ライン
     - 命令遅延・スループット
     - メモリ遅延
     
  → MIRでは汎用的な表現のみ可能
  → プラットフォーム依存最適化は実装不可
```

### 3.2 型システムの詳細度の不足

```
MIR型システム (高レベル)
  │
  ├─ int, float, bool, struct, ...
  │
  └─ 足りない情報:
     - 動的型情報 (JavaScript)
     - メモリページング制約 (WASM)
     - リアルタイム保証 (ベアメタル)
     
  → 型推測やJIT最適化は実行時情報が必須
```

### 3.3 プロファイル情報の不可用性

```
実行時情報の活用が必須な最適化:

┌────────────────────────────────────┐
│  分岐予測                           │
│  - 分岐の取られやすさ (%)          │
│  - キャッシュミス率                │
│  → 実行環境でのプロファイリング    │
└────────────────────────────────────┘

┌────────────────────────────────────┐
│  ホットパス検出 (JS)               │
│  - 呼び出し頻度                    │
│  - ループイテレーション数          │
│  → V8/SpiderMonkeyの実行時解析    │
└────────────────────────────────────┘

┌────────────────────────────────────┐
│  メモリアクセスパターン             │
│  - キャッシュ局所性                │
│  - ページング動作                  │
│  → 実行時のメモリトレース          │
└────────────────────────────────────┘
```

### 3.4 セマンティック保持の困難性

MIRレベルで以下を最適化すると、プラットフォーム固有の動作を変える可能性:

```
例: メモリアクセス順序変更

MIRコード:
  _0 = load ptr[0]
  call_function()  // 副作用不明
  _1 = load ptr[1]

最適化案: 順序変更
  _0 = load ptr[0]
  _1 = load ptr[1]
  call_function()

リスク:
  - JavaScript: call_function()が_0に依存する可能性
  - WASM: Shared Memory (Workers)で競合条件発生
  - ベアメタル: 割り込みハンドラが介入する可能性
  
→ セマンティック保持が困難なため、
  プラットフォーム別の安全解析が必須
```

---

## 4. no_std/ベアメタルサポートの必要性分析

### 4.1 現在のサポート状況

**実装済み**:
- [x] LLVMバックエンドでベアメタルターゲット対応
- [x] ARM Cortex-M ターゲット設定
- [x] スタートアップコード自動生成
- [x] リンカスクリプト自動生成
- [x] MSPレジスタ初期化

**未実装**:
- [ ] スタック使用量静的解析
- [ ] メモリセクション最適化
- [ ] 割り込みハンドラ統合
- [ ] リアルタイム保証検証

### 4.2 ベアメタル環境での要件

#### 4.2.1 メモリ制約

```
STM32L476等の一般的なMCU:
  Flash: 256KB
  RAM:   64KB
  EEPROM: 6KB

Cm の現在の要件:
  - MIRインタプリタ: ~100KB
  - LLVM IR生成: ~200KB (コンパイル時)
  - 生成コード: 可変

→ 最適化必須: コードサイズ優先 (Os)
```

#### 4.2.2 スタック管理

```
典型的なベアメタル環境:
  
┌──────────────────────────┐ 0x20000000 + 64K
│  スタック                │ ← SP (成長方向: 下向き)
├──────────────────────────┤
│                          │
│      ヒープ              │
│ (malloc/new 領域)       │
├──────────────────────────┤
│  BSS セクション          │ (グローバル変数)
├──────────────────────────┤
│  DATA セクション         │ (初期化済みグローバル)
└──────────────────────────┘ 0x20000000

リスク: スタック溢れ
  → LICM による無限ループ回避が重要
  → スタック圧力最小化が必須
```

#### 4.2.3 リアルタイム要件

```
組み込みシステムの典型的な要件:

┌─────────────────────────────────┐
│ 実時間性 (Determinism)         │
│ - 予測可能な実行時間            │
│ - キャッシュがない/小さい       │
│ → 条件分岐の最小化              │
└─────────────────────────────────┘

┌─────────────────────────────────┐
│ 割り込みレイテンシ              │
│ - 割り込み応答時間 < 1μs        │
│ → 臨界セクション最小化          │
└─────────────────────────────────┘

┌─────────────────────────────────┐
│ 動的メモリ禁止                  │
│ - メモリフラグメンテーション防止│
│ → スタック割り当てのみ          │
└─────────────────────────────────┘
```

### 4.3 no_std サポートの実装戦略

#### 4.3.1 推奨される実装順序

1. **Phase 1: スタック使用量解析**
   - 全関数のスタック使用量計測
   - 自動警告システム
   - 最大再帰深度検証

2. **Phase 2: メモリセクション最適化**
   - ROM化可能なデータの自動検出
   - `__flash` 修飾子の導入
   - リンカスクリプトの自動調整

3. **Phase 3: 割り込みハンドラ統合**
   - `[[interrupt]]` 属性対応
   - ISR自動登録機構
   - スタック保存最適化

4. **Phase 4: リアルタイム保証**
   - ワーストケース実行時間 (WCET) 推定
   - 詳細なタイミング情報提供

#### 4.3.2 推奨される言語拡張

```cm
// スタック制限指定
[[stack_limit(2048)]]
void critical_loop() {
    // ...
}

// ROM化指定
struct Config {
    [[const_flash]] int param1;
    [[const_flash]] int param2;
}

// 割り込みハンドラ
[[interrupt]]
void USART1_IRQHandler() {
    // ISR処理
}

// リアルタイム制約
[[wcet(100)]]  // Worst Case Execution Time: 100μs
void sensor_read() {
    // ...
}
```

---

## 5. 最適化実装への推奨事項

### 5.1 MIRレベルで追加可能な最適化 (短期)

| 最適化 | 推奨度 | 実装難度 | 効果範囲 |
|--------|--------|---------|---------|
| **強度削減 (Strength Reduction)** | ★★★★☆ | 中 | 全プラットフォーム |
| **ポインタ解析 (Points-To Analysis)** | ★★★☆☆ | 高 | メモリ最適化全般 |
| **範囲チェック削除** | ★★★☆☆ | 中 | 配列アクセス |
| **ビットフィールド最適化** | ★★☆☆☆ | 低 | 構造体メモリ |
| **関数属性 (pure/const)** | ★★★★★ | 低 | CSE機会拡大 |

### 5.2 プラットフォーム別の最適化焦点

#### LLVM Native
- [ ] LLVMパスの細粒度制御
- [ ] プロファイル駆動最適化 (PGO)
- [ ] リンク時最適化 (LTO) の活用

#### JavaScript
- [ ] TypedArray の自動活用
- [ ] 静的型推測の向上
- [ ] Worker スレッド活用の検討

#### WASM
- [ ] バルクメモリ操作 (`memory.copy`)
- [ ] Reference Types の活用
- [ ] Call Indirect テーブル最小化

#### ベアメタル
- [ ] スタック使用量追跡
- [ ] メモリセクション最適化
- [ ] リアルタイム保証機構

### 5.3 アーキテクチャの改善案

#### 5.3.1 プロファイル駆動最適化 (PGO) の導入

```
Current:
  MIR → (Platform-specific Codegen) → Output

Proposed with PGO:
  MIR → (Instrumentation Pass) →
    → (Platform-specific Codegen) → Output (Instrumented)
      ↓
    [Profiling Run]
      ↓
    [Profile Data] 
      ↓
    MIR → (PGO Pass: Branch/Loop Optimization) →
      → (Platform-specific Codegen) → Output (Optimized)
```

#### 5.3.2 マルチターゲット最適化キャッシュ

```
type OptimizationCache {
  target: BuildTarget,
  optimization_level: 0-3,
  profile_data: Option<ProfileData>,
  computed_analyses: {
    dominators: DominatorTree,
    loops: LoopAnalysis,
    alias_sets: AliasAnalysis,
  },
  platform_hints: {
    native: { cache_line: 64, registers: 16 },
    wasm: { memory_pages: 1024 },
    baremetal: { stack_limit: 65536 },
  }
}
```

---

## 6. 検証とテスト戦略

### 6.1 最適化の正確性検証

```
Test Framework:

┌─────────────────────────────────────┐
│ 1. 機能的正確性テスト               │
│    - 最適化前後で同じ出力           │
│    - サニタイザーで副作用検出       │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 2. パフォーマンステスト              │
│    - 実行時間計測                   │
│    - メモリ使用量計測               │
│    - プロファイル駆動テスト         │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 3. リグレッション検出               │
│    - 過去のテストケース再実行       │
│    - CI/CD 統合                     │
└─────────────────────────────────────┘
```

### 6.2 プラットフォーム別テストマトリックス

| テスト項目 | Interpreter | JS | WASM | Native | Baremetal |
|-----------|-------------|----|----|--------|-----------|
| 機能性 | ✓ | ✓ | ✓ | ✓ | ✓ |
| パフォーマンス | ○ | ○ | ✓ | ✓ | ✓ |
| メモリ使用量 | ○ | ○ | ✓ | ✓ | ✓ |
| リアルタイム性 | ✗ | ✗ | ✗ | ○ | ✓ |

---

## 7. まとめと結論

### 7.1 現状の評価

**強み**:
- MIRレベルの包括的最適化パス実装
- プラットフォーム別の適切な最適化戦略
- スケーラブルなアーキテクチャ

**弱み**:
- ベアメタル環境での詳細な最適化不足
- プロファイル駆動最適化の欠如
- ポインタ解析の未実装

### 7.2 推奨される優先順位

1. **高優先度**:
   - ベアメタルスタック使用量解析
   - WASM バイナリサイズ最適化
   - JavaScript 型推測強化

2. **中優先度**:
   - LTO (Link-Time Optimization) 統合
   - プロファイル駆動最適化基盤
   - ポインタ解析の実装

3. **低優先度**:
   - WCET 推定
   - 並列化最適化
   - 機械学習駆動最適化

### 7.3 長期ビジョン

```
2025年 (現状):
  ├─ MIR汎用最適化: 完全
  ├─ LLVM Native: 十分
  ├─ JavaScript: 基本実装
  ├─ WASM: 基本実装
  └─ Baremetal: 初期実装

2026年 (推奨):
  ├─ PGO フレームワーク導入
  ├─ ベアメタル詳細最適化
  ├─ ポインタ解析実装
  └─ マルチターゲット最適化戦略

2027年+ (展望):
  ├─ 機械学習駆動最適化
  ├─ プロトコルベアメタル対応
  └─ コード生成キャッシング
```

---

## 付録: ファイル構成と実装マップ

### A. 最適化パスの実装ファイル

```
src/mir/optimizations/
├── optimization_pass.hpp          [基底クラス]
├── all_passes.hpp                 [パイプライン管理]
├── constant_folding.hpp           [400行: 最大規模]
├── copy_propagation.hpp           
├── dead_code_elimination.hpp      
├── program_dce.hpp                
├── dse.hpp                        [Dead Store Elimination]
├── gvn.hpp                        [Global Value Numbering]
├── inlining.hpp                   [関数インライン化]
├── licm.hpp                       [323行: ループ最適化]
├── sccp.hpp                       [条件付き定数伝播]
└── simplify_cfg.hpp               [CFG簡約化]

src/mir/analysis/
├── dominators.hpp                 [LICM依存]
└── loop_analysis.hpp              [LICM依存]
```

### B. コード生成実装ファイル

```
src/codegen/
├── interpreter/
│   ├── interpreter.hpp            [MIR直接実行]
│   ├── eval.hpp                   [値評価]
│   └── builtins.hpp               [組み込み関数]
│
├── js/
│   ├── codegen.hpp/cpp            [JavaScript生成]
│   ├── emitter.hpp                [JS出力]
│   ├── control_flow.hpp           [制御フロー分析]
│   └── runtime.hpp                [ランタイムヘルパー]
│
└── llvm/
    ├── core/
    │   ├── mir_to_llvm.hpp        [MIR→LLVM変換]
    │   ├── context.hpp            [LLVM Context管理]
    │   ├── intrinsics.hpp         [組み込み関数]
    │   └── types.cpp              [型変換]
    │
    ├── native/
    │   ├── codegen.hpp            [Native コード生成]
    │   ├── target.hpp             [ターゲット設定]
    │   └── runtime.c              [C ランタイム]
    │
    └── wasm/
        ├── generator.hpp          [WASM生成]
        └── runtime_wasm.c         [WASMランタイム]
```

---

**作成者**: AI調査システム  
**バージョン**: v0.10.0 調査版  
**次回更新予定**: v0.11.0
