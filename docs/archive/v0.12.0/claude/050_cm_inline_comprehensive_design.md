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

## 高度な最適化機能設計（047-049）

### 047: Link Time Optimization (LTO)

#### 基本設計
```cpp
// src/codegen/llvm/lto/lto_manager.hpp
class LTOManager {
private:
    llvm::lto::LTO lto_instance;
    llvm::lto::Config config;
    std::vector<std::unique_ptr<llvm::Module>> modules;

public:
    void configure() {
        config.OptLevel = 3;  // O3最適化
        config.DisableVerify = false;
        config.CPU = llvm::sys::getHostCPUName();

        // ThinLTO設定
        config.UseNewPM = true;
        config.CGOptLevel = llvm::CodeGenOpt::Aggressive;

        // クロスモジュールインライン
        config.OptPipeline = "default<O3>";
        config.AAPipeline = "default";
    }

    void add_module(std::unique_ptr<llvm::Module> module) {
        // ビットコードの保存
        std::string bitcode;
        llvm::raw_string_ostream os(bitcode);
        llvm::WriteBitcodeToFile(*module, os);

        // LTOに追加
        auto buffer = llvm::MemoryBuffer::getMemBuffer(bitcode);
        lto_instance.add(std::move(buffer), {});

        modules.push_back(std::move(module));
    }

    std::unique_ptr<llvm::Module> optimize() {
        // 全モジュールを結合して最適化
        lto_instance.run([](size_t task, const llvm::lto::NativeObjectOutput &output) {
            // ネイティブオブジェクトの処理
        });

        // 最適化されたモジュールを返す
        return merge_and_optimize_modules();
    }

private:
    std::unique_ptr<llvm::Module> merge_and_optimize_modules() {
        // モジュール結合
        auto merged = std::make_unique<llvm::Module>("merged", modules[0]->getContext());
        llvm::Linker linker(*merged);

        for (auto& module : modules) {
            linker.linkInModule(std::move(module));
        }

        // 全体最適化パス
        llvm::PassBuilder PB;
        llvm::ModuleAnalysisManager MAM;
        llvm::ModulePassManager MPM;

        // インタープロシージャル最適化
        MPM.addPass(llvm::GlobalDCEPass());           // 未使用関数削除
        MPM.addPass(llvm::InternalizePASS());         // 非公開化
        MPM.addPass(llvm::IPSCCPPass());              // 定数伝播
        MPM.addPass(llvm::GlobalOptPass());           // グローバル最適化
        MPM.addPass(llvm::DeadArgumentEliminationPass()); // 引数削除

        MPM.run(*merged, MAM);
        return merged;
    }
};
```

#### Cm側の制御
```cm
// コンパイラフラグ
// cm build --lto=thin    # ThinLTO（並列処理可能）
// cm build --lto=full    # Full LTO（最大最適化）
// cm build --lto=off     # LTOなし（デフォルト）

// アトリビュート制御
#[lto(always)]     // 必ずLTOに含める
#[lto(never)]      // LTOから除外
#[lto(thin)]       // ThinLTOのみ
export void critical_function() {
    // この関数は必ずLTO最適化される
}
```

### 048: Profile Guided Optimization (PGO)

