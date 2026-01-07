[English](file_structure_reorganization.en.html)

# ファイル構造再編成計画

## 現状の問題点

1. **冗長な命名**: `optimizations/optimization_*`、`optimizations/constant_folding.hpp`など
2. **フラットな構造**: `codegen/llvm/*.cpp`が細分化されていない
3. **不適切な配置**: `buffered_*`ファイルが散在
4. **重複したバージョン**: `optimization_pass.hpp`と`optimization_pass_v2.hpp`

## 提案する新構造

### MIR層の再編成

```
src/mir/
├── analysis/                    # 解析インフラ
│   ├── dataflow/               # データフロー解析
│   │   ├── framework.hpp       # 汎用データフロー枠組み
│   │   ├── liveness.hpp        # 生存性解析
│   │   └── reaching_defs.hpp   # 到達定義解析
│   ├── control_flow/           # 制御フロー解析
│   │   ├── cfg.hpp            # CFG構築
│   │   ├── dominance.hpp      # 支配関係
│   │   └── loops.hpp          # ループ検出・解析
│   └── memory/                 # メモリ解析
│       ├── alias.hpp           # エイリアス解析
│       └── escape.hpp          # エスケープ解析
│
├── passes/                      # 最適化パス（"optimization"接頭辞を削除）
│   ├── core/                   # 基本インフラ
│   │   ├── base.hpp           # 基底クラス（旧optimization_pass.hpp）
│   │   ├── manager.hpp        # パスマネージャ（旧all_passes.hpp）
│   │   └── pipeline.cpp       # パイプライン実装
│   ├── scalar/                 # スカラー最適化
│   │   ├── folding.hpp        # 定数畳み込み
│   │   ├── propagation.hpp    # コピー/定数伝播
│   │   ├── sccp.hpp           # Sparse Conditional Constant Propagation
│   │   └── algebraic.hpp      # 代数的簡約化
│   ├── cleanup/                # クリーンアップ系
│   │   ├── dce.hpp            # デッドコード除去（関数レベル）
│   │   ├── dse.hpp            # デッドストア除去
│   │   ├── program_dce.hpp    # プログラム全体のDCE
│   │   └── simplify_cfg.hpp   # CFG簡約化
│   ├── redundancy/             # 冗長性除去
│   │   ├── gvn.hpp            # Global Value Numbering
│   │   ├── cse.hpp            # 共通部分式除去
│   │   └── pre.hpp            # Partial Redundancy Elimination
│   ├── loop/                   # ループ最適化
│   │   ├── licm.hpp           # ループ不変式移動
│   │   ├── unroll.hpp         # ループアンローリング
│   │   └── vectorize.hpp      # ループベクトル化
│   ├── interprocedural/        # 関数間最適化
│   │   ├── inlining.hpp       # インライン化
│   │   ├── ipa.hpp            # 関数間解析
│   │   └── devirtualize.hpp   # 脱仮想化
│   └── convergence/            # 収束管理
│       ├── manager.hpp         # 基本収束マネージャ
│       └── smart.hpp           # スマート収束戦略
│
└── transform/                   # MIR変換（最適化以外）
    ├── lowering.hpp            # HIR→MIR変換
    ├── lifting.hpp             # MIR→HIR逆変換
    └── normalization.hpp       # 正規化

```

### CodeGen層の再編成

