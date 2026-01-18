# Cm言語 再帰的モジュール解析システム設計

**文書番号:** 082
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 設計中

## 1. 概要

フォルダの再帰的lint (`-r`オプション) とimportによる依存関係解析を効率的に実装する設計。重複処理と深すぎるimportチェーンの問題を解決する。

### 重要な最適化
**import先は型定義の抽出のみ**を行い、フルLintはターゲットファイルのみに適用する。これにより大幅な高速化を実現。

## 2. 核心的な課題と解決策

### 2.1 課題

```
プロジェクト構造例:
src/
├── main.cm        (imports: lib/math.cm, lib/utils.cm)
├── app.cm         (imports: lib/math.cm, lib/utils.cm)
├── lib/
│   ├── math.cm    (imports: utils.cm, core/base.cm)
│   ├── utils.cm   (imports: core/base.cm)
│   └── core/
│       └── base.cm (imports: なし)
└── tests/
    └── test.cm    (imports: ../lib/math.cm)
```

**問題点:**
1. `math.cm`が3回解析される（main, app, test）
2. `base.cm`が深い階層で何度も参照される
3. 循環参照の可能性
4. 大規模プロジェクトでメモリ爆発

### 2.2 解決策：モジュールグラフとキャッシュ

```cpp
class ModuleGraph {
    // モジュール情報のキャッシュ
    map<string, ModuleInfo> module_cache;

    // 依存関係グラフ
    map<string, set<string>> dependencies;
    map<string, set<string>> dependents;

    // 解析状態
    enum State { NotStarted, InProgress, Completed };
    map<string, State> analysis_state;
};
```

## 3. 設計アーキテクチャ

### 3.1 全体フロー（最適化版）

```
cm check -r src/
    │
    ▼
┌─────────────────────────────────┐
│  1. ファイル収集フェーズ          │
│     - ターゲット: src/*.cm        │
│     - フルLint対象として記録       │
└─────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────┐
│  2. 依存グラフ構築フェーズ        │
│     - importを軽量パースで探索     │
│     - 循環参照検出               │
└─────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────┐
│  3. 型定義収集フェーズ（高速）     │
│     - import先: 型定義のみ抽出     │
│     - Lintルールはスキップ        │
│     - 並列処理可能               │
└─────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────┐
│  4. フルLintフェーズ              │
│     - ターゲットのみフル解析       │
│     - 収集した型情報を使用        │
│     - E/W/L全ルール適用          │
└─────────────────────────────────┘
```

### 3.2 モジュール解析エンジン（最適化版）