#### プロファイル収集
```cpp
// src/codegen/llvm/pgo/profile_manager.hpp
class ProfileManager {
private:
    std::string profile_data_path;
    llvm::ProfileSummaryInfo* PSI;
    llvm::BlockFrequencyInfo* BFI;

public:
    // Phase 1: プロファイル計測コードの挿入
    void instrument_module(llvm::Module& module) {
        llvm::PassBuilder PB;

        // プロファイル計測パス
        PB.registerPipelineStartEPCallback(
            [](llvm::ModulePassManager &MPM, llvm::OptimizationLevel) {
                MPM.addPass(llvm::PGOInstrumentationGen());
            });

        // カウンタ挿入
        for (auto& F : module) {
            if (F.isDeclaration()) continue;

            // 各基本ブロックにカウンタ
            for (auto& BB : F) {
                auto* counter = create_profile_counter(&BB);
                BB.getInstList().push_front(counter);
            }
        }
    }

    // Phase 2: プロファイルデータの読み込み
    void load_profile(const std::string& profile_path) {
        auto buffer = llvm::MemoryBuffer::getFile(profile_path);
        if (!buffer) {
            error("Failed to load profile data");
            return;
        }

        // プロファイル情報の解析
        auto reader = llvm::IndexedInstrProfReader::create(
            std::move(buffer.get())
        );

        PSI = new llvm::ProfileSummaryInfo(module);
        PSI->refresh();
    }

    // Phase 3: プロファイルベースの最適化
    void optimize_with_profile(llvm::Module& module) {
        llvm::PassBuilder PB;

        // ホットパス最適化
        PB.registerPipelineStartEPCallback(
            [this](llvm::ModulePassManager &MPM, llvm::OptimizationLevel) {
                MPM.addPass(llvm::PGOInstrumentationUse(profile_data_path));
                MPM.addPass(llvm::SampleProfileLoaderPass(profile_data_path));
            });

        // 関数ごとの最適化
        for (auto& F : module) {
            optimize_function_with_profile(F);
        }
    }

private:
    void optimize_function_with_profile(llvm::Function& F) {
        if (!BFI) {
            BFI = new llvm::BlockFrequencyInfo(F, *BPI, *LI);
        }

        // ホットパスのインライン化
        for (auto& BB : F) {
            uint64_t freq = BFI->getBlockFreq(&BB).getFrequency();

            if (freq > HOT_THRESHOLD) {
                // 積極的な最適化
                mark_hot_path(&BB);
                aggressive_inline(&BB);
            } else if (freq < COLD_THRESHOLD) {
                // コールドパスの最適化抑制
                mark_cold_path(&BB);
                outline_cold_code(&BB);
            }
        }
    }
};
```

#### Cm側の使用方法
```bash
# Step 1: プロファイル計測ビルド
cm build --pgo-generate=profile.data

# Step 2: 代表的なワークロードを実行
./program typical_input.txt

# Step 3: プロファイルを使った最適化ビルド
cm build --pgo-use=profile.data
```

### 049: SIMD ベクトル化

#### 自動ベクトル化
```cpp
// src/codegen/llvm/vectorization/auto_vectorizer.hpp
class AutoVectorizer {
private:
    llvm::LoopVectorizePass vectorizer;
    llvm::SLPVectorizerPass slp_vectorizer;

public:
    void vectorize_loops(llvm::Function& F) {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;

        // ループのベクトル化
        for (auto& loop : LAM.getLoops(F)) {
            if (can_vectorize(loop)) {
                vectorize_loop(loop);
            }
        }
    }

private:
    bool can_vectorize(llvm::Loop* loop) {
        // ベクトル化可能性のチェック
        if (!loop->isLoopSimplifyForm()) return false;
        if (!loop->getLoopPreheader()) return false;

        // 依存性解析
        auto* DA = new llvm::DependenceAnalysis();
        for (auto* BB : loop->blocks()) {
            for (auto& I1 : *BB) {
                for (auto& I2 : *BB) {
                    if (auto dep = DA->depends(&I1, &I2, true)) {
                        if (!dep->isLoopIndependent()) {
                            return false;  // ループ間依存あり
                        }
                    }
                }
            }
        }

        return true;
    }

    void vectorize_loop(llvm::Loop* loop) {
        // ベクトル幅の決定
        unsigned VF = determine_vector_width(loop);

        // ループの変換
        auto* preheader = loop->getLoopPreheader();
        auto* header = loop->getHeader();

        // ベクトルレジスタの準備
        for (unsigned i = 0; i < VF; i++) {
            // SIMD命令の生成
            generate_simd_instructions(header, i, VF);
        }
    }
};
```

