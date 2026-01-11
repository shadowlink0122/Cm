# Cm言語インライン機能 包括的設計書

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 実装設計提案

## エグゼクティブサマリー

Cm言語に安全性を重視したインライン機能を実装します。明示的な制御、厳格なチェック、エラーによる失敗を基本方針とし、予期しない動作を防ぐ設計を提案します。

## 1. 設計理念

### 1.1 基本方針

1. **明示性優先**: 暗黙的な動作より明示的な指定
2. **失敗安全**: 疑わしい場合はエラーで停止
3. **予測可能性**: 動作が明確で理解しやすい
4. **診断充実**: 詳細なエラーメッセージと提案

### 1.2 他言語との差別化

| 言語 | アプローチ | Cmの選択 |
|------|-----------|----------|
| C++ | 暗黙的＋ヒント | ❌ 採用しない |
| Rust | 明示的属性 | ✅ 基本的に採用 |
| Go | コンパイラ任せ | ❌ 採用しない |
| Zig | 明示的制御 | ✅ 参考にする |

## 2. インライン化戦略の選択

### 2.1 明示的 vs 暗黙的

#### 検討結果：**明示的インライン化を採用**

```cm
// ✅ Cmの選択：明示的な属性指定
#[inline]
int add(int a, int b) {
    return a + b;
}

// ❌ 採用しない：暗黙的インライン化
int add(int a, int b) {  // コンパイラが勝手に判断
    return a + b;
}
```

#### 理由

1. **予測可能性**: ユーザーが意図を明確に表現
2. **デバッグ容易**: インライン化の有無が明確
3. **学習曲線**: 初心者にも分かりやすい
4. **最適化制御**: 細かな調整が可能

### 2.2 属性体系

```cm
// 基本属性
#[inline]           // インライン化を推奨
#[inline(always)]   // 強制インライン化（制限付き）
#[inline(never)]    // インライン化禁止
#[inline(hint)]     // ヒントのみ（コンパイラ判断）

// 拡張属性
#[inline(size = 50)]     // サイズ制限を明示
#[inline(depth = 3)]     // 再帰深度制限
#[inline(hot)]          // ホットパス最適化
#[inline(cold)]         // コールドパス（インライン化しない）
```

## 3. 安全性メカニズム

### 3.1 サイズ制限（エラーで停止）

```cm
// src/mir/passes/inline_validator.hpp
class InlineValidator {
public:
    struct Limits {
        size_t max_always_inline = 100;   // always指定でも100命令まで
        size_t max_inline = 50;           // 通常は50命令まで
        size_t max_expansion = 5;          // 展開倍率の上限
        size_t max_total_growth = 10000;  // 総コードサイズ増加の上限
    };

    ValidationResult validate(const MirFunction& func, const InlineAttribute& attr) {
        size_t size = calculate_size(func);

        if (attr.is_always && size > limits.max_always_inline) {
            return ValidationResult::error(
                "Function '{}' is too large for always_inline ({} > {} instructions)\n"
                "Consider:\n"
                "  1. Removing #[inline(always)] attribute\n"
                "  2. Splitting the function into smaller parts\n"
                "  3. Using #[inline(size = {})] to override",
                func.name, size, limits.max_always_inline, size
            );
        }

        if (!attr.is_always && size > limits.max_inline) {
            return ValidationResult::error(
                "Function '{}' exceeds inline size limit ({} > {} instructions)\n"
                "Use #[inline(never)] to prevent inlining",
                func.name, size, limits.max_inline
            );
        }

        return ValidationResult::ok();
    }
};
```

### 3.2 再帰検出（エラーで停止）

```cm
#[inline(always)]  // エラー！
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// コンパイラエラー:
// error[E0401]: Recursive function cannot be always_inline
//   --> factorial.cm:1:1
//    |
//  1 | #[inline(always)]
//    | ^^^^^^^^^^^^^^^^^ recursive functions cannot be force-inlined
//  2 | int factorial(int n) {
//    |     --------- function defined here
//  3 |     return n * factorial(n - 1);
//    |                --------- recursive call here
//    |
//    = help: use #[inline] for hint-based inlining
//    = help: or remove the inline attribute
```

