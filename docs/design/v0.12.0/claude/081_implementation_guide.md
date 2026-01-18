# Cm言語 診断システム実装ガイド

**文書番号:** 081
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 実装ガイド

## 1. 統合の核心概念

### ❌ 従来の分離型アーキテクチャ（非効率）

```
ソースコード
    ├──→ コンパイラ
    │     ├─ Lexer（重複）
    │     ├─ Parser（重複）
    │     ├─ 型チェック
    │     └─ コード生成
    │
    └──→ Linter（別プロセス）
          ├─ Lexer（重複）
          ├─ Parser（重複）
          ├─ 独自の解析
          └─ スタイルチェック
```

### ✅ 統合型アーキテクチャ（効率的）

```
ソースコード
    │
    ▼
┌──────────────────────────────────┐
│    統一フロントエンド（共有）        │
│  - Trivia保持Lexer（1回だけ）      │
│  - Trivia保持Parser（1回だけ）     │
└──────────────────────────────────┘
    │
    ▼
┌──────────────────────────────────┐
│   統一セマンティック解析（共有）      │
│  - シンボルテーブル構築（1回）       │
│  - 型推論・型チェック（1回）         │
│  - 制御フロー解析（1回）            │
│  - 使用-定義解析（1回）            │
└──────────────────────────────────┘
    │
    ├─────────────────┬─────────────┐
    ▼                 ▼             ▼
型エラー収集      Lint診断収集    最適化ヒント
（E100-E199）     （L001-L999）   （L350-L399）
```

## 2. 診断の段階的収集

### 2.1 解析パイプラインと診断タイミング

```cpp
class UnifiedDiagnosticPipeline {
public:
    DiagnosticResult process(const string& source) {
        DiagnosticResult result;

        // ========== Phase 1: 字句解析 ==========
        auto tokens = lexer.tokenize(source);
        // 検出される診断:
        // - E010: 閉じられていない文字列
        // - E011: 閉じられていないコメント
        // - E012: 無効なエスケープシーケンス

        // ========== Phase 2: 構文解析 ==========
        auto ast = parser.parse(tokens);
        // 検出される診断:
        // - E001-E009: 構文エラー
        // - E016: const/mut欠落（v0.11.0必須）
        // - L001: インデント違反（Trivia解析）
        // - L003: 末尾空白（Trivia解析）

        // ========== Phase 3: セマンティック解析 ==========
        analyzer.analyze(ast);

        // Phase 3.1: シンボル解決
        // - E101: 未定義変数
        // - E102: 未定義関数
        // - E103: 未定義型
        // - W001: 未使用変数（シンボル参照カウント）

        // Phase 3.2: 型チェック
        // - E100: 型不一致
        // - E112: ジェネリック制約違反
        // - W100: 暗黙の型変換

        // Phase 3.3: 所有権解析
        // - E200: move後の使用
        // - E201: 借用中の変更
        // - E202: 借用中の移動

        // Phase 3.4: 制御フロー解析
        // - E400: return文の欠落
        // - W200: 到達不能コード
        // - L150: 循環的複雑度（CFGから計算）

        // Phase 3.5: スタイル・最適化解析
        // - L100-L114: 命名規則（シンボルテーブル走査）
        // - L250: constにできる変数（変更追跡）
        // - L350: 不要なコピー（データフロー解析）

        return result;
    }
};
```

## 3. 型チェックとLintの統合例

### 3.1 未使用変数の検出（型チェック中に収集）

```cpp
// 従来: 別々に実装
class TypeChecker {
    void check_variable(VarDecl* var) {
        // 型チェックのみ
        if (!type_matches(var->type, var->init)) {
            report_error("E100", ...);
        }
    }
};

class Linter {
    void check_unused(VarDecl* var) {
        // 再度ASTを走査して未使用チェック
        if (!is_used(var)) {
            report_warning("W001", ...);
        }
    }
};

// 統合版: 一度の走査で両方チェック
class UnifiedAnalyzer {
    void analyze_variable(VarDecl* var) {
        // 1. 型チェック
        if (!type_matches(var->type, var->init)) {
            diag.report("E100", ...);  // 型エラー
        }

        // 2. 使用状況を記録（同じ走査で）
        usage_tracker.register_declaration(var);

        // 3. constチェック（同じ情報から）
        if (!var->is_const && !is_modified(var)) {
            diag.report("L250", ...);  // constにできる
        }

        // 4. 命名規則チェック（同じタイミングで）
        if (!is_snake_case(var->name)) {
            diag.report("L101", ...);  // 命名規則違反
        }
    }

    // 解析終了時に未使用をチェック
    void finalize() {
        for (auto& [name, info] : usage_tracker.all_symbols()) {
            if (info.use_count == 0) {
                diag.report("W001", info.location, {name});
            }
        }
    }
};
```

### 3.2 型情報を活用したLintルール

```cpp
class TypeAwareLinter {
    // L350: 不要なコピーの検出（型サイズを考慮）
    void check_unnecessary_copy(const CallExpr* call) {
        auto param_type = call->get_parameter_type();

        // 型チェッカーが計算した型サイズを使用
        if (param_type->size() > sizeof(void*) &&
            !param_type->has_side_effects()) {

            // 大きな型を値渡ししている
            diag.report("L350", call->location, {
                "Consider passing by pointer",
                to_string(param_type->size())
            });
        }
    }

    // L100: 関数名の規則（オーバーロードを考慮）
    void check_function_naming(const FuncDecl* func) {
        // 型チェッカーのオーバーロード解決情報を使用
        auto overloads = type_checker->get_overloads(func->name);

        if (overloads.size() > 1) {
            // オーバーロードがある場合は一貫性チェック
            for (auto& overload : overloads) {
                if (!has_consistent_naming(overload)) {
                    diag.report("L100", ...);
                }
            }
        }
    }
};
```

