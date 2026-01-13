# Cm言語 最適化されたimport解決設計

**文書番号:** 083
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 設計最適化

## 1. 核心的な最適化

### 1.1 解析レベルの分離

```cpp
enum class AnalysisLevel {
    // フル解析（Lintターゲット）
    Full,         // 型チェック + Lintルール全て（L001-L999）

    // 型定義のみ（import先）
    TypesOnly,    // エクスポート型とシンボルのみ抽出
};
```

### 重要な洞察
- **指定ファイル**: フルLint（スタイル、命名規則、複雑度など）
- **import先**: 型定義の抽出のみ（Lintルール不要）

## 2. 2段階解析アーキテクチャ

```
cm check -r src/
    │
    ▼
┌──────────────────────────────────────┐
│  Phase 1: 型定義の収集（高速）          │
│  - 全import先を浅く解析                │
│  - エクスポートされた型・関数シグネチャ │
│  - Lintルールはスキップ                │
└──────────────────────────────────────┘
    │
    ▼
┌──────────────────────────────────────┐
│  Phase 2: フルLint（ターゲットのみ）     │
│  - 指定されたファイルのみフル解析        │
│  - Phase 1の型情報を使用              │
│  - 全Lintルール適用                   │
└──────────────────────────────────────┘
```

## 3. 実装設計

### 3.1 軽量パーサー（型定義専用）

```cpp
class LightweightTypeExtractor {
    // import先の高速解析用
    struct ExportedTypes {
        // 型定義
        map<string, TypeInfo> types;

        // 関数シグネチャ（実装は不要）
        map<string, FunctionSignature> functions;

        // 変数・定数の型
        map<string, Type*> variables;

        // インターフェース定義
        map<string, InterfaceInfo> interfaces;
    };

    // 軽量解析（スタイルチェックなし）
    ExportedTypes extract_types(const string& filepath) {
        auto tokens = lexer.tokenize_minimal(filepath);  // Trivia無視
        auto ast = parser.parse_declarations_only(tokens);  // 宣言のみ

        ExportedTypes result;

        // トップレベル宣言のみ走査
        for (const auto& node : ast->declarations) {
            switch (node->kind) {
            case NodeKind::TypeDef:
                result.types[node->name] = extract_type_info(node);
                break;

            case NodeKind::FunctionDecl:
                // 実装は無視、シグネチャのみ
                result.functions[node->name] = {
                    .params = node->params,
                    .return_type = node->return_type
                };
                break;

            case NodeKind::StructDecl:
                result.types[node->name] = extract_struct_info(node);
                break;

            case NodeKind::InterfaceDecl:
                result.interfaces[node->name] = extract_interface(node);
                break;

            case NodeKind::GlobalVar:
                if (node->is_exported) {
                    result.variables[node->name] = node->type;
                }
                break;
            }
        }

        return result;
    }
};
```

### 3.2 最適化された解析エンジン

```cpp
class OptimizedModuleAnalyzer {
private:
    // 軽量キャッシュ（型情報のみ）
    unordered_map<string, ExportedTypes> type_cache;

    // フル解析キャッシュ（Lintターゲットのみ）
    unordered_map<string, FullAnalysisResult> full_cache;

public:
    DiagnosticResult analyze_recursive(const string& path, bool recursive) {
        // ステップ1: ターゲットファイルの収集
        auto target_files = collect_target_files(path, recursive);

        // ステップ2: 依存関係の探索（BFS）
        set<string> all_dependencies;
        for (const auto& file : target_files) {
            collect_dependencies_bfs(file, all_dependencies);
        }

        // ステップ3: 依存ファイルの型抽出（軽量・並列可）
        parallel_for(all_dependencies, [this](const string& dep) {
            if (!type_cache.count(dep)) {
                type_cache[dep] = extract_types_only(dep);
            }
        });

        // ステップ4: ターゲットのフル解析（型情報利用）
        DiagnosticResult result;
        for (const auto& target : target_files) {
            auto diags = analyze_full(target);
            result.merge(diags);
        }

        return result;
    }

private:
    // 型定義のみ抽出（高速）
    ExportedTypes extract_types_only(const string& filepath) {
        LightweightTypeExtractor extractor;
        return extractor.extract_types(filepath);
    }

    // フル解析（Lintターゲットのみ）
    vector<Diagnostic> analyze_full(const string& filepath) {
        // import先の型情報を収集
        ImportedTypes imported;
        for (const auto& import : get_imports(filepath)) {
            imported.merge(type_cache[import]);
        }

        // フルパース（Trivia含む）
        auto ast = parse_with_trivia(filepath);

        // 統一セマンティック解析（型チェック + Lint）
        UnifiedSemanticAnalyzer analyzer;
        analyzer.set_imported_types(imported);
        analyzer.enable_all_lint_rules();  // フルLint
        analyzer.analyze(ast);

        return analyzer.get_diagnostics();
    }
};
```

## 4. パフォーマンス比較

### 4.1 従来の方法（全ファイルフルLint）

```
プロジェクト: 1000ファイル
ターゲット: 10ファイル
依存: 200ファイル

処理時間:
- 210ファイル × フル解析(100ms) = 21秒
```

