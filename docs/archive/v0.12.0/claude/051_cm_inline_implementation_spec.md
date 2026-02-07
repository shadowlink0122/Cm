# Cm言語インライン機能 実装仕様書

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 実装仕様
関連文書: 050_cm_inline_comprehensive_design.md

## 1. 言語仕様

### 1.1 構文定義（EBNF）

```ebnf
inline_attribute ::= '#' '[' 'inline' inline_params? ']'

inline_params ::= '(' param_list ')'

param_list ::= param (',' param)*

param ::= mode_param
        | 'size' '=' integer
        | 'depth' '=' integer
        | 'hot'
        | 'cold'

mode_param ::= 'always' | 'never' | 'hint'
```

### 1.2 有効な組み合わせ

| 属性 | 説明 | 制約 |
|------|------|------|
| `#[inline]` | 基本的なインライン化推奨 | サイズ制限50命令 |
| `#[inline(always)]` | 強制インライン化 | サイズ制限100命令、再帰不可 |
| `#[inline(never)]` | インライン化禁止 | 他のパラメータと併用不可 |
| `#[inline(hint)]` | ヒントのみ | コンパイラ判断 |
| `#[inline(size = N)]` | カスタムサイズ制限 | N ≤ 500 |
| `#[inline(depth = N)]` | 再帰深度制限 | N ≤ 10 |
| `#[inline(hot)]` | ホットパス最適化 | cold と排他 |
| `#[inline(cold)]` | コールドパス | hot と排他 |

### 1.3 無効な組み合わせ（コンパイルエラー）

```cm
// ❌ エラー: hot と cold は排他
#[inline(hot, cold)]
int func() { }

// ❌ エラー: never と size は矛盾
#[inline(never, size = 100)]
int func() { }

// ❌ エラー: 再帰関数に always は使用不可
#[inline(always)]
int recursive(int n) {
    return recursive(n - 1);
}
```

## 2. コンパイラ内部実装

### 2.1 データフロー

```
Source → Lexer → Parser → AST → HIR → MIR → [Inline Pass] → Optimized MIR → LLVM
                            ↑                        ↑
                    属性アタッチ              インライン展開
```

### 2.2 内部データ構造

```cpp
// src/mir/inline/inline_context.hpp
namespace cm::mir::inline_pass {

struct InlineContext {
    // グローバル状態
    size_t total_code_size = 0;
    size_t original_size = 0;
    size_t expansion_count = 0;

    // 関数ごとの情報
    struct FunctionInfo {
        std::string name;
        size_t size;                    // 命令数
        size_t call_count;              // 呼び出し回数
        InlineAttribute attribute;      // インライン属性
        bool is_recursive;              // 再帰判定結果
        bool is_inlined;               // インライン化済み
        std::vector<CallSite> call_sites;
    };

    std::map<std::string, FunctionInfo> functions;

    // 呼び出しサイト情報
    struct CallSite {
        std::string caller;
        std::string callee;
        size_t bb_id;           // 基本ブロックID
        size_t instr_id;        // 命令ID
        bool in_loop;           // ループ内か
        size_t loop_depth;      // ループネスト深度
    };

    // 統計情報
    struct Statistics {
        size_t functions_inlined = 0;
        size_t functions_rejected = 0;
        size_t total_expansions = 0;
        size_t code_size_increase = 0;
        std::vector<std::string> error_messages;
    } stats;
};

}  // namespace cm::mir::inline_pass
```

### 2.3 サイズ計算アルゴリズム

