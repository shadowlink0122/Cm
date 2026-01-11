# JIT差分コンパイル・キャッシュ機能 将来設計メモ

## 概要

**対象バージョン**: v0.12.0以降（v0.11.0では実装しない）

本ドキュメントは、JITおよびネイティブコンパイルにおける**差分コンパイル（インクリメンタルコンパイル）**と**キャッシュ機能**の将来設計メモです。

---

## 背景

Python 3.13のJITコンパイラ（copy-and-patch JIT）は以下を実装：
- **バイトコードキャッシュ** (`.pyc`ファイル)
- **JITコードキャッシュ** (実行時最適化結果の保存)
- **差分コンパイル** (変更されたモジュールのみ再コンパイル)

これにより：
- 2回目以降の実行が高速化
- 大規模プロジェクトでのビルド時間削減
- ホットパス最適化の永続化

---

## Cmへの適用案

### 1. MIRキャッシュ

```
.cm_cache/
├── module_hash.json        # ソースファイル→ハッシュマッピング
├── mir/
│   ├── main.mir           # シリアライズされたMIR
│   └── utils.mir
└── llvm/
    ├── main.bc            # LLVM bitcode
    └── utils.bc
```

**ハッシュ戦略**:
```
ソースファイルハッシュ = SHA256(ファイル内容 + 依存モジュールハッシュ)
```

### 2. 差分コンパイルフロー

```
[ソース変更検出] → [変更モジュール特定] → [MIR再生成]
                                           ↓
                         [既存キャッシュ] ← [リンク]
                                           ↓
                                    [LLVM IR/JIT生成]
```

### 3. JITプロファイル活用

```cpp
// 実行プロファイルの保存
struct JITProfile {
    std::map<std::string, uint64_t> hotFunctions;  // 呼び出し回数
    std::map<std::string, uint64_t> executionTime; // 実行時間
    std::map<std::string, int> optimizationLevel;  // 適用済み最適化
};

// プロファイルベース最適化
void JITEngine::applyProfileGuidedOptimization(const JITProfile& profile) {
    for (auto& [func, count] : profile.hotFunctions) {
        if (count > HOT_THRESHOLD) {
            applyAggressiveOptimization(func);
        }
    }
}
```

---

## 実装フェーズ案

### Phase 1: MIRキャッシュ（v0.12.0）
- MIRのシリアライゼーション/デシリアライゼーション実装
- ファイルハッシュベースのキャッシュ有効性チェック
- `--cache`/`--no-cache`オプション追加

### Phase 2: LLVM Bitcodeキャッシュ（v0.12.x）
- LLVM bitcode (`.bc`)の保存と読み込み
- LTO (Link-Time Optimization)との統合
- 大規模プロジェクトでの検証

### Phase 3: JITプロファイリング（v0.13.0）
- 実行時プロファイル収集
- プロファイルベース最適化 (PGO)
- ホットパス自動検出と最適化

### Phase 4: 分散キャッシュ（将来）
- CI/CDでのキャッシュ共有
- リモートキャッシュサーバー
- チーム開発での活用

---

## 技術的考慮事項

### キャッシュ無効化条件
- ソースファイル変更
- 依存モジュール変更
- コンパイラバージョン変更
- 最適化レベル変更
- ターゲットアーキテクチャ変更

### セキュリティ
- キャッシュファイルの整合性チェック
- 署名付きキャッシュ（オプション）
- サンドボックス化されたキャッシュディレクトリ

### ディスク容量
- キャッシュサイズ上限設定
- LRUベースの自動クリーンアップ
- `cm cache clean`コマンド

---

## 参考リンク

- [Python 3.13 JIT](https://docs.python.org/3/whatsnew/3.13.html)
- [LLVM ThinLTO](https://llvm.org/docs/ThinLTO.html)
- [Rust Incremental Compilation](https://blog.rust-lang.org/2016/09/08/incremental.html)
- [ccache](https://ccache.dev/)

---

## ステータス

**現在**: 検討段階メモ（v0.11.0では未実装）
**優先度**: 中（パフォーマンス改善後に着手）