```cpp
class OptimizedRecursiveAnalyzer {
private:
    // 2種類のキャッシュ
    struct TypeOnlyCache {
        string filepath;
        time_t last_modified;

        // 型定義のみ（軽量）
        map<string, TypeInfo> exported_types;
        map<string, FunctionSignature> function_sigs;
        size_t memory_size;  // 約1KB/ファイル
    };

    struct FullAnalysisCache {
        string filepath;
        time_t last_modified;

        // フル解析結果（重量）
        unique_ptr<AST> ast;
        SymbolTable exported_symbols;
        TypeEnvironment types;
        vector<Diagnostic> diagnostics;  // E/W/L全て
        size_t memory_size;  // 約100KB/ファイル
    };

    unordered_map<string, TypeOnlyCache> type_cache;    // import先用
    unordered_map<string, FullAnalysisCache> full_cache; // ターゲット用

    // 依存関係グラフ
    class DependencyGraph {
        map<string, set<string>> imports;    // A imports B,C
        map<string, set<string>> importers;  // A is imported by X,Y

    public:
        // トポロジカルソート（解析順序を決定）
        vector<string> topological_sort() {
            vector<string> result;
            set<string> visited;
            set<string> visiting;  // 循環検出用

            for (auto& [module, _] : imports) {
                if (!visited.count(module)) {
                    dfs_visit(module, visited, visiting, result);
                }
            }

            return result;
        }

        // 循環参照チェック
        bool has_cycle() {
            // Tarjanのアルゴリズムなど
        }
    };

    DependencyGraph dep_graph;

public:
    // エントリーポイント（最適化版）
    DiagnosticResult analyze_recursive(const string& path, bool recursive) {
        // Phase 1: ターゲットファイル収集
        auto target_files = collect_files(path, recursive);
        set<string> targets(target_files.begin(), target_files.end());

        // Phase 2: 依存関係探索（BFS）
        set<string> all_dependencies;
        for (const auto& file : target_files) {
            collect_dependencies_bfs(file, all_dependencies);
        }

        // import先からターゲットを除外
        set<string> import_only;
        set_difference(all_dependencies.begin(), all_dependencies.end(),
                      targets.begin(), targets.end(),
                      inserter(import_only, import_only.begin()));

        // Phase 3: 型定義収集（import先、並列実行）
        parallel_for(import_only, [this](const string& filepath) {
            extract_types_only(filepath);  // 軽量解析
        });

        // Phase 4: フルLint（ターゲットのみ）
        DiagnosticResult result;
        for (const auto& target : target_files) {
            auto diags = analyze_full(target);
            result.merge(diags);
        }

        return result;
    }

private:
    // 型定義のみ抽出（import先用、高速）
    void extract_types_only(const string& filepath) {
        if (type_cache.count(filepath)) return;  // キャッシュ済み

        TypeOnlyCache& cache = type_cache[filepath];
        cache.filepath = filepath;

        // 軽量パース（Trivia無視、実装本体スキップ）
        auto tokens = lexer.tokenize_minimal(filepath);
        auto declarations = parser.parse_declarations_only(tokens);

        // 型定義とシグネチャのみ抽出
        for (const auto& decl : declarations) {
            if (decl->is_exported) {
                switch (decl->kind) {
                case DeclKind::Type:
                    cache.exported_types[decl->name] = extract_type_info(decl);
                    break;
                case DeclKind::Function:
                    cache.function_sigs[decl->name] = extract_signature(decl);
                    break;
                }
            }
        }

        cache.memory_size = estimate_size(cache);
    }

    // フル解析（ターゲット用、全Lintルール適用）
    vector<Diagnostic> analyze_full(const string& filepath) {
        FullAnalysisCache& cache = full_cache[filepath];
        cache.filepath = filepath;

        // import先の型情報を収集
        ImportedTypes imported;
        for (const auto& import : get_imports(filepath)) {
            if (type_cache.count(import)) {
                imported.merge(type_cache[import]);
            }
        }

        // フルパース（Trivia含む、全情報）
        auto source = read_file(filepath);
        cache.ast = parse_with_trivia(source);

        // 統一セマンティック解析（E/W/L全て）
        UnifiedSemanticAnalyzer analyzer;
        analyzer.set_imported_types(imported);
        analyzer.enable_all_rules();  // フルLint
        analyzer.analyze(cache.ast.get());

        // 結果を保存
        cache.diagnostics = analyzer.get_diagnostics();
        cache.memory_size = estimate_size(cache);

        return cache.diagnostics;
    }
};
```

## 4. import解決の深さ制限

### 4.1 深さ制限の設定

```cpp
class ImportDepthLimiter {
    static constexpr int DEFAULT_MAX_DEPTH = 10;
    static constexpr int DEFAULT_MAX_IMPORTS = 100;

    struct ImportContext {
        int current_depth = 0;
        int total_imports = 0;
        set<string> visited;  // 訪問済みモジュール
        vector<string> current_path;  // 現在のimportパス
    };

    bool should_analyze_import(const string& module, ImportContext& ctx) {
        // 深さチェック
        if (ctx.current_depth >= DEFAULT_MAX_DEPTH) {
            diag.report("W500", location, {
                "Import depth exceeds limit",
                to_string(DEFAULT_MAX_DEPTH)
            });
            return false;
        }

        // 総import数チェック
        if (ctx.total_imports >= DEFAULT_MAX_IMPORTS) {
            diag.report("W501", location, {
                "Too many imports in dependency tree",
                to_string(DEFAULT_MAX_IMPORTS)
            });
            return false;
        }

        // 既に訪問済み（ダイヤモンド依存はOK）
        if (ctx.visited.count(module)) {
            return false;  // スキップ（エラーではない）
        }

        return true;
    }
};
```

### 4.2 スマートキャッシング戦略

```cpp
class SmartCache {
    // LRUキャッシュ（メモリ制限）
    class LRUModuleCache {
        static constexpr size_t MAX_CACHE_SIZE = 100 * 1024 * 1024;  // 100MB

        struct CacheEntry {
            ModuleCache data;
            size_t size;
            time_t last_access;
        };

        map<string, CacheEntry> entries;
        size_t total_size = 0;

    public:
        void put(const string& key, ModuleCache&& module) {
            size_t module_size = estimate_size(module);

            // メモリ制限チェック
            while (total_size + module_size > MAX_CACHE_SIZE && !entries.empty()) {
                evict_lru();
            }

            entries[key] = {move(module), module_size, time(nullptr)};
            total_size += module_size;
        }

        ModuleCache* get(const string& key) {
            if (auto it = entries.find(key); it != entries.end()) {
                it->second.last_access = time(nullptr);
                return &it->second.data;
            }
            return nullptr;
        }

    private:
        void evict_lru() {
            // 最も古いエントリを削除
            auto oldest = min_element(entries.begin(), entries.end(),
                [](const auto& a, const auto& b) {
                    return a.second.last_access < b.second.last_access;
                });

            total_size -= oldest->second.size;
            entries.erase(oldest);
        }
    };
};
```