```cpp
// src/mir/inline/size_calculator.cpp
class InlineSizeCalculator {
    // 命令ごとのコスト定義
    static constexpr size_t COST_TABLE[] = {
        1,  // Move
        1,  // Add, Sub, Mul
        3,  // Div, Mod
        2,  // Call
        1,  // Branch
        2,  // Switch
        5,  // Alloca
        3,  // Load
        3,  // Store
        10, // Intrinsic
    };

public:
    size_t calculate(const MirFunction& func) {
        size_t total = 0;

        // 基本ブロックごとに計算
        for (const auto& bb : func.basic_blocks) {
            // 通常命令
            for (const auto& inst : bb.instructions) {
                total += get_instruction_cost(inst);
            }

            // ターミネータ
            total += get_terminator_cost(bb.terminator);
        }

        // ローカル変数のコスト
        total += func.locals.size() * 2;

        // 引数のコスト
        total += func.parameters.size();

        return total;
    }

private:
    size_t get_instruction_cost(const MirInstruction& inst) {
        return COST_TABLE[static_cast<size_t>(inst.opcode)];
    }

    size_t get_terminator_cost(const MirTerminator& term) {
        switch (term.kind) {
            case TerminatorKind::Return: return 1;
            case TerminatorKind::Branch: return 2;
            case TerminatorKind::Switch: return 3 + term.cases.size();
            default: return 1;
        }
    }
};
```

### 2.4 再帰検出アルゴリズム

```cpp
// src/mir/inline/recursion_detector.cpp
class RecursionDetector {
    using CallGraph = std::map<std::string, std::set<std::string>>;

public:
    bool is_recursive(const std::string& func_name, const CallGraph& graph) {
        std::set<std::string> visited;
        std::set<std::string> recursion_stack;

        return dfs_detect_cycle(func_name, graph, visited, recursion_stack);
    }

private:
    bool dfs_detect_cycle(const std::string& current,
                         const CallGraph& graph,
                         std::set<std::string>& visited,
                         std::set<std::string>& rec_stack) {
        visited.insert(current);
        rec_stack.insert(current);

        // 呼び出し先を探索
        auto it = graph.find(current);
        if (it != graph.end()) {
            for (const auto& callee : it->second) {
                if (rec_stack.count(callee)) {
                    // サイクル検出（再帰）
                    return true;
                }

                if (!visited.count(callee)) {
                    if (dfs_detect_cycle(callee, graph, visited, rec_stack)) {
                        return true;
                    }
                }
            }
        }

        rec_stack.erase(current);
        return false;
    }
};
```

## 3. インライン展開アルゴリズム

### 3.1 関数複製と変数リネーミング

```cpp
// src/mir/inline/function_cloner.cpp
class FunctionCloner {
public:
    MirBasicBlock clone_and_inline(const MirFunction& callee,
                                   const MirCall& call_site,
                                   MirFunction& caller) {
        // 1. 新しい変数IDマッピングを作成
        std::map<VarId, VarId> var_mapping;

        // パラメータを引数にマップ
        for (size_t i = 0; i < callee.parameters.size(); ++i) {
            var_mapping[callee.parameters[i]] = call_site.arguments[i];
        }

        // ローカル変数を新規作成
        for (const auto& local : callee.locals) {
            VarId new_var = caller.create_temp_var(local.type);
            var_mapping[local.id] = new_var;
        }

        // 2. 基本ブロックを複製
        std::vector<MirBasicBlock> cloned_blocks;
        std::map<BlockId, BlockId> block_mapping;

        for (const auto& bb : callee.basic_blocks) {
            BlockId new_id = caller.create_block();
            block_mapping[bb.id] = new_id;

            MirBasicBlock new_bb;
            new_bb.id = new_id;

            // 命令を複製してリネーム
            for (const auto& inst : bb.instructions) {
                new_bb.instructions.push_back(
                    rename_instruction(inst, var_mapping)
                );
            }

            // ターミネータを複製
            new_bb.terminator = rename_terminator(
                bb.terminator, var_mapping, block_mapping
            );

            cloned_blocks.push_back(new_bb);
        }

        // 3. Return文を結果代入に変換
        transform_returns(cloned_blocks, call_site.result, block_mapping);

        return merge_blocks(cloned_blocks);
    }

private:
    MirInstruction rename_instruction(const MirInstruction& inst,
                                      const std::map<VarId, VarId>& mapping) {
        MirInstruction new_inst = inst;

        // オペランドのリネーミング
        for (auto& operand : new_inst.operands) {
            if (operand.is_variable()) {
                auto it = mapping.find(operand.var_id);
                if (it != mapping.end()) {
                    operand.var_id = it->second;
                }
            }
        }

        // 結果のリネーミング
        if (new_inst.result.has_value()) {
            auto it = mapping.find(new_inst.result.value());
            if (it != mapping.end()) {
                new_inst.result = it->second;
            }
        }

        return new_inst;
    }
};
```

