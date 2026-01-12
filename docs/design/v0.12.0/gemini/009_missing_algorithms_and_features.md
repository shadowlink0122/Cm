[English](009_missing_algorithms_and_features.en.html)

# Cm言語コンパイラ・インタプリタにおける未実装アルゴリズムと機能の分析レポート

## 概要
本レポートでは、現在のCm言語（`Cm`）のコンパイラおよびインタプリタ実装において、Rust, C++, TypeScript, Go, Pythonなどの現代的なプログラミング言語や商用コンパイラで標準的に採用されているが、Cmにはまだ実装されていない、あるいは不十分であるアルゴリズムや技術要素を列挙します。

## 1. フロントエンド (Frontend) 技術

### 1.1 高度なエラー回復 (Sophisticated Error Recovery)
- **現状**: 基本的なパニックモード回復（同期トークンまでスキップ）のみの実装と推測されます。
- **欠如している技術**:
  - **以前の文脈を利用した回復**: 挿入・削除・置換を試行して構文解析を続行するアルゴリズム。
  - **Global Error Repair**: プログラム全体で最小の修正コストを見つけるアルゴリズム。
  - **Typo Correction**: 編集距離（Levenshtein距離）を用いた識別子の修正提案（"Did you mean...?"）。

### 1.2 増分解析 (Incremental Parsing)
- **現状**: ファイル全体を毎回再解析しています。
- **欠如している技術**:
  - **Incremental Parsing**: 変更された部分のみを再解析し、ASTを部分的に更新する技術（LSP/IDE支援に必須）。
  - **Tree-sitter** のようなGLR (Generalized LR) パーシングまたはPackrat Parsingの概念。

## 2. 型システムと意味解析 (Type System & Semantic Analysis)

### 2.1 高度なフロー解析 (Advanced Control Flow Analysis)
- **現状**: 基本的な型チェックと初期化チェックは存在しますが、限定的です。
- **欠如している技術**:
  - **Control Flow Graph (CFG) Construction**: 基本ブロックとエッジによるグラフ化。
  - **Dominator Tree**: 非初期化変数の厳密な検出やループ最適化に使用。
  - **Reachability Analysis**: 到達不能コードの削除（Dead Code Elimination）。
  - **TypeScriptのようなNarrowing**: `if (x is String)` ブロック内での型絞り込み。

### 2.2 借用チェッカーとライフタイム (Borrow Checking & Lifetimes)
- **現状**: RAII（コンストラクタ/デストラクタ）はありますが、Rustのようなコンパイル時の厳密な所有権・借用・ライフタイム解析は未実装に見えます。
- **欠如している技術**:
  - **Non-lexical Lifetimes (NLL)**: スコープベースより柔軟なライフタイム解析。
  - **Move Semanticsの静的解析**: 値の移動後のアクセス禁止の厳密な追跡。

## 3. 中間表現と最適化 (IR & Optimization)

### 3.1 静的単一代入形式 (SSA: Static Single Assignment)
- **現状**: MIRが存在しますが、完全なSSA形式（Phi関数の挿入、変数名のバージョン管理）への変換が行われているか不明瞭、あるいは簡易的です。
- **欠如している技術**:
  - **Dominance Frontier** の計算アルゴリズム。
  - **Phi Node Insertion**: 分岐合流点での変数のマージ。
  - **SSA-based Optimizations**:
    - **Constant Propagation (定数伝播)**: SSA上では効率的に行える。
    - **Common Subexpression Elimination (CSE, 共通部分式削除)**: GVN (Global Value Numbering) など。

### 3.2 ループ最適化 (Loop Optimizations)
- **現状**: 基本的な変換のみ。
- **欠如している技術**:
  - **Loop Invariant Code Motion (LICM)**: ループ内で不変な計算を外に出す。
  - **Loop Unrolling (ループ展開)**: ループ回数が既知の場合の展開。
  - **Induction Variable Elimination**: 誘導変数の簡約化。
  - **Auto-Vectorization (自動ベクトル化)**: SIMD命令への変換。

### 3.3 メメモリアロケーション最適化
- **現状**: `malloc`/`free` (new/delete) ベースの手動管理に近いRAII。
- **欠如している技術**:
  - **Escape Analysis (エスケープ解析)**: ヒープ割り当てをスタック割り当てに昇格させる（GoやJavaで重要）。
  - **Region-based Memory Management**: 関連するオブジェクトをまとめて解放する。

## 4. バックエンドと実行環境 (Backend & Runtime)

### 4.1 JIT コンパイラ (Just-In-Time Compilation)
- **現状**: インタプリタ実行またはAOT（Ahead-Of-Time）コンパイル（LLVM経由）のみ。
- **欠如している技術**:
  - **Tracing JIT**: 実行頻度の高いパス（ホットスポット）を検出して機械語にコンパイルする（Python PyPy, JS V8など）。
  - **Method JIT**: メソッド単位でコンパイルする（Java HotSpotなど）。
  - **Inline Caching**: 動的ディスパッチ（メソッド呼び出し）の高速化（Polymorphic Inline Cache）。
  - **Deoptimization (On-Stack Replacement)**: 最適化されたコードからインタプリタへの復帰。

### 4.2 ガベージコレクション (Garbage Collection)
- **現状**: 参照カウントやRAIIによる決定的な解放（Rust/C++寄り）。
- **方針**: **意図的に除外**。システム言語としての決定論的動作を重視するため、GCは導入しません。
- **JSバックエンドへの対応**:
  - JS環境（ブラウザ/Node.js）にはGCが存在しますが、Cm言語としてはそれに依存せず、**RAIIセマンティクスを維持**します。
  - 具体的には、スコープ終了時のデストラクタ呼び出しをJSの `try...finally` ブロックにトランスパイルすることで、JS上でも決定論的なリソース破棄（`dispose`等の呼び出し）を保証します。これにより、メモリ管理はJSエンジンに任せつつ、論理的なリソース管理は厳密に行います。

### 4.3 レジスタ割り当て (Register Allocation)
- **現状**: LLVMに依存していると思われますが、自前バックエンドを持つ場合は以下が必要です。
- **欠如している技術**:
  - **Graph Coloring Register Allocation**: 干渉グラフを用いた古典的アルゴリズム（Chaitinなど）。
  - **Linear Scan Register Allocation**: JITなどで使われる高速な割り当てアルゴリズム。

## 5. ツールチェーンとエコシステム

### 5.1 言語サーバー (LSP: Language Server Protocol)
- **現状**: コンパイラのみ。
- **欠如している技術**:
  - コード補完、定義へ移動、リファクタリング（名前変更）などのためのインデックス化アルゴリズム。

### 5.2 REPL (Read-Eval-Print Loop)
- **現状**: ファイル入力のみ。
- **欠如している技術**:
  - 行単位での増分実行と状態保持。

## まとめ
Cmは現在、C++やRustに近い静的コンパイル言語の基礎を持っていますが、現代的なコンパイラが備える「最適化の深さ」と「開発体験を向上させるツール連携」の面で多くのアルゴリズムが未実装です。特に、**SSA形式に基づいたデータフロー解析と最適化**、および**JITコンパイル技術**の導入は、言語のパフォーマンスと柔軟性を大きく向上させる鍵となります。