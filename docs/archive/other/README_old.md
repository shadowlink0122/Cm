# Cm言語ドキュメント

このディレクトリにはCm言語の設計文書、実装ガイド、チュートリアルが含まれます。

## 📁 ディレクトリ構造

```
docs/
├── README.md                 # このファイル
├── QUICKSTART.md            # クイックスタートガイド
├── PROJECT_STRUCTURE.md     # プロジェクト構造の説明
├── PROJECT_STATUS.md        # プロジェクトの現状
│
├── guides/                  # 機能ガイド（NEW）
│   ├── README.md
│   ├── getting-started.md
│   ├── type-system.md
│   ├── generics.md
│   ├── interfaces.md
│   ├── memory.md
│   └── ...
│
├── features/                # 機能別リファレンス（NEW）
│   ├── README.md
│   ├── arrays.md           # 配列
│   ├── pointers.md         # ポインタ
│   ├── with-keyword.md     # with自動実装
│   ├── match-expression.md # match式
│   └── ...
│
├── tutorials/               # チュートリアル（NEW）
│   ├── README.md
│   ├── hello-world.md
│   └── ...
│
├── design/                  # 設計文書
│   ├── README.md
│   ├── CANONICAL_SPEC.md    # ⭐ 正式言語仕様（最優先）
│   ├── architecture.md      # システムアーキテクチャ
│   ├── type_system.md       # 型システム
│   ├── hir.md              # HIR（高レベル中間表現）
│   ├── mir.md              # MIR（中レベル中間表現）
│   ├── memory_safety.md    # メモリ安全性
│   ├── module_system.md    # モジュールシステム
│   ├── codegen/            # コード生成
│   └── archive/            # 過去の設計文書
│
├── implementation/          # 実装詳細
│   ├── implementation_status.md
│   ├── implementation_progress_*.md
│   └── known_limitations.md
│
├── llvm/                    # LLVMバックエンド
│   ├── llvm_backend_implementation.md
│   ├── LLVM_OPTIMIZATION.md
│   ├── LLVM_RUNTIME_LIBRARY.md
│   ├── llvm_migration_plan.md
│   └── llvm_multiplatform.md
│
├── spec/                    # 言語仕様
│   ├── README.md
│   ├── grammar.md          # 文法定義
│   ├── memory.md           # メモリモデル
│   └── supported_versions.md
│
├── releases/                # リリースノート
│   ├── README.md
│   └── v0.10.0.md
│
├── progress/                # 開発進捗
│   └── 2025-12-MIR-completion.md
│
├── debug/                   # デバッグシステム
│   └── DEBUG_SYSTEM_IMPROVEMENTS.md
│
└── archive/                 # アーカイブ（旧文書）
    ├── CLEANUP_SUMMARY.md
    ├── STRUCT_*.md
    ├── STRING_INTERPOLATION_*.md
    └── ...
```

## 🚀 はじめに

### 1. 言語を学ぶ

1. **[QUICKSTART.md](QUICKSTART.md)** - 5分でCm言語を学ぶ
2. **[design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md)** - 正式言語仕様（最も重要）
3. **[examples/](../examples/)** - サンプルコード

### 2. コンパイラを理解する

1. **[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)** - プロジェクト構造
2. **[design/architecture.md](design/architecture.md)** - システム設計
3. **[design/hir.md](design/hir.md)** と **[design/mir.md](design/mir.md)** - 中間表現

### 3. 開発に参加する

1. **[CONTRIBUTING.md](../CONTRIBUTING.md)** - 貢献ガイド
2. **[implementation/implementation_status.md](implementation/implementation_status.md)** - 実装状況
3. **[ROADMAP.md](../ROADMAP.md)** - ロードマップ

## 📖 重要なドキュメント

### 必読

| ドキュメント | 説明 |
|------------|------|
| [design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md) | **正式言語仕様**（最優先） |
| [design/architecture.md](design/architecture.md) | システムアーキテクチャ |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | プロジェクト構造 |
| [QUICKSTART.md](QUICKSTART.md) | クイックスタート |

### 設計文書

| ドキュメント | 説明 |
|------------|------|
| [design/type_system.md](design/type_system.md) | 型システムの設計 |
| [design/memory_safety.md](design/memory_safety.md) | メモリ安全性 |
| [design/module_system.md](design/module_system.md) | モジュールシステム |
| [design/hir.md](design/hir.md) | HIR（高レベル中間表現） |
| [design/mir.md](design/mir.md) | MIR（中レベル中間表現） |

### LLVMバックエンド

| ドキュメント | 説明 |
|------------|------|
| [llvm/llvm_backend_implementation.md](llvm/llvm_backend_implementation.md) | LLVM実装詳細 |
| [llvm/LLVM_OPTIMIZATION.md](llvm/LLVM_OPTIMIZATION.md) | 最適化パイプライン |
| [llvm/LLVM_RUNTIME_LIBRARY.md](llvm/LLVM_RUNTIME_LIBRARY.md) | ランタイムライブラリ |

### 実装状況

| ドキュメント | 説明 |
|------------|------|
| [PROJECT_STATUS.md](PROJECT_STATUS.md) | プロジェクトの現状 |
| [implementation/implementation_status.md](implementation/implementation_status.md) | 機能実装状況 |
| [implementation/known_limitations.md](implementation/known_limitations.md) | 既知の制限事項 |

## 🔍 トピック別ガイド

### 型システム

- [design/type_system.md](design/type_system.md) - 基本的な型システム
- [design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md) - 正式仕様

### ジェネリクス

- [design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md) - ジェネリクスの仕様
- [design/generic_inference.md](design/generic_inference.md) - 型推論

### メモリ管理

- [design/memory_safety.md](design/memory_safety.md) - メモリ安全性
- [spec/memory.md](spec/memory.md) - メモリモデル

### コード生成

- [design/codegen/](design/codegen/) - 各バックエンドの設計
- [llvm/](llvm/) - LLVMバックエンド

## 📝 ドキュメント規約

### ファイル配置

- **設計文書** → `design/`
- **実装詳細** → `implementation/`
- **LLVMバックエンド** → `llvm/`
- **言語仕様** → `spec/`
- **リリースノート** → `releases/`
- **古い文書** → `archive/`

### ファイル命名

- 小文字とアンダースコア: `type_system.md`
- 大文字はキーワードのみ: `CANONICAL_SPEC.md`, `README.md`

### ドキュメントフォーマット

```markdown
# タイトル

## 概要
簡潔な説明

## 詳細
詳しい説明

## 例
コード例

## 関連ドキュメント
- リンク
```

## 🔄 更新履歴

- **2025-12-15**: v0.10.0リリースノート作成
- **2025-12-15**: ドキュメント構造整理
- **2025-12-14**: v0.8.0リリース
- **2025-12-14**: CANONICAL_SPEC.md更新

## 🆘 ヘルプ

質問や問題がある場合:

1. **[FAQ](design/README.md)** を確認
2. **[既知の制限事項](implementation/known_limitations.md)** を確認
3. **[GitHubイシュー](https://github.com/your-repo/Cm/issues)** を検索
4. 新しいイシューを作成

---

**Cm言語プロジェクトへようこそ！** 🎉