```
src/codegen/
├── llvm/
│   ├── core/                   # コア機能（既存）
│   │   ├── context.hpp         # LLVMコンテキスト管理
│   │   ├── builder.hpp         # IRビルダー
│   │   ├── mir_to_llvm.cpp    # MIR→LLVM変換
│   │   ├── types.cpp          # 型変換
│   │   └── utils.cpp          # ユーティリティ
│   │
│   ├── emit/                   # 命令生成
│   │   ├── operators.cpp      # 演算子
│   │   ├── terminator.cpp     # 終端命令
│   │   ├── intrinsics.cpp     # 組み込み関数
│   │   └── print.cpp          # print文生成
│   │
│   ├── interface/              # インターフェース
│   │   ├── codegen.hpp        # メインインターフェース
│   │   └── interface.cpp      # interface/impl実装
│   │
│   ├── native/                 # ネイティブターゲット（既存）
│   │   ├── target.hpp          # ターゲット設定
│   │   └── linker.hpp         # リンカー統合
│   │
│   ├── wasm/                   # WebAssemblyターゲット
│   │   ├── wasm_codegen.hpp   # WASM生成
│   │   └── wasm_target.hpp    # WASMターゲット設定
│   │
│   ├── monitoring/             # モニタリング・デバッグ
│   │   ├── block_monitor.hpp  # ブロック監視
│   │   ├── output_monitor.hpp # 出力監視
│   │   ├── guard.hpp          # コード生成ガード
│   │   └── buffered.hpp      # バッファリング機能
│   │
│   └── passes/                 # LLVM固有の最適化
│       ├── custom_passes.hpp   # カスタムLLVMパス
│       ├── lto.hpp            # Link Time Optimization
│       └── vectorize.hpp      # 自動ベクトル化設定
│
├── interpreter/                # インタプリタ（既存構造維持）
│   └── ...
│
├── js/                        # JavaScript生成
│   ├── core/
│   │   ├── js_codegen.hpp    # JS生成メイン
│   │   └── js_types.hpp      # 型マッピング
│   └── passes/
│       ├── minify.hpp         # コード圧縮
│       └── tree_shake.hpp    # ツリーシェイキング
│
└── common/                     # 共通コンポーネント
    ├── target.hpp             # ターゲット抽象化
    ├── options.hpp            # コンパイルオプション
    └── output.hpp             # 出力管理
```

## 移行手順

### Phase 1: MIR最適化の再編成
1. `mir/passes/`の新構造作成
2. 既存ファイルの移動・リネーム
3. 冗長な`optimization_`接頭辞の削除
4. 重複バージョンの統合

### Phase 2: LLVM CodeGenの整理
1. `llvm/emit/`、`llvm/monitoring/`の作成
2. 散在する`.cpp`ファイルの適切な配置
3. `buffered_*`ファイルを`monitoring/`へ統合

### Phase 3: ヘッダー更新
1. include パスの更新
2. CMakeLists.txt の更新
3. ビルドテスト

## 利点

1. **冗長性の排除**: `optimizations/optimization_*` → `passes/*`
2. **明確な階層**: 機能別にサブディレクトリ化
3. **発見しやすさ**: 関連ファイルがグループ化
4. **拡張性**: 新機能追加時の配置が明確

## 統合に関する提案

### ROADMAP.mdとの統合について

現在のROADMAP.md（2765行）は既に大規模なため、最適化ロードマップの統合は推奨しません。代わりに：

1. **ROADMAP.md**: 主要なマイルストーンのみ記載（1段落程度）
   ```markdown
   ## Version 0.12.0 - 最適化インフラ強化
   MIR最適化パスの拡充とインポート時の無限ループ問題の解決。
   詳細: [最適化ロードマップ](docs/optimization/roadmap.html)
   ```

2. **docs/optimization/**: 最適化関連文書を集約
   ```
   docs/optimization/
   ├── roadmap.md              # 最適化ロードマップ（旧mir_optimization_roadmap.md）
   ├── implementation.md       # 実装詳細（旧implementation_details.md）
   ├── benchmarks.md          # パフォーマンス測定結果
   └── issues/
       └── import_hang.md     # インポート無限ループ問題
   ```

## CMakeLists.txt更新例

```cmake
# MIR Passes
set(MIR_PASS_SOURCES
    src/mir/passes/core/base.hpp
    src/mir/passes/core/manager.hpp
    src/mir/passes/core/pipeline.cpp
    src/mir/passes/scalar/folding.hpp
    src/mir/passes/scalar/propagation.hpp
    # ...
)

# LLVM CodeGen
set(LLVM_CODEGEN_SOURCES
    src/codegen/llvm/core/context.cpp
    src/codegen/llvm/core/mir_to_llvm.cpp
    src/codegen/llvm/emit/operators.cpp
    src/codegen/llvm/emit/terminator.cpp
    # ...
)
```

## 影響範囲

- **ビルドシステム**: CMakeLists.txt の更新必要
- **インクルード**: 全ソースファイルのinclude文更新
- **ドキュメント**: 開発者向けドキュメントの更新
- **CI/CD**: ビルドパスの確認

この再編成により、コードベースの保守性と拡張性が大幅に向上します。