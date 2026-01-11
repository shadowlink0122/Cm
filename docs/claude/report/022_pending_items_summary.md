# 未対応項目サマリー

作成日: 2026-01-11
ステータス: 調査中

## 概要

アーカイブされたドキュメント(001-021)から抽出した、部分的に未対応の項目一覧。

---

## 高優先度

### 1. 巨大ファイル分割（リファクタリング）
**出典**: 019_refactoring_audit, 021_comprehensive_fixes

| ファイル | 行数 | 推奨 |
|----------|------|------|
| parser.cpp | 3,826行 | 機能別に分割 |
| lowering.cpp | 3,456行 | パス別に分割 |
| mir_to_llvm.cpp | 2,000行+ | コンポーネント別分割 |

### 2. イテレータシステム完全実装
**出典**: 004_iterator_design, 005_builtin_iterator

- `Iterator<T>`トレイト標準化
- `for item in collection` 構文サポート
- map/filter/fold高階関数

---

## 中優先度

### 3. 開発者ツール
**出典**: 003_implementation_roadmap, 021_comprehensive_fixes

- [ ] REPL実装
- [ ] 基本LSPサポート  
- [ ] デバッガ統合

### 4. STL実装
**出典**: 001_stl_implementation_analysis

- [ ] HashMap
- [ ] HashSet
- [ ] BTreeMap
- [ ] LinkedList

---

## 低優先度

### 5. JITコンパイラ
**出典**: 021_comprehensive_fixes

### 6. パッケージマネージャー
**出典**: 003_implementation_roadmap

---

## 今セッションで追加対応済み

- ✅ ジェネリック構造体フィールド型置換 (`mir_to_llvm.cpp`)
- ✅ プリミティブ型impl修正 (`operators.cpp`)
- ✅ import時O3最適化制限解除 (`codegen.hpp`)
- ✅ 暗黙的最適化フォールバック廃止 (`pass_limiter.hpp`, `mir_pattern_detector.hpp`)