#### Cm側のSIMDサポート
```cm
// SIMD組み込み型
typedef float4 = __vector(float, 4);
typedef int8 = __vector(int, 8);

// SIMD演算
#[vectorize(enable)]
void vector_add(float* a, float* b, float* c, int n) {
    #pragma simd
    for (int i = 0; i < n; i++) {
        c[i] = a[i] + b[i];  // 自動ベクトル化
    }
}

// 明示的なSIMD
void explicit_simd(float4* a, float4* b, float4* c, int n) {
    for (int i = 0; i < n; i++) {
        c[i] = a[i] + b[i];  // float4の加算
    }
}

// SIMD組み込み関数
namespace std::simd {
    float4 load(float* ptr);
    void store(float* ptr, float4 v);
    float4 add(float4 a, float4 b);
    float4 mul(float4 a, float4 b);
    float4 fma(float4 a, float4 b, float4 c);  // a * b + c
    float horizontal_sum(float4 v);
}
```

## インライン実装詳細設計（052-059）

### 052: Inline Parser実装

```cpp
// src/frontend/parser/inline_parser.cpp
class InlineParser {
public:
    void parse_inline_attribute(FunctionDecl* func) {
        if (current_token == Token::LBRACKET) {
            consume(Token::LBRACKET);

            if (consume_keyword("inline")) {
                if (consume(Token::LPAREN)) {
                    parse_inline_directive(func);
                    consume(Token::RPAREN);
                }
            }

            consume(Token::RBRACKET);
        }
    }

private:
    void parse_inline_directive(FunctionDecl* func) {
        if (check_keyword("always")) {
            func->inline_directive = InlineDirective::Always;
        } else if (check_keyword("never")) {
            func->inline_directive = InlineDirective::Never;
        } else if (check_keyword("hint")) {
            func->inline_directive = InlineDirective::Hint;
        }
    }
};
```

### 053: Inline HIR統合

```cpp
// src/hir/inline_hir.hpp
struct HIRInlineAnalyzer {
    struct InlineMetrics {
        size_t instruction_count;
        size_t call_count;
        size_t loop_depth;
        bool has_recursion;
        bool has_indirect_calls;
    };

    InlineMetrics analyze(const HIRFunction& func) {
        InlineMetrics metrics{};

        for (const auto& stmt : func.body) {
            analyze_statement(stmt, metrics, 0);
        }

        return metrics;
    }

private:
    void analyze_statement(const HIRStmt& stmt, InlineMetrics& m, int loop_depth) {
        m.instruction_count++;

        switch (stmt.kind) {
            case HIRStmtKind::Call:
                m.call_count++;
                if (stmt.call.is_indirect) {
                    m.has_indirect_calls = true;
                }
                if (stmt.call.target == func.name) {
                    m.has_recursion = true;
                }
                break;

            case HIRStmtKind::Loop:
                m.loop_depth = std::max(m.loop_depth, loop_depth + 1);
                for (const auto& s : stmt.loop.body) {
                    analyze_statement(s, m, loop_depth + 1);
                }
                break;
        }
    }
};
```

### 054: Inline MIR最適化