### 4.2 最適化版（型定義のみ抽出）

```
処理時間:
- 200ファイル × 型抽出(10ms) = 2秒
- 10ファイル × フル解析(100ms) = 1秒
合計: 3秒

→ 7倍高速化
```

## 5. 段階的解析の詳細

### 5.1 Phase 1: 型定義収集（並列実行可能）

```cpp
class TypeCollectionPhase {
    // 必要最小限のパース
    void collect_type_definitions(const string& filepath) {
        // スキップするもの：
        // - 関数の実装本体
        // - ローカル変数
        // - 制御フロー
        // - コメント・空白（Trivia）
        // - プライベートメンバー

        // 抽出するもの：
        // - パブリックな型定義
        // - 関数シグネチャ
        // - エクスポートされた変数の型
        // - インターフェース定義
    }
};
```

### 5.2 Phase 2: フルLint（ターゲットのみ）

```cpp
class FullLintPhase {
    void analyze_with_lint(const string& filepath, const ImportedTypes& types) {
        // フルチェック：
        // - E001-E999: 全エラー
        // - W001-W999: 全警告
        // - L001-L999: 全Lintルール

        // 利用可能な情報：
        // - import先の型定義（Phase 1から）
        // - このファイルの完全なAST
        // - Trivia（インデント、コメント）
    }
};
```

## 6. キャッシュ戦略の最適化

### 6.1 2層キャッシュ

```cpp
class TwoTierCache {
    // 第1層: 型キャッシュ（小さい・長期保持）
    struct TypeCache {
        size_t avg_entry_size = 1KB;  // 型情報のみ
        time_t ttl = 3600 * 24;       // 24時間
        size_t max_entries = 10000;   // 大量にキャッシュ可能
    };

    // 第2層: フル解析キャッシュ（大きい・短期保持）
    struct FullCache {
        size_t avg_entry_size = 100KB;  // AST + 診断結果
        time_t ttl = 3600;              // 1時間
        size_t max_entries = 100;       // 限定的
    };
};
```

### 6.2 キャッシュヒット率の向上

```cpp
// 型定義は変更が少ない → 高いキャッシュヒット率
// 例: std.cm, math.cm などの標準ライブラリ

bool should_invalidate_type_cache(const string& filepath) {
    // 型定義に影響する変更のみ無効化
    // - 関数シグネチャの変更
    // - 構造体定義の変更
    // - 新しいエクスポート

    // 無視する変更：
    // - 関数実装の変更
    // - ローカル変数の変更
    // - コメント・空白の変更
}
```

## 7. import解決の最適化

### 7.1 選択的import

```cpp
// ファイル: math.cm
export {
    func add(int a, int b) -> int;  // エクスポート
    func multiply(int a, int b) -> int;  // エクスポート

    // プライベート（スキップ可能）
    static func helper() { ... }
    static int internal_counter = 0;
}

// 使用側
import { add } from "math.cm";  // addの型情報のみ必要
```

### 7.2 遅延解析

```cpp
class LazyImportResolver {
    // 実際に使用されるまで解析しない
    void resolve_on_demand(const string& symbol) {
        if (!is_resolved(symbol)) {
            // このシンボルを含むモジュールのみ解析
            auto module = find_module_containing(symbol);
            type_cache[module] = extract_minimal_types(module, {symbol});
        }
    }
};
```

## 8. CLI使用例

### 8.1 基本的な使用

```bash
# カレントファイルのみフルLint（import先は型のみ）
cm check main.cm

# フォルダ再帰（ターゲットのみフルLint）
cm check -r src/

# import先もフルLintしたい場合（明示的）
cm check -r src/ --lint-imports
```

### 8.2 パフォーマンス統計

```
$ cm check -r src/ --stats

=== Analysis Summary ===
Target files (full lint): 10
Import dependencies (types only): 187
Type extraction time: 1.2s
Full analysis time: 0.8s
Total time: 2.0s

Cache statistics:
- Type cache hits: 145/187 (77.5%)
- Full cache hits: 2/10 (20.0%)

Skipped checks in imports:
- Style checks (L001-L099): 18,700 rules
- Complexity checks (L150-L199): 3,740 rules
- Performance hints (L350-L399): 3,740 rules
Time saved: ~19s
```

## 9. 実装の利点

### メリット

1. **大幅な高速化**: import先のLintをスキップ
2. **メモリ効率**: 型情報のみ保持
3. **スケーラビリティ**: 大規模プロジェクトでも実用的
4. **キャッシュ効率**: 型定義は変更頻度が低い

### デメリット対策

```yaml
# .cmlint.yml - import先もチェックしたい場合
lint:
  imports:
    # 特定のimportはフルチェック
    full_check:
      - "src/critical/*.cm"  # 重要なモジュール

    # 特定のルールのみ適用
    rules:
      - E100-E199  # 型エラーのみ
      - E200-E299  # 所有権エラーのみ
```

## 10. 関連ドキュメント

- [082_recursive_module_analysis.md](./082_recursive_module_analysis.md) - 再帰解析の基本設計
- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - 診断システム
- [081_implementation_guide.md](./081_implementation_guide.md) - 実装ガイド