### 3.3 展開爆発の防止

```cm
class ExpansionChecker {
    bool check_expansion(const Function& func, const std::vector<CallSite>& sites) {
        size_t total_expansion = 0;
        size_t func_size = func.instruction_count();

        for (const auto& site : sites) {
            total_expansion += func_size;

            if (total_expansion > MAX_EXPANSION) {
                emit_error(
                    "Inline expansion would increase code size by {}KB\n"
                    "Function '{}' ({} instructions) is called {} times\n"
                    "Maximum allowed expansion: {}KB",
                    total_expansion / 1024,
                    func.name,
                    func_size,
                    sites.size(),
                    MAX_EXPANSION / 1024
                );
                return false;  // コンパイル停止
            }
        }
        return true;
    }
};
```

## 4. 実装詳細設計

### 4.1 パーサー拡張

```cpp
// src/frontend/parser/parser_attributes.cpp
class AttributeParser {
    std::unique_ptr<InlineAttribute> parse_inline_attribute() {
        expect(TokenKind::Hash);        // #
        expect(TokenKind::LBracket);    // [

        if (!expect_keyword("inline")) {
            return nullptr;
        }

        auto attr = std::make_unique<InlineAttribute>();

        if (peek().kind == TokenKind::LParen) {
            consume();  // (

            // パラメータ解析
            while (!check(TokenKind::RParen)) {
                auto param = parse_inline_param();

                if (param.name == "always") {
                    attr->mode = InlineMode::Always;
                } else if (param.name == "never") {
                    attr->mode = InlineMode::Never;
                } else if (param.name == "hint") {
                    attr->mode = InlineMode::Hint;
                } else if (param.name == "size") {
                    attr->size_limit = parse_integer();
                } else if (param.name == "depth") {
                    attr->recursion_depth = parse_integer();
                } else if (param.name == "hot") {
                    attr->is_hot = true;
                } else if (param.name == "cold") {
                    attr->is_cold = true;
                } else {
                    error("Unknown inline parameter: '{}'", param.name);
                }

                if (!check(TokenKind::RParen)) {
                    expect(TokenKind::Comma);
                }
            }

            expect(TokenKind::RParen);  // )
        }

        expect(TokenKind::RBracket);    // ]
        return attr;
    }
};
```

### 4.2 AST表現

```cpp
// src/frontend/ast/attributes.hpp
enum class InlineMode {
    Default,    // #[inline]
    Always,     // #[inline(always)]
    Never,      // #[inline(never)]
    Hint,       // #[inline(hint)]
};

struct InlineAttribute : Attribute {
    InlineMode mode = InlineMode::Default;
    std::optional<size_t> size_limit;      // #[inline(size = N)]
    std::optional<size_t> recursion_depth; // #[inline(depth = N)]
    bool is_hot = false;                   // #[inline(hot)]
    bool is_cold = false;                  // #[inline(cold)]

    // 検証メソッド
    bool validate() const {
        if (is_hot && is_cold) {
            error("Function cannot be both hot and cold");
            return false;
        }
        if (mode == InlineMode::Never && size_limit.has_value()) {
            error("size limit is meaningless with never mode");
            return false;
        }
        return true;
    }
};
```

### 4.3 MIRレベル実装