```cpp
// src/mir/passes/inline_pass.cpp
class InlinePass : public MIRPass {
private:
    std::map<std::string, InlineDecision> decisions;

public:
    void run(MIRProgram& program) override {
        // Phase 1: インライン候補の収集
        collect_candidates(program);

        // Phase 2: インライン決定
        make_decisions(program);

        // Phase 3: インライン展開
        perform_inlining(program);

        // Phase 4: クリーンアップ
        cleanup_after_inlining(program);
    }

private:
    void perform_inlining(MIRProgram& program) {
        for (auto& func : program.functions) {
            for (auto& bb : func.basic_blocks) {
                for (auto& stmt : bb.statements) {
                    if (stmt.kind == MIRStmtKind::Call) {
                        if (should_inline(stmt.call)) {
                            inline_call(func, bb, stmt);
                        }
                    }
                }
            }
        }
    }

    void inline_call(MIRFunction& caller, MIRBlock& bb, MIRStmt& call) {
        auto* callee = find_function(call.target);
        if (!callee) return;

        // パラメータマッピング
        std::map<MIROperand, MIROperand> mapping;
        for (size_t i = 0; i < call.args.size(); i++) {
            mapping[callee->params[i]] = call.args[i];
        }

        // 関数本体のコピーと変数置換
        auto inlined_body = clone_and_substitute(callee->body, mapping);

        // 呼び出し文を展開した本体で置換
        replace_statement(bb, call, inlined_body);
    }
};
```

### 055: Inline LLVM統合

```cpp
// src/codegen/llvm/inline_llvm.cpp
class LLVMInliner {
private:
    llvm::InlineParams params;

public:
    void setup_inline_params() {
        params.DefaultThreshold = 225;  // デフォルト閾値
        params.HintThreshold = 325;     // hint時の閾値
        params.ColdThreshold = 45;      // コールドパスの閾値
        params.OptSizeThreshold = 75;   // サイズ最適化時
        params.LocallyHotCallSiteThreshold = 525;  // ホットパス
    }

    void apply_inline_attributes(llvm::Function* func, InlineDirective directive) {
        switch (directive) {
            case InlineDirective::Always:
                func->addFnAttr(llvm::Attribute::AlwaysInline);
                break;
            case InlineDirective::Never:
                func->addFnAttr(llvm::Attribute::NoInline);
                break;
            case InlineDirective::Hint:
                func->addFnAttr(llvm::Attribute::InlineHint);
                break;
        }
    }

    void run_inline_pass(llvm::Module& module) {
        llvm::legacy::FunctionPassManager FPM(&module);

        // インラインパスの追加
        FPM.add(llvm::createFunctionInliningPass(params));
        FPM.add(llvm::createEarlyCSEPass());  // 共通部分式除去
        FPM.add(llvm::createCFGSimplificationPass());  // CFG簡略化

        for (auto& F : module) {
            FPM.run(F);
        }
    }
};
```

### 056: Inline Test

```cm
// tests/inline/inline_test.cm
#[test]
void test_inline_always() {
    #[inline(always)]
    int add(int a, int b) {
        return a + b;
    }

    // この呼び出しは必ずインライン展開される
    int result = add(1, 2);
    assert(result == 3);

    // アセンブリで確認
    // 関数呼び出しではなく、直接加算になっているはず
}

#[test]
void test_inline_never() {
    #[inline(never)]
    int multiply(int a, int b) {
        return a * b;
    }

    // この呼び出しはインライン展開されない
    int result = multiply(3, 4);
    assert(result == 12);

    // アセンブリで確認
    // call命令が残っているはず
}

#[test]
void test_recursive_inline() {
    #[inline(always)]  // エラーになるはず
    int factorial(int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    }

    // コンパイルエラー: 再帰関数はinline(always)不可
}
```

### 057: Inline Benchmark

```cm
// benchmarks/inline_benchmark.cm
import std::time;

struct BenchmarkResult {
    String name;
    double time_ms;
    size_t iterations;
}

// ベンチマーク1: 小さい関数のインライン
#[inline(always)]
int small_func_inlined(int x) {
    return x * 2 + 1;
}

#[inline(never)]
int small_func_not_inlined(int x) {
    return x * 2 + 1;
}

BenchmarkResult bench_small_function() {
    const int iterations = 10000000;

    // インライン版
    auto start1 = time::now();
    int sum1 = 0;
    for (int i = 0; i < iterations; i++) {
        sum1 += small_func_inlined(i);
    }
    auto time1 = time::elapsed_ms(start1);

    // 非インライン版
    auto start2 = time::now();
    int sum2 = 0;
    for (int i = 0; i < iterations; i++) {
        sum2 += small_func_not_inlined(i);
    }
    auto time2 = time::elapsed_ms(start2);

    println(f"Inline speedup: {time2 / time1}x");

    return BenchmarkResult{
        "small_function_inline",
        time1,
        iterations
    };
}

// ベンチマーク2: 大きい関数のインライン（コード膨張の測定）
void measure_code_size() {
    // コンパイル時にバイナリサイズを比較
    // cm build --inline-threshold=0   # インラインなし
    // cm build --inline-threshold=999 # 積極的インライン
}
```