## 5. 並列処理の活用

### 5.1 並列解析戦略

```cpp
class ParallelAnalyzer {
    // 依存関係レベルごとに並列処理
    void analyze_parallel(const vector<vector<string>>& levels) {
        for (const auto& level : levels) {
            // 同じレベルのモジュールは並列処理可能
            parallel_for(level.begin(), level.end(), [this](const string& module) {
                analyze_module(module);
            });
        }
    }

    // 依存関係をレベル分け
    vector<vector<string>> compute_levels(const DependencyGraph& graph) {
        vector<vector<string>> levels;
        set<string> processed;

        while (processed.size() < graph.size()) {
            vector<string> current_level;

            // 依存がないか、依存が全て処理済みのモジュール
            for (const auto& [module, deps] : graph.imports) {
                if (!processed.count(module)) {
                    if (all_of(deps.begin(), deps.end(),
                              [&](const string& d) { return processed.count(d); })) {
                        current_level.push_back(module);
                    }
                }
            }

            for (const auto& m : current_level) {
                processed.insert(m);
            }

            levels.push_back(current_level);
        }

        return levels;
    }
};
```

## 6. CLI実装

### 6.1 コマンドラインオプション

```bash
# フォルダ再帰
cm check -r src/                    # src/以下を再帰的にチェック
cm check -r .                       # カレントディレクトリ以下すべて

# 深さ制限
cm check -r src/ --max-depth=5      # import深さを5に制限
cm check -r src/ --max-imports=50   # 総import数を50に制限

# キャッシュ制御
cm check -r src/ --no-cache         # キャッシュ無効
cm check -r src/ --cache-dir=.cm-cache  # キャッシュディレクトリ指定

# 並列処理
cm check -r src/ -j8                # 8スレッドで並列処理
cm check -r src/ -j auto            # CPU数に応じて自動

# 除外パターン
cm check -r src/ --exclude="tests/**" --exclude="*.generated.cm"

# 依存関係の可視化
cm check -r src/ --dump-deps        # 依存グラフを出力
cm check -r src/ --dump-deps-format=dot  # Graphviz形式
```

### 6.2 設定ファイル対応

```yaml
# .cmlint.yml
recursive:
  max_depth: 10
  max_imports: 100
  exclude:
    - "tests/**"
    - "vendor/**"
    - "*.generated.cm"

  # キャッシュ設定
  cache:
    enabled: true
    directory: .cm-cache
    max_size: 100MB
    ttl: 3600  # 1時間

  # 並列処理
  parallel:
    enabled: true
    threads: auto  # or number
```

## 7. インクリメンタル解析

### 7.1 ファイル変更検出

```cpp
class IncrementalAnalyzer {
    // ファイルの変更を検出
    bool is_modified(const string& filepath, const ModuleCache& cache) {
        struct stat st;
        if (stat(filepath.c_str(), &st) == 0) {
            return st.st_mtime > cache.last_modified;
        }
        return true;
    }

    // 影響を受けるモジュールを特定
    set<string> find_affected_modules(const string& modified_file) {
        set<string> affected;
        queue<string> to_check;

        // 変更されたファイルに依存するモジュール
        for (const auto& importer : dep_graph.get_importers(modified_file)) {
            to_check.push(importer);
        }

        // 推移的に影響を受けるモジュール
        while (!to_check.empty()) {
            string current = to_check.front();
            to_check.pop();

            if (affected.insert(current).second) {
                for (const auto& importer : dep_graph.get_importers(current)) {
                    to_check.push(importer);
                }
            }
        }

        return affected;
    }

    // インクリメンタル解析の実行
    void analyze_incremental(const set<string>& changed_files) {
        // 影響を受けるモジュールを特定
        set<string> to_reanalyze;
        for (const auto& file : changed_files) {
            to_reanalyze.insert(file);
            auto affected = find_affected_modules(file);
            to_reanalyze.insert(affected.begin(), affected.end());
        }

        // キャッシュを無効化
        for (const auto& file : to_reanalyze) {
            if (auto it = cache.find(file); it != cache.end()) {
                it->second.needs_reanalysis = true;
            }
        }

        // 再解析
        analyze_modules(to_reanalyze);
    }
};
```