## 4. エラー処理仕様

### 4.1 エラーコード体系

```cpp
enum class InlineError : uint32_t {
    // サイズ関連
    E0501_TOO_LARGE_FOR_ALWAYS = 0x0501,
    E0502_EXCEEDS_SIZE_LIMIT   = 0x0502,
    E0503_EXPANSION_TOO_LARGE  = 0x0503,

    // 再帰関連
    E0511_RECURSIVE_ALWAYS     = 0x0511,
    E0512_MUTUAL_RECURSION     = 0x0512,
    E0513_DEPTH_EXCEEDED       = 0x0513,

    // 属性関連
    E0521_CONFLICTING_ATTRS    = 0x0521,
    E0522_INVALID_COMBINATION  = 0x0522,

    // リソース関連
    E0531_REGISTER_PRESSURE    = 0x0531,
    E0532_STACK_SIZE_EXCEEDED  = 0x0532,
};
```

### 4.2 エラーメッセージテンプレート

```cpp
class InlineErrorFormatter {
    static const std::map<InlineError, std::string> ERROR_TEMPLATES = {
        {E0501_TOO_LARGE_FOR_ALWAYS,
         "Function '{func}' is too large for always_inline\n"
         "  Size: {size} instructions (limit: {limit})\n"
         "  Location: {file}:{line}:{col}"},

        {E0511_RECURSIVE_ALWAYS,
         "Recursive function '{func}' cannot use always_inline\n"
         "  Recursive call at: {call_location}\n"
         "  Help: Use #[inline] or #[inline(hint)] instead"},

        {E0521_CONFLICTING_ATTRS,
         "Conflicting inline attributes\n"
         "  Attribute 1: {attr1} at {loc1}\n"
         "  Attribute 2: {attr2} at {loc2}\n"
         "  Help: Remove one of the attributes"},
    };

    std::string format(InlineError code, const ErrorContext& ctx) {
        auto tmpl = ERROR_TEMPLATES.at(code);
        return substitute_placeholders(tmpl, ctx);
    }
};
```

## 5. 最適化レベルとの連携

### 5.1 最適化レベル別動作

| レベル | -O0 | -O1 | -O2 | -O3 | -Os |
|--------|-----|-----|-----|-----|-----|
| インライン化 | 無効 | hint のみ | 有効 | 積極的 | サイズ優先 |
| サイズ制限 | - | 30 | 50 | 100 | 20 |
| always 制限 | - | 50 | 100 | 200 | 30 |
| 展開倍率 | - | 2x | 5x | 10x | 1.5x |

### 5.2 実装

```cpp
struct InlineConfig {
    static InlineConfig from_opt_level(int level, bool size_opt) {
        InlineConfig config;

        if (level == 0) {
            config.enabled = false;
            return config;
        }

        config.enabled = true;

        if (size_opt) {  // -Os
            config.max_size = 20;
            config.max_always_size = 30;
            config.max_expansion = 1.5;
            config.prefer_size = true;
        } else {
            switch (level) {
                case 1:
                    config.max_size = 30;
                    config.max_always_size = 50;
                    config.max_expansion = 2.0;
                    config.honor_hints_only = true;
                    break;
                case 2:
                    config.max_size = 50;
                    config.max_always_size = 100;
                    config.max_expansion = 5.0;
                    break;
                case 3:
                    config.max_size = 100;
                    config.max_always_size = 200;
                    config.max_expansion = 10.0;
                    config.aggressive = true;
                    break;
            }
        }

        return config;
    }

    bool enabled = true;
    size_t max_size = 50;
    size_t max_always_size = 100;
    double max_expansion = 5.0;
    bool honor_hints_only = false;
    bool aggressive = false;
    bool prefer_size = false;
};
```