### 058: Inline Debug

```cpp
// src/debug/inline_debugger.hpp
class InlineDebugger {
private:
    struct InlineEvent {
        std::string caller;
        std::string callee;
        SourceLocation location;
        InlineDecision decision;
        std::string reason;
    };

    std::vector<InlineEvent> events;

public:
    void record_inline_decision(const std::string& caller,
                               const std::string& callee,
                               SourceLocation loc,
                               InlineDecision decision,
                               const std::string& reason) {
        events.push_back({caller, callee, loc, decision, reason});
    }

    void dump_inline_report(std::ostream& out) {
        out << "=== Inline Optimization Report ===\n";
        out << "Total decisions: " << events.size() << "\n\n";

        int inlined_count = 0;
        int not_inlined_count = 0;

        for (const auto& event : events) {
            if (event.decision == InlineDecision::Yes) {
                inlined_count++;
                out << "[INLINED] ";
            } else {
                not_inlined_count++;
                out << "[NOT INLINED] ";
            }

            out << event.caller << " -> " << event.callee
                << " at " << event.location << "\n";
            out << "  Reason: " << event.reason << "\n";
        }

        out << "\nSummary:\n";
        out << "  Inlined: " << inlined_count << "\n";
        out << "  Not inlined: " << not_inlined_count << "\n";
    }
};
```

### 059: Inline Documentation

```markdown
# Cm Inline Optimization Guide

## 概要
Cm言語のインライン最適化は、関数呼び出しのオーバーヘッドを削減し、
さらなる最適化の機会を提供します。

## 使用方法

### 基本的な使い方
\```cm
#[inline(always)]  // 必ずインライン展開
int add(int a, int b) {
    return a + b;
}

#[inline(never)]   // インライン展開しない
void debug_log(String msg) {
    println(f"[DEBUG] {msg}");
}

#[inline(hint)]    // コンパイラにヒントを与える
int calculate(int x) {
    return x * x + 2 * x + 1;
}
\```

### ベストプラクティス

1. **小さな関数には`inline(always)`**
   - 1-3行の単純な関数
   - getter/setter

2. **デバッグ関数には`inline(never)`**
   - ログ出力
   - アサーション

3. **通常は`inline(hint)`かデフォルト**
   - コンパイラの判断に任せる

### トラブルシューティング

**Q: インライン展開されているか確認したい**
A: `--dump-inline-report`フラグを使用
\```bash
cm build --dump-inline-report
\```

**Q: コードサイズが大きくなりすぎる**
A: インライン閾値を調整
\```bash
cm build --inline-threshold=100  # デフォルト: 225
\```

**Q: 再帰関数がインライン展開でエラーになる**
A: 再帰関数は`inline(always)`できません。`inline(hint)`を使用してください。

## パフォーマンスガイドライン

| 関数の種類 | 推奨設定 | 理由 |
|-----------|---------|------|
| Getter/Setter | `always` | 非常に小さく、頻繁に呼ばれる |
| 数値計算 | `hint` | サイズと速度のバランス |
| I/O操作 | `never` | 大きく、最適化効果が薄い |
| ホットパス | `always`/`hint` | プロファイル結果に基づく |
| コールドパス | `never` | コードサイズ削減 |
```