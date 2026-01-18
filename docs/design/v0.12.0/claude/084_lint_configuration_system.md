# Cm言語 Lint設定システム設計

**文書番号:** 084
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 設計中

## 1. 概要

プロジェクトレベル（`.cmconfig.yml`）とコードレベル（インラインコメント）でLintルールを柔軟に制御できるシステムの設計。

### 設計原則
1. **明示性**: ルール無効化には理由の記述を推奨
2. **段階性**: プロジェクトの成熟度に応じた段階的な適用
3. **局所性**: 影響範囲を最小限に制限
4. **追跡可能性**: 無効化したルールの監視

## 2. 設定の階層と優先順位

```
優先度（高→低）:
1. インラインコメント（最も局所的）
2. ファイル先頭のコメント
3. ディレクトリ別設定（.cmconfig.yml）
4. プロジェクトルート設定（.cmconfig.yml）
5. ユーザーグローバル設定（~/.cmconfig.yml）
6. デフォルト設定
```

## 3. プロジェクト設定ファイル（.cmconfig.yml）

### 3.1 完全な設定例

```yaml
# .cmconfig.yml - Cmプロジェクト設定ファイル
version: 1.0  # 設定ファイルのバージョン

# プロジェクト情報
project:
  name: my-project
  version: 0.1.0
  language_version: 0.12.0  # Cm言語バージョン

# グローバル診断設定
diagnostics:
  # デフォルトレベル
  default_level: warning

  # エラーレポート設定
  reporting:
    format: detailed  # detailed | brief | json
    show_suggestions: true
    max_errors: 50
    max_warnings: 100
    color_output: auto  # auto | always | never

# Lintルール設定
lint:
  # 全体設定
  enabled: true
  auto_fix: false
  parallel: true

  # ルールセット選択
  preset: recommended  # minimal | recommended | strict | custom

  # 個別ルール設定
  rules:
    # ========== エラー（E）==========
    E016:  # const/mut必須
      level: error  # 変更不可（言語仕様）

    E200-E209:  # 所有権エラー
      level: error

    # ========== 警告（W）==========
    W001:  # 未使用変数
      level: warning
      options:
        ignore_pattern: "^_"  # _で始まる変数は無視

    W100:  # 暗黙の型変換
      level: warning
      options:
        allow_numeric_widening: false

    # ========== Lintルール（L）==========
    # スタイル
    L001:  # インデント
      level: error
      options:
        style: spaces  # spaces | tabs
        size: 4

    L002:  # 最大行長
      level: warning
      options:
        max_length: 100
        ignore_comments: false
        ignore_strings: true

    L003:  # 末尾空白
      level: error
      auto_fix: true

    # 命名規則
    L100:  # 関数名
      level: warning
      options:
        pattern: snake_case
        # 例外を許可
        exceptions:
          - "XMLHttpRequest"  # 標準API互換
          - "JSONParse"

    L102:  # 定数名
      level: warning
      options:
        pattern: UPPER_SNAKE_CASE
        # 数学定数は例外
        exceptions:
          - "Pi"
          - "E"
          - "Phi"

    # 複雑度
    L150:  # 循環的複雑度
      level: warning
      options:
        max_complexity: 10
        # 特定の関数は除外
        exclude_functions:
          - "parse*"  # パーサー関数
          - "handle*" # イベントハンドラ

    L153:  # 関数長
      level: hint
      options:
        max_lines: 50
        count_blank_lines: false
        count_comments: false

# パス別の設定（オーバーライド）
paths:
  # テストコードは緩い設定
  "tests/**/*.cm":
    rules:
      L150:
        options:
          max_complexity: 20  # テストは複雑でもOK
      L153:
        level: disabled  # 行数制限なし
      W001:
        level: disabled  # 未使用変数OK

  # 生成コードは除外
  "**/*.generated.cm":
    lint:
      enabled: false  # 完全に無効化

  # クリティカルなコード
  "src/core/**/*.cm":
    rules:
      # より厳格に
      W001:
        level: error
      L250:  # prefer-const
        level: error

  # ベンダーコード
  "vendor/**":
    lint:
      enabled: false

# インポート先の扱い
imports:
  # デフォルトはtypes_onlyモード（高速）
  analysis_mode: types_only  # types_only | full | skip

  # 特定のインポートはフルチェック
  full_analysis:
    - "src/critical/**/*.cm"
    - "src/security/**/*.cm"

  # キャッシュ設定
  cache:
    enabled: true
    directory: .cm-cache
    ttl: 3600  # 秒

# 段階的な厳格化
migration:
  # 新規ファイルにはより厳格なルールを適用
  new_files:
    # 作成日が指定日以降のファイル
    since: "2024-01-01"
    rules:
      L001:
        level: error
      L100:
        level: error

  # 既存ファイルは段階的に
  existing_files:
    rules:
      L001:
        level: warning
      L100:
        level: hint

# カスタムルール
custom_rules:
  # プロジェクト固有のルール
  - id: "PROJ001"
    name: "no-console-log"
    message: "console.log is not allowed in production code"
    pattern: 'console\.log'
    level: error
    paths:
      include: ["src/**/*.cm"]
      exclude: ["src/debug/**/*.cm"]

# チーム設定
team:
  # コードレビュー時のみ有効
  review_mode:
    rules:
      L001-L099:  # スタイルは全てエラー
        level: error

  # CI/CD設定
  ci:
    fail_on_warnings: false
    fail_on_hints: false

# レポート設定
reports:
  # サマリー出力
  summary:
    show_statistics: true
    show_top_violations: 10

  # 無視されたルールの追跡
  suppression_tracking:
    enabled: true
    require_reason: true  # 理由必須
    report_file: .cm-suppressions.log
```

