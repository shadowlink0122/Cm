# TODO/FIXMEの整理

**優先度**: 中  
**影響範囲**: プロジェクト管理  
**対象ファイル**: 複数

---

## 問題

TODO/FIXMEコメントが30+箇所に散在し、追跡されていない。

---

## 統計

| マーカー | 件数 |
|---------|------|
| TODO | 30+ |
| FIXME | 0 |
| BUG修正（コメント残留） | 8 |
| 暫定/未実装（日本語） | 15+ |

---

## 主要なTODO

### 高優先度

| ファイル | 内容 |
|---------|------|
| `lint/lint_runner.hpp` | 各診断チェックの実装 |
| `ast/types.hpp` | 構造体サイズ計算 |
| `hir/lowering/impl.cpp` | モノモーフィゼーション後のサイズ計算 |

### 中優先度

| ファイル | 内容 |
|---------|------|
| `parser_module.cpp` | constexprフラグ、テンプレート対応 |
| `mir/passes/scalar/folding.cpp` | 浮動小数点演算の畳み込み |
| `macro/expander.cpp` | 繰り返しの展開実装 |

### 低優先度

| ファイル | 内容 |
|---------|------|
| `vectorizer.cpp` | ベクトル化の完全実装 |
| `common/source_location.hpp` | より正確な実装 |
| `intrinsics.hpp` | atomic操作、自動生成 |

---

## 修正案

### 1. GitHub Issues化

各TODOをGitHub Issueとして登録し、ラベル付け:
- `priority:high` / `priority:medium` / `priority:low`
- `type:enhancement` / `type:bug` / `type:tech-debt`

### 2. コメント形式の統一

```cpp
// TODO(#123): 説明
// FIXME(#124): 説明
```

### 3. BUG修正コメントの削除

修正済みの「BUG修正」コメントは削除し、gitログに委ねる。

---

## 影響

- 技術的負債の可視化
- 優先度に基づく計画的対応
- 新規コントリビューターへの明確なタスク

