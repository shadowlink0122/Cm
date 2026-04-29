# TODO/FIXMEの整理

**優先度**: 中  
**影響範囲**: プロジェクト管理  
**対象ファイル**: 複数  
**必要テスト**: 各TODO項目の実装に伴うユニットテスト

---

## 問題

TODO/FIXMEコメントが29箇所に散在し、追跡されていない。

---

## 統計（2026-04-29更新）

| マーカー | 件数 |
|---------|------|
| TODO | 29 |
| FIXME | 0 |

---

## 主要なTODO（カテゴリ別）

### フロントエンド (8件)

| ファイル | 内容 |
|---------|------|
| `lint/lint_runner.hpp:31` | 各診断チェックを実行 |
| `frontend/types/checking/utils.cpp:327` | 変数参照の追跡 |
| `frontend/types/checking/expr.cpp:878` | サブパターンの再帰チェック |
| `frontend/types/checking/expr.cpp:1009` | バインディング変数の型設定 |
| `frontend/parser/parser_module.cpp:782` | constexprフラグ設定 |
| `frontend/parser/parser_module.cpp:791` | ConstExprDeclノード作成 |
| `frontend/parser/parser_module.cpp:819` | テンプレート対応 |
| `frontend/ast/decl.hpp:417` | パーサー更新時の修正 |

### HIR/MIR (7件)

| ファイル | 内容 |
|---------|------|
| `frontend/ast/types.hpp:192` | 構造体サイズ計算 |
| `hir/lowering/impl.cpp:666` | モノモーフィゼーション後のサイズ計算 |
| `mir/passes/cleanup/dce.cpp:339` | 詳細な副作用解析 |
| `mir/passes/scalar/folding.cpp:367` | 浮動小数点演算の畳み込み |
| `mir/lowering/lowering.cpp:1623` | より良いハッシュ関数（FNV-1a等） |
| `mir/lowering/lowering.cpp:1662` | 型に応じたハッシュ計算 |
| `mir/lowering/stmt.cpp:285` | 構造体のサイズ計算 |

### コード生成 (10件)

| ファイル | 内容 |
|---------|------|
| `codegen/sv/codegen.cpp:2183` | sv::packed属性チェック |
| `codegen/sv/codegen.cpp:2199` | より複雑な文のサポート |
| `codegen/js/optimizations/minification/js_minifier.cpp:256` | 高度なインライン化 |
| `codegen/llvm/core/intrinsics.hpp:181` | atomic load/store/cmpxchg |
| `codegen/llvm/core/intrinsics.hpp:325` | Intrinsics::IDから自動生成 |
| `codegen/llvm/core/terminator.cpp:181` | enum_info_マップ追加 |
| `codegen/llvm/optimizations/vectorization/vectorizer.cpp:197` | ベクトル化処理 |
| `codegen/llvm/optimizations/vectorization/vectorizer.cpp:216` | ベクトル化値使用 |
| `codegen/llvm/optimizations/vectorization/vectorizer.cpp:248` | 完全実装 |
| `common/source_location.hpp:188` | より正確な実装 |

### その他 (4件)

| ファイル | 内容 |
|---------|------|
| `mir/lowering/lowering.cpp:2852` | initial block内の文変換 |
| `mir/lowering/expr_call.cpp:315` | 完全な式パーサー |
| `mir/lowering/expr_call.cpp:2128` | printlnエラー報告 |
| `mir/lowering/auto_impl/generator.cpp:119` | 完全実装の移動 |

---

## 影響

- 技術的負債の可視化
- 優先度に基づく計画的対応
- 新規コントリビューターへの明確なタスク