### 3.2 プリセット定義

```yaml
# minimal - 最小限
presets:
  minimal:
    rules:
      E001-E999: error
      W001: warning
      L001: warning

# recommended - 推奨（デフォルト）
  recommended:
    rules:
      E001-E999: error
      W001-W099: warning
      W100-W199: warning
      L001: error
      L100-L114: warning
      L150-L159: warning

# strict - 厳格
  strict:
    rules:
      E001-E999: error
      W001-W999: error
      L001-L099: error
      L100-L149: error
      L150-L199: warning
      L250-L349: warning
```

## 4. インラインコメントによる制御

### 4.1 基本構文

```cm
// ========== 次の行のみ無効化 ==========

// @cm-disable-next-line L100
int CalculateSum() { }  // この行のL100は無視される

// 理由付き（推奨）
// @cm-disable-next-line L100 -- 外部APIとの互換性のため
func XMLHttpRequest() { }

// 複数ルール
// @cm-disable-next-line L100, L150, W001
func complex_LEGACY_function() { }

// ========== ブロック無効化 ==========

// @cm-disable L350, L351
void performance_critical() {
    // このブロック内はL350, L351が無効
    auto copy1 = expensive_object;
    auto copy2 = another_object;
}
// @cm-enable L350, L351

// ========== ファイル全体 ==========

// @cm-disable-file L153  -- レガシーコードのため一時的に無効化

// ========== 特定範囲 ==========

// @cm-disable-region ComplexLogic
void parse_expression() {
    // 複雑なパース処理
    // L150（複雑度）は無視
}
// @cm-enable-region ComplexLogic
```

### 4.2 コメント構文仕様

```cpp
// パーサー実装
class SuppressionCommentParser {
    enum Directive {
        DisableNextLine,    // 次の行のみ
        DisableLine,        // 現在行
        DisableBlock,       // ブロック開始
        EnableBlock,        // ブロック終了
        DisableFile,        // ファイル全体
        DisableRegion,      // 名前付き領域
        EnableRegion        // 名前付き領域終了
    };

    struct Suppression {
        Directive type;
        vector<string> rules;     // 対象ルールID
        string reason;            // 理由（オプション）
        SourceLocation location;
        string region_name;       // リージョン名（該当時）
    };

    // コメントパース正規表現
    regex patterns[7] = {
        // @cm-disable-next-line RULES [-- REASON]
        regex(R"(@cm-disable-next-line\s+([\w,\s-]+)(?:\s+--\s+(.+))?)"),

        // @cm-disable-line RULES [-- REASON]
        regex(R"(@cm-disable-line\s+([\w,\s-]+)(?:\s+--\s+(.+))?)"),

        // @cm-disable RULES [-- REASON]
        regex(R"(@cm-disable\s+([\w,\s-]+)(?:\s+--\s+(.+))?)"),

        // @cm-enable RULES
        regex(R"(@cm-enable\s+([\w,\s-]+))"),

        // @cm-disable-file RULES [-- REASON]
        regex(R"(@cm-disable-file\s+([\w,\s-]+)(?:\s+--\s+(.+))?)"),

        // @cm-disable-region NAME
        regex(R"(@cm-disable-region\s+(\w+))"),

        // @cm-enable-region NAME
        regex(R"(@cm-enable-region\s+(\w+))")
    };
};
```

### 4.3 エイリアスとショートカット