## 8. エラー処理と診断

### 8.1 循環参照の検出と報告

```cpp
void report_circular_dependency(const vector<string>& cycle) {
    diag.report("E500", location, {
        "Circular dependency detected"
    });

    // 循環を視覚的に表示
    cout << "Dependency cycle:\n";
    for (size_t i = 0; i < cycle.size(); ++i) {
        cout << "  " << cycle[i];
        if (i < cycle.size() - 1) {
            cout << " → ";
        }
    }
    cout << " → " << cycle[0] << " (cycle)\n";
}
```

### 8.2 パフォーマンス統計（最適化版）

```cpp
class PerformanceStats {
    struct Stats {
        // ファイル統計
        int target_files = 0;         // フルLint対象
        int import_files = 0;         // 型抽出のみ
        int type_cache_hits = 0;      // 型キャッシュヒット
        int full_cache_hits = 0;      // フルキャッシュヒット

        // 時間統計
        duration type_extraction_time;  // 型抽出時間
        duration full_analysis_time;    // フル解析時間
        duration total_time;

        // メモリ統計
        size_t type_cache_memory = 0;   // 型キャッシュサイズ
        size_t full_cache_memory = 0;   // フルキャッシュサイズ

        // スキップした処理
        int skipped_lint_rules = 0;     // import先でスキップしたLintルール数
        duration time_saved = 0;        // 節約できた時間
    };

    void print_summary() {
        cout << "\n=== Analysis Summary ===\n";
        cout << "Target files (full lint): " << stats.target_files << "\n";
        cout << "Import dependencies (types only): " << stats.import_files << "\n";

        cout << "\nCache performance:\n";
        cout << "  Type cache hits: " << stats.type_cache_hits
             << "/" << (stats.import_files + stats.type_cache_hits)
             << " (" << (100.0 * stats.type_cache_hits /
                        (stats.import_files + stats.type_cache_hits)) << "%)\n";

        cout << "\nTime breakdown:\n";
        cout << "  Type extraction: " << stats.type_extraction_time.count() << "ms\n";
        cout << "  Full analysis: " << stats.full_analysis_time.count() << "ms\n";
        cout << "  Total: " << stats.total_time.count() << "ms\n";

        cout << "\nOptimization impact:\n";
        cout << "  Skipped lint rules in imports: " << stats.skipped_lint_rules << "\n";
        cout << "  Estimated time saved: " << stats.time_saved.count() << "ms\n";

        cout << "\nMemory usage:\n";
        cout << "  Type cache: " << (stats.type_cache_memory / 1024) << "KB\n";
        cout << "  Full cache: " << (stats.full_cache_memory / 1024 / 1024) << "MB\n";
    }
};
```

## 9. 実装優先度

### Phase 1（基本機能）- 1週間
- [ ] 基本的な再帰ファイル収集
- [ ] シンプルな依存グラフ構築
- [ ] 循環参照検出

### Phase 2（キャッシュ）- 1週間
- [ ] モジュールキャッシュ実装
- [ ] ファイル変更検出
- [ ] インクリメンタル解析

### Phase 3（最適化）- 1週間
- [ ] 並列処理対応
- [ ] LRUキャッシュ
- [ ] メモリ管理最適化

## 10. パフォーマンス改善の実例

### 従来の方法（全ファイルフルLint）
```
プロジェクト規模: 1000ファイル
ターゲット: 10ファイル (-r src/)
依存: 200ファイル

処理時間:
- 210ファイル × フル解析(100ms) = 21秒
- メモリ使用: 210 × 100KB = 21MB
```

### 最適化版（型定義のみ抽出）
```
処理時間:
- 200ファイル × 型抽出(10ms) = 2秒
- 10ファイル × フル解析(100ms) = 1秒
- 合計: 3秒（7倍高速化）

メモリ使用:
- 型キャッシュ: 200 × 1KB = 200KB
- フルキャッシュ: 10 × 100KB = 1MB
- 合計: 1.2MB（17分の1に削減）
```

## 11. 関連ドキュメント

- [083_optimized_import_resolution.md](./083_optimized_import_resolution.md) - 最適化の詳細
- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - 診断システム設計
- [080_diagnostic_catalog.md](./080_diagnostic_catalog.md) - 診断カタログ
- [081_implementation_guide.md](./081_implementation_guide.md) - 実装ガイド