```cpp
// src/mir/passes/inline_expansion_pass.hpp
class InlineExpansionPass : public MirPass {
private:
    struct InlineDecision {
        enum class Result {
            Inline,
            Skip,
            Error
        };

        Result result;
        std::string reason;
        DiagnosticLevel level;
    };

public:
    void run(MirModule& module) override {
        // Phase 1: 検証
        if (!validate_all_inline_attributes(module)) {
            throw CompilationError("Inline validation failed");
        }

        // Phase 2: 決定
        auto decisions = make_inline_decisions(module);

        // Phase 3: 実行
        apply_inline_decisions(module, decisions);

        // Phase 4: 検証
        verify_inline_results(module);
    }

private:
    InlineDecision decide_inline(const MirFunction& caller,
                                 const MirFunction& callee,
                                 const MirCall& call_site) {
        auto& attr = callee.inline_attr;

        // Never mode
        if (attr.mode == InlineMode::Never) {
            return {InlineDecision::Result::Skip, "never attribute", DiagnosticLevel::None};
        }

        // サイズチェック
        size_t size = calculate_inline_size(callee);

        // Always mode
        if (attr.mode == InlineMode::Always) {
            if (size > config.max_always_size) {
                return {
                    InlineDecision::Result::Error,
                    format("Function too large for always_inline: {} > {}",
                           size, config.max_always_size),
                    DiagnosticLevel::Error
                };
            }
            return {InlineDecision::Result::Inline, "always attribute", DiagnosticLevel::None};
        }

        // カスタムサイズ制限
        if (attr.size_limit.has_value()) {
            if (size > attr.size_limit.value()) {
                return {
                    InlineDecision::Result::Error,
                    format("Exceeds specified size limit: {} > {}",
                           size, attr.size_limit.value()),
                    DiagnosticLevel::Error
                };
            }
        }

        // デフォルトヒューリスティック
        if (attr.mode == InlineMode::Default || attr.mode == InlineMode::Hint) {
            int benefit = calculate_benefit(caller, callee, call_site);
            int cost = size;

            if (attr.is_hot) benefit *= 2;
            if (attr.is_cold) benefit /= 4;

            if (benefit > cost) {
                return {InlineDecision::Result::Inline, "profitable", DiagnosticLevel::None};
            } else {
                return {InlineDecision::Result::Skip, "not profitable", DiagnosticLevel::Info};
            }
        }

        return {InlineDecision::Result::Skip, "default skip", DiagnosticLevel::None};
    }
};
```

### 4.4 診断システム

```cpp
// src/diagnostics/inline_diagnostics.hpp
class InlineDiagnostics {
public:
    void emit_inline_report(const InlineDecision& decision,
                            const SourceLocation& loc) {
        switch (decision.result) {
            case InlineDecision::Result::Error:
                emit_error(loc, "Inline expansion failed", decision.reason);
                suggest_fixes(decision);
                break;

            case InlineDecision::Result::Skip:
                if (verbose_mode) {
                    emit_info(loc, "Function not inlined", decision.reason);
                }
                break;

            case InlineDecision::Result::Inline:
                if (verbose_mode) {
                    emit_success(loc, "Function inlined", decision.reason);
                }
                break;
        }
    }

private:
    void suggest_fixes(const InlineDecision& decision) {
        if (decision.reason.contains("too large")) {
            suggest("Consider splitting the function into smaller parts");
            suggest("Use #[inline(never)] to explicitly prevent inlining");
            suggest("Increase size limit with #[inline(size = N)]");
        }

        if (decision.reason.contains("recursive")) {
            suggest("Remove #[inline(always)] from recursive functions");
            suggest("Use iteration instead of recursion");
            suggest("Split into recursive and non-recursive parts");
        }
    }
};
```

## 5. エラーメッセージ設計

### 5.1 エラーメッセージ形式

```
error[E0501]: Inline expansion failed
  --> src/math.cm:15:1
   |
15 | #[inline(always)]
   | ^^^^^^^^^^^^^^^^^ cannot force inline
16 | int complex_calculation(Matrix m) {
   |     ------------------- function 'complex_calculation' has 250 instructions
   |
   = note: maximum size for always_inline is 100 instructions
   = help: remove #[inline(always)] attribute
   = help: or split the function into smaller parts
   = help: or use #[inline(size = 250)] to override (not recommended)
```

### 5.2 警告メッセージ

```
warning[W0201]: Large inline expansion detected
  --> src/utils.cm:8:5
   |
 8 |     process_data(buffer);
   |     ^^^^^^^^^^^^ function will be expanded 50 times
   |
   = note: 'process_data' is 40 instructions
   = note: total expansion: 2000 instructions (8KB)
   = help: consider using #[inline(never)] to prevent expansion
```