```cm
// 一般的なエイリアス
// @noqa  = @cm-disable-line  （Python風）
// @nolint = @cm-disable-line  （C++風）
// @ts-ignore = @cm-disable-next-line  （TypeScript風）

// 使用例
int x = 10;  // @noqa W001

// カテゴリ指定
// @cm-disable-next-line all      -- 全ルール
// @cm-disable-next-line style    -- L001-L099
// @cm-disable-next-line naming   -- L100-L149
// @cm-disable-next-line perf     -- L350-L399
```

## 5. 設定の適用と解決

### 5.1 設定解決アルゴリズム

```cpp
class ConfigResolver {
    struct ResolvedConfig {
        DiagnosticLevel level;
        bool enabled;
        bool auto_fix;
        map<string, any> options;
        string source;  // 設定の出所（デバッグ用）
    };

    ResolvedConfig resolve_rule(const string& rule_id,
                                const string& filepath,
                                int line_number) {
        ResolvedConfig config;

        // 1. デフォルト設定
        config = get_default_config(rule_id);
        config.source = "default";

        // 2. グローバル設定（~/.cmconfig.yml）
        if (auto global = load_global_config()) {
            merge_config(config, global.get_rule(rule_id));
            config.source = "global";
        }

        // 3. プロジェクト設定（.cmconfig.yml）
        if (auto project = load_project_config()) {
            merge_config(config, project.get_rule(rule_id));
            config.source = "project";
        }

        // 4. パス別設定
        if (auto path_config = match_path_config(filepath)) {
            merge_config(config, path_config.get_rule(rule_id));
            config.source = "path: " + path_config.pattern;
        }

        // 5. ファイルレベルの抑制
        if (is_suppressed_in_file(rule_id, filepath)) {
            config.enabled = false;
            config.source = "file suppression";
        }

        // 6. インライン抑制
        if (is_suppressed_at_line(rule_id, filepath, line_number)) {
            config.enabled = false;
            config.source = "inline suppression";
        }

        return config;
    }
};
```

### 5.2 設定の検証

```cpp
class ConfigValidator {
    vector<ConfigError> validate(const Config& config) {
        vector<ConfigError> errors;

        // バージョン互換性チェック
        if (config.language_version > CURRENT_VERSION) {
            errors.push_back({
                "Configuration requires Cm " + config.language_version
            });
        }

        // ルール設定の妥当性
        for (const auto& [rule_id, rule_config] : config.rules) {
            // 存在しないルール
            if (!is_valid_rule(rule_id)) {
                errors.push_back({
                    "Unknown rule: " + rule_id
                });
            }

            // 矛盾する設定
            if (rule_config.level == "error" && rule_config.auto_fix) {
                errors.push_back({
                    "Cannot auto-fix errors: " + rule_id
                });
            }

            // 必須ルールの無効化
            if (is_mandatory_rule(rule_id) && !rule_config.enabled) {
                errors.push_back({
                    "Cannot disable mandatory rule: " + rule_id
                });
            }
        }

        return errors;
    }
};
```

## 6. 抑制の追跡とレポート

### 6.1 抑制統計の収集

```cpp
class SuppressionTracker {
    struct SuppressionInfo {
        string rule_id;
        string filepath;
        int line;
        string reason;
        string author;  // gitから取得
        time_t timestamp;
        string commit_hash;
    };

    void track_suppression(const Suppression& supp) {
        SuppressionInfo info;
        info.rule_id = supp.rule_id;
        info.filepath = supp.filepath;
        info.line = supp.line;
        info.reason = supp.reason;

        // git blameから情報取得
        auto git_info = get_git_blame(supp.filepath, supp.line);
        info.author = git_info.author;
        info.timestamp = git_info.timestamp;
        info.commit_hash = git_info.commit;

        suppressions.push_back(info);
    }

    void generate_report() {
        cout << "=== Suppression Report ===\n\n";

        // ルール別集計
        map<string, int> by_rule;
        for (const auto& supp : suppressions) {
            by_rule[supp.rule_id]++;
        }

        cout << "Most suppressed rules:\n";
        for (const auto& [rule, count] : by_rule) {
            cout << "  " << rule << ": " << count << " times\n";
        }

        // 理由なし抑制
        int no_reason = count_if(suppressions.begin(), suppressions.end(),
            [](const auto& s) { return s.reason.empty(); });

        if (no_reason > 0) {
            cout << "\nWarning: " << no_reason
                 << " suppressions without reason\n";
        }

        // 古い抑制（6ヶ月以上）
        auto old_suppressions = filter_old_suppressions(6_months);
        if (!old_suppressions.empty()) {
            cout << "\nConsider reviewing " << old_suppressions.size()
                 << " suppressions older than 6 months\n";
        }
    }
};
```