## 6. デバッグサポート

### 6.1 インラインマップ生成

```cpp
// デバッグ情報にインライン化情報を埋め込む
struct InlineDebugInfo {
    struct InlineRecord {
        std::string original_func;
        std::string inlined_into;
        SourceLocation call_site;
        SourceLocation original_location;
        size_t depth;  // インライン深度
    };

    std::vector<InlineRecord> records;

    void emit_dwarf_info(llvm::DIBuilder& builder) {
        for (const auto& record : records) {
            auto scope = builder.createInlinedVariable(
                record.original_func,
                record.inlined_into,
                record.call_site.line,
                record.call_site.column
            );
            // DWARF情報を生成
        }
    }
};
```

### 6.2 インラインレポート

```
=== Inline Expansion Report ===
Date: 2026-01-11 14:30:00
Source: main.cm
Optimization Level: -O2

Functions Analyzed: 42
Functions Inlined: 18 (42.8%)
Functions Rejected: 24 (57.2%)

Code Size:
  Original: 8,432 bytes
  After Inline: 10,248 bytes
  Increase: 1,816 bytes (21.5%)

Top Inlined Functions:
  1. add() - inlined 127 times (saved ~1905 cycles)
  2. min() - inlined 89 times (saved ~1335 cycles)
  3. abs() - inlined 76 times (saved ~1140 cycles)

Rejected Functions:
  1. process_data() - too large (156 > 50 instructions)
  2. recursive_calc() - recursive function
  3. error_handler() - marked as #[inline(never)]

Performance Impact (estimated):
  Call Overhead Removed: ~15,420 cycles
  Cache Benefit: ~3,200 cycles
  Total Benefit: ~18,620 cycles
```

## 7. テスト仕様

### 7.1 単体テスト

```cpp
// tests/mir/inline/test_size_calculator.cpp
TEST(InlineSizeCalculator, BasicFunction) {
    MirFunction func = create_test_function(R"(
        int add(int a, int b) {
            return a + b;
        }
    )");

    InlineSizeCalculator calc;
    EXPECT_EQ(calc.calculate(func), 3);  // load + add + return
}

TEST(InlineSizeCalculator, ComplexFunction) {
    MirFunction func = create_test_function(R"(
        int complex(int x) {
            int a = x * 2;
            int b = x / 3;
            if (a > b) {
                return a - b;
            } else {
                return b - a;
            }
        }
    )");

    InlineSizeCalculator calc;
    EXPECT_EQ(calc.calculate(func), 12);
}
```

### 7.2 統合テスト

```cm
// tests/inline/integration/test_full_pipeline.cm
#[test]
void test_inline_expansion() {
    // インライン化される関数
    #[inline]
    int square(int x) { return x * x; }

    // テスト
    int result = square(5);
    assert_eq(result, 25);

    // アセンブリ検証（call命令がないことを確認）
    #[asm_assert(no_call_to = "square")]
    void verify_inline() {
        volatile int x = square(10);
    }
}
```

## 8. まとめ

この実装仕様により：

1. **厳密な制御**: サイズと再帰の厳格なチェック
2. **詳細な診断**: エラーコードと修正提案
3. **最適化連携**: -O レベルとの適切な統合
4. **デバッグ対応**: インライン情報の保持

が実現されます。

---

**作成者:** Claude Code
**ステータス:** 実装仕様
**次のステップ:** コード実装