## 4. 共有データ構造

### 4.1 統一シンボルテーブル

```cpp
// コンパイラとLinterで共有
struct SymbolInfo {
    string name;
    Type* type;              // 型チェッカーが設定
    SourceLocation location;

    // 使用状況（Linter用）
    int use_count = 0;
    int modification_count = 0;
    vector<SourceLocation> use_sites;

    // 所有権情報（型チェック＋Lint）
    bool is_moved = false;
    bool is_borrowed = false;

    // スタイル情報（Linter用）
    NamingStyle detected_style;
    bool follows_convention = true;
};

class UnifiedSymbolTable {
    map<string, SymbolInfo> symbols;

public:
    // 型チェッカーとLinterが同じテーブルを使用
    void register_symbol(const string& name, Type* type, Location loc) {
        symbols[name] = {name, type, loc};
    }

    void mark_used(const string& name, Location use_loc) {
        symbols[name].use_count++;
        symbols[name].use_sites.push_back(use_loc);
    }

    void mark_modified(const string& name) {
        symbols[name].modification_count++;
    }
};
```

### 4.2 統一制御フローグラフ（CFG）

```cpp
class UnifiedCFG {
    // 基本ブロックと制御フロー
    vector<BasicBlock> blocks;

public:
    // 型チェッカー用
    bool has_return_on_all_paths() { ... }

    // Linter用（同じCFGから計算）
    int calculate_cyclomatic_complexity() {
        // McCabeの循環的複雑度
        return edges.size() - nodes.size() + 2;
    }

    // 最適化ヒント用
    vector<Location> find_unreachable_code() { ... }
};
```

## 5. 実行モード

### 5.1 モード別の診断レベル

```cpp
enum class ExecutionMode {
    Compile,      // コンパイル: E/Wのみ
    Check,        // チェック: E/W/L全て
    LintOnly,     // Lint専用: Lのみ
    QuickCheck    // 高速チェック: 軽量ルールのみ
};

class DiagnosticController {
    void run(ExecutionMode mode) {
        switch (mode) {
        case Compile:
            // 型エラーと警告のみ
            enable_categories({"E", "W"});
            disable_categories({"L"});
            break;

        case Check:
            // すべて有効
            enable_categories({"E", "W", "L"});
            break;

        case LintOnly:
            // Lintのみ（型エラーは無視できない）
            enable_categories({"E100-E199", "L"});
            disable_categories({"E001-E099", "W"});
            break;

        case QuickCheck:
            // 高速チェック（重い解析をスキップ）
            enable_categories({"E", "W001-W009", "L001-L099"});
            disable_expensive_checks();
            break;
        }
    }
};
```

## 6. パフォーマンス最適化

### 6.1 解析の共有による高速化

```
従来（分離型）:
- Lexer: 10ms × 2回 = 20ms
- Parser: 20ms × 2回 = 40ms
- 型チェック: 30ms
- Lint独自解析: 40ms
合計: 130ms

統合型:
- Lexer: 10ms × 1回 = 10ms
- Parser: 20ms × 1回 = 20ms
- 統合解析: 40ms（型チェック＋Lint）
合計: 70ms

→ 約46%の高速化
```

### 6.2 インクリメンタル解析

```cpp
class IncrementalAnalyzer {
    // 変更された部分のみ再解析
    void analyze_changes(const FileChanges& changes) {
        // 影響範囲を計算
        auto affected = calculate_affected_scope(changes);

        // 型情報は再利用
        if (!affected.has_type_changes) {
            reuse_type_info();
        }

        // スタイルチェックは局所的
        for (auto& line : changes.modified_lines) {
            check_local_style(line);  // L001-L099
        }
    }
};
```

## 7. 実装例：統合された診断フロー

```cpp
// main.cpp
int main() {
    UnifiedDiagnosticSystem diag;

    // 1つのパイプラインで全診断
    auto result = diag.process_file("main.cm");

    // カテゴリ別に出力
    for (auto& d : result.diagnostics) {
        if (d.is_error()) {
            cerr << format_error(d) << endl;
        } else if (d.is_warning()) {
            cout << format_warning(d) << endl;
        } else if (d.is_lint()) {
            if (config.show_hints) {
                cout << format_hint(d) << endl;
            }
        }
    }

    // コンパイル継続判定
    if (result.has_errors()) {
        return 1;  // エラーがあればコンパイル中止
    }

    // Lintのみのエラーなら継続可能
    if (mode == ExecutionMode::Compile) {
        generate_code(result.ast);
    }

    return 0;
}
```

## 8. まとめ

### 統合のメリット

1. **効率性**: 解析を1回だけ実行
2. **一貫性**: 同じ型情報・シンボル情報を使用
3. **正確性**: 型情報を活用した高度なLint
4. **保守性**: コード重複の排除
5. **拡張性**: 新ルールが既存情報を再利用可能

### 重要なポイント

- **型チェックとLintは同じ解析パイプライン上で実行**
- **シンボルテーブル、型情報、CFGを共有**
- **診断は段階的に収集（Lexer→Parser→Semantic）**
- **モードによって有効な診断を切り替え**

## 9. 関連ドキュメント

- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - メイン設計
- [080_diagnostic_catalog.md](./080_diagnostic_catalog.md) - 診断カタログ
- [078_linter_formatter_design.md](./078_linter_formatter_design.md) - 初期設計