### 6.2 抑制ログファイル

```json
// .cm-suppressions.json
{
  "version": "1.0",
  "generated": "2024-01-13T10:00:00Z",
  "suppressions": [
    {
      "rule": "L100",
      "file": "src/api/client.cm",
      "line": 45,
      "reason": "External API compatibility",
      "author": "developer@example.com",
      "date": "2024-01-10",
      "commit": "abc123",
      "type": "inline"
    },
    {
      "rule": "L153",
      "file": "src/legacy/parser.cm",
      "reason": "Legacy code - will refactor in v2.0",
      "author": "team-lead@example.com",
      "date": "2023-12-01",
      "type": "file",
      "review_date": "2024-06-01"
    }
  ],
  "statistics": {
    "total": 42,
    "with_reason": 38,
    "without_reason": 4,
    "by_rule": {
      "L100": 15,
      "L153": 10,
      "W001": 8,
      "L350": 9
    },
    "by_type": {
      "inline": 35,
      "file": 5,
      "config": 2
    }
  }
}
```

## 7. CLI統合

### 7.1 コマンドラインオプション

```bash
# 設定確認
cm lint --show-config                # 現在の設定を表示
cm lint --show-config-for file.cm    # 特定ファイルの設定
cm lint --explain L100               # ルールの説明

# 設定検証
cm lint --validate-config            # 設定ファイルの検証

# 抑制レポート
cm lint --suppression-report         # 抑制統計を表示
cm lint --list-suppressions          # 全抑制をリスト
cm lint --clean-suppressions         # 古い抑制を削除提案

# 設定生成
cm lint --init                       # .cmconfig.yml生成
cm lint --init --preset=strict       # strictプリセットで生成

# オーバーライド
cm lint --rules L001=error,L100=warn # 一時的なルール変更
cm lint --preset strict              # プリセット一時適用
cm lint --config other.yml           # 別の設定ファイル使用
```

## 8. IDE/エディタ統合

### 8.1 VSCode設定

```json
// .vscode/settings.json
{
  "cm.lint.configFile": ".cmconfig.yml",
  "cm.lint.showSuppressionHints": true,
  "cm.lint.suppressionRequireReason": true,
  "cm.lint.quickFix.preferInline": false,  // 設定ファイル優先

  // クイックアクション
  "cm.lint.codeActions": {
    "suppressLine": true,
    "suppressNextLine": true,
    "suppressFile": false,  // 危険なので無効
    "addToConfig": true
  }
}
```

### 8.2 クイックフィックス実装

```typescript
// VSCode拡張機能
class CmLintCodeActionProvider {
    provideCodeActions(diagnostic: Diagnostic): CodeAction[] {
        const actions = [];

        // 1. 次の行を抑制
        actions.push({
            title: `Suppress ${diagnostic.code} on next line`,
            edit: {
                insert: {
                    position: line_start(diagnostic.line - 1),
                    text: `// @cm-disable-next-line ${diagnostic.code}\n`
                }
            }
        });

        // 2. 理由付き抑制（推奨）
        actions.push({
            title: `Suppress ${diagnostic.code} with reason...`,
            command: {
                command: 'cm.suppressWithReason',
                arguments: [diagnostic]
            }
        });

        // 3. 設定ファイルに追加
        if (diagnostic.severity === 'hint') {
            actions.push({
                title: `Disable ${diagnostic.code} in config`,
                command: {
                    command: 'cm.addToConfig',
                    arguments: [{
                        rule: diagnostic.code,
                        level: 'disabled'
                    }]
                }
            });
        }

        return actions;
    }
}
```

## 9. ベストプラクティス

### 9.1 推奨される使い方

```yaml
# プロジェクト開始時
lint:
  preset: recommended  # 標準から開始

migration:
  new_files:
    rules:
      all: error  # 新規は厳格に

# 成熟したプロジェクト
lint:
  preset: strict

paths:
  # レガシーコードは段階的に
  "src/legacy/**":
    rules:
      L001-L099: warning
```

### 9.2 アンチパターン

```cm
// ❌ 悪い例：理由なし大量抑制
// @cm-disable-file all

// ❌ 悪い例：広範囲な抑制
// @cm-disable L001-L999

// ✅ 良い例：限定的で理由付き
// @cm-disable-next-line L100 -- External API naming convention
func XMLHttpRequest() { }
```

## 10. 関連ドキュメント

- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - 診断システム
- [080_diagnostic_catalog.md](./080_diagnostic_catalog.md) - ルール一覧
- [082_recursive_module_analysis.md](./082_recursive_module_analysis.md) - モジュール解析