## 6. 設定とカスタマイズ

### 6.1 コンパイラフラグ

```bash
# インライン化の制御
cm build --inline-mode=strict      # 厳格モード（デフォルト）
cm build --inline-mode=permissive  # 寛容モード
cm build --inline-mode=off         # インライン化無効

# 制限の調整
cm build --max-inline-size=100     # 最大サイズ
cm build --max-inline-expansion=10 # 最大展開倍率

# 診断
cm build --inline-report           # 詳細レポート
cm build --inline-stats            # 統計情報
```

### 6.2 プロジェクト設定

```toml
# cm.toml
[inline]
mode = "strict"              # strict | permissive | off
max_size = 100              # 最大命令数
max_always_size = 200       # always指定時の最大
max_expansion = 5           # 展開倍率上限
error_on_failure = true     # 失敗時にエラー

[inline.diagnostics]
verbose = true              # 詳細診断
report_file = "inline.log"  # レポート出力先
show_stats = true           # 統計表示
```

## 7. 実装フェーズ

### Phase 1: 基盤（2週間）
- [ ] 属性パーサー実装
- [ ] AST拡張
- [ ] エラーメッセージ基盤

### Phase 2: 検証（2週間）
- [ ] サイズ計算
- [ ] 再帰検出
- [ ] 展開チェック

### Phase 3: MIRパス（3週間）
- [ ] InlineExpansionPass実装
- [ ] 関数複製
- [ ] 変数リネーミング

### Phase 4: 診断（1週間）
- [ ] エラーメッセージ改善
- [ ] レポート生成
- [ ] 統計情報

### Phase 5: テスト（2週間）
- [ ] ユニットテスト
- [ ] 統合テスト
- [ ] パフォーマンス測定

## 8. テスト計画

### 8.1 ポジティブテスト

```cm
// tests/inline/test_basic.cm
#[test]
void test_simple_inline() {
    #[inline]
    int add(int a, int b) { return a + b; }

    assert_eq(add(2, 3), 5);
    // アセンブリを確認してcall命令がないことを検証
}

#[test]
void test_always_inline() {
    #[inline(always)]
    int square(int x) { return x * x; }  // 小さい関数

    assert_eq(square(5), 25);
    // 必ずインライン化されることを検証
}
```

### 8.2 ネガティブテスト

```cm
// tests/inline/test_errors.cm
#[test(should_fail)]
void test_large_always_inline() {
    #[inline(always)]
    void huge_function() {
        // 200行のコード
    }
    // コンパイルエラーを期待
}

#[test(should_fail)]
void test_recursive_always() {
    #[inline(always)]
    int fib(int n) {
        return n <= 1 ? n : fib(n-1) + fib(n-2);
    }
    // エラー: 再帰関数にalwaysは使用不可
}
```

## 9. パフォーマンス目標

| メトリクス | 目標値 | 測定方法 |
|-----------|--------|----------|
| コンパイル時間増加 | <10% | 大規模プロジェクトで測定 |
| バイナリサイズ増加 | <20% | 実行ファイルサイズ比較 |
| 実行速度改善 | >15% | ベンチマークスイート |
| エラー検出率 | 100% | 不正なインライン化を全検出 |

## 10. まとめ

### 設計の特徴

1. **明示的制御**: ユーザーが意図を明確に表現
2. **厳格な検証**: サイズ、再帰、展開を厳密にチェック
3. **エラー優先**: 疑わしい場合は停止
4. **詳細な診断**: 分かりやすいエラーメッセージ

### 期待される効果

- **安全性向上**: 予期しない最適化を防止
- **予測可能性**: 動作が明確
- **学習容易**: 初心者にも分かりやすい
- **デバッグ容易**: 問題の特定が簡単

---

**作成者:** Claude Code
**ステータス:** 実装設計完了
**次のステップ:** Phase 1の実装開始