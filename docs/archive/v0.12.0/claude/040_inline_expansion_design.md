# Cm言語のインライン展開設計

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計・実装提案

## エグゼクティブサマリー

Cm言語の現在のコンパイラは、限定的なインライン展開機能を持っています。LLVMバックエンドに依存した暗黙的な最適化は行われていますが、明示的なインライン制御のサポートが不足しています。本文書では、現状分析と完全なインライン展開機能の設計を提案します。

## インライン展開とは

インライン展開（Inline Expansion/Inlining）は、関数呼び出しを関数本体のコードで置き換えるコンパイラ最適化技術です。

### 利点
1. **関数呼び出しオーバーヘッドの削除**
   - スタックフレームの作成・破棄コスト削減
   - レジスタの退避・復帰の削減
   - 分岐予測ミスの削減

2. **さらなる最適化機会の創出**
   - 定数伝播
   - デッドコード除去
   - ループ最適化

3. **キャッシュ効率の改善**
   - 命令キャッシュのローカリティ向上

### 欠点
1. **コードサイズの増大**
   - 同じコードの複製
   - 命令キャッシュの圧迫

2. **コンパイル時間の増加**
   - 解析・最適化の負荷増大

## 現在のCm言語の実装状況

### 調査結果

#### 1. 属性システムの基盤あり ✅
```cpp
// src/frontend/ast/module.hpp
struct AttributeNode {
    std::string name;
    std::vector<std::string> args;
};
```
- 基本的な属性構造は定義済み
- パーサーレベルで属性を受け取る準備はある

#### 2. MIRレベルのインラインパスなし ❌
- `src/mir/passes/`にインライン展開パスは未実装
- MIRレベルでの関数インライン化は行われない

#### 3. LLVMレベルの限定的制御 ⚠️
```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
// 特定の関数にnoinline属性を付与
if (func.name.find("alloc") != std::string::npos) {
    llvmFunc->addFnAttr(llvm::Attribute::NoInline);
}
```
- アロケータ関数には`noinline`を強制
- main関数には`noinline`と`optnone`を付与
- その他の関数はLLVMのデフォルト判断に委譲

#### 4. 最適化レベル設定 ✅
```cpp
// src/main.cpp
int optimization_level = 3;  // -O0, -O1, -O2, -O3
```
- コマンドライン引数で最適化レベル指定可能
- LLVMパスマネージャーに渡される

### 現状の問題点

1. **明示的なインライン制御不可**
   - `#[inline]`、`#[inline(always)]`、`#[inline(never)]`属性なし
   - ユーザーが意図的にインライン化を制御できない

2. **MIRレベルの最適化欠如**
   - 言語固有の情報を活用したインライン判断ができない
   - ジェネリック関数の特殊化後のインライン化が困難

3. **デバッグ困難**
   - インライン展開の可視性なし
   - どの関数がインライン化されたか不明

## 提案する設計

### 1. インライン属性の導入

#### 1.1 属性の定義

```cm
// インライン化のヒント（推奨）
#[inline]
int add(int a, int b) {
    return a + b;
}

// 強制的にインライン化
#[inline(always)]
int max(int a, int b) {
    return a > b ? a : b;
}

// インライン化を禁止
#[inline(never)]
void debug_print(string msg) {
    println("[DEBUG] {msg}");
}

// コスト閾値を指定（バイト単位）
#[inline(cost = 20)]
int calculate(int x) {
    return x * x + 2 * x + 1;
}
```

#### 1.2 AST拡張

```cpp
// src/frontend/ast/decl.hpp
enum class InlineHint {
    Default,     // 指定なし（コンパイラ判断）
    Hint,        // #[inline]
    Always,      // #[inline(always)]
    Never,       // #[inline(never)]
    Cost         // #[inline(cost = N)]
};

struct FunctionDecl {
    // ... 既存フィールド
    InlineHint inline_hint = InlineHint::Default;
    int inline_cost = -1;  // costが指定された場合の値
};
```

### 2. MIRレベルのインライン展開パス

#### 2.1 インライン展開パスの実装

```cpp
// src/mir/passes/inline_expansion.hpp
class InlineExpansionPass : public MirPass {
private:
    struct InlineCandidate {
        std::string callee_name;
        size_t call_site_id;
        int benefit_score;    // インライン化の利益スコア
        int cost_score;       // インライン化のコスト
    };

    // インライン化の判断基準
    struct InlinePolicy {
        int max_inline_size = 50;      // 基本的な関数サイズ制限
        int always_inline_size = 200;  // alwaysでも制限
        int loop_bonus = 20;           // ループ内呼び出しのボーナス
        int recursive_penalty = 100;   // 再帰呼び出しのペナルティ
    };

public:
    void run(MirModule& module) override;

private:
    bool should_inline(const MirFunction& caller,
                       const MirFunction& callee,
                       const MirCall& call_site);

    int calculate_inline_cost(const MirFunction& func);
    int calculate_inline_benefit(const MirFunction& caller,
                                 const MirCall& call_site);

    void perform_inlining(MirFunction& caller,
                         const MirFunction& callee,
                         MirCall& call_site);
};
```

#### 2.2 インライン展開のアルゴリズム

```cpp
void InlineExpansionPass::run(MirModule& module) {
    bool changed = true;
    int iteration = 0;
    const int max_iterations = 5;  // 最大5回の反復

    while (changed && iteration < max_iterations) {
        changed = false;
        iteration++;

        // 1. インライン候補を収集
        std::vector<InlineCandidate> candidates;

        for (auto& func : module.functions) {
            if (func.is_external) continue;

            for (auto& bb : func.basic_blocks) {
                for (auto& term : bb.terminators) {
                    if (auto* call = term.as<MirCall>()) {
                        auto* callee = module.find_function(call->name);
                        if (callee && should_inline(func, *callee, *call)) {
                            candidates.push_back({
                                call->name,
                                call->id,
                                calculate_inline_benefit(func, *call),
                                calculate_inline_cost(*callee)
                            });
                        }
                    }
                }
            }
        }

        // 2. 利益/コスト比でソート
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
                return (a.benefit_score - a.cost_score) >
                       (b.benefit_score - b.cost_score);
            });

        // 3. 上位候補をインライン化
        for (const auto& candidate : candidates) {
            if (candidate.benefit_score > candidate.cost_score) {
                // インライン化実行
                auto& caller = *module.find_function_containing(candidate.call_site_id);
                auto& callee = *module.find_function(candidate.callee_name);
                auto& call = *find_call_by_id(caller, candidate.call_site_id);

                perform_inlining(caller, callee, call);
                changed = true;
            }
        }
    }

    if (debug_enabled) {
        std::cerr << "[INLINE] Completed after " << iteration << " iterations\n";
    }
}
```

### 3. インライン化の判断基準

#### 3.1 コスト計算

```cpp
int InlineExpansionPass::calculate_inline_cost(const MirFunction& func) {
    int cost = 0;

    // 基本ブロック数
    cost += func.basic_blocks.size() * 5;

    // 命令数
    for (const auto& bb : func.basic_blocks) {
        cost += bb.instructions.size();
        cost += bb.terminators.size() * 2;  // 分岐は高コスト
    }

    // ローカル変数
    cost += func.locals.size() * 2;

    // 属性による調整
    if (func.inline_hint == InlineHint::Always) {
        cost = cost / 4;  // 75%削減
    } else if (func.inline_hint == InlineHint::Never) {
        cost = INT_MAX;   // 無限大
    } else if (func.inline_hint == InlineHint::Cost) {
        cost = std::min(cost, func.inline_cost);
    }

    return cost;
}
```

#### 3.2 利益計算

```cpp
int InlineExpansionPass::calculate_inline_benefit(const MirFunction& caller,
                                                  const MirCall& call_site) {
    int benefit = 10;  // 基本利益（呼び出しオーバーヘッド削除）

    // ループ内の呼び出し
    if (is_in_loop(caller, call_site)) {
        benefit += policy.loop_bonus;
    }

    // 定数引数がある場合
    int const_args = count_constant_arguments(call_site);
    benefit += const_args * 5;

    // 小さな関数
    auto* callee = find_function(call_site.name);
    if (callee && callee->basic_blocks.size() == 1) {
        benefit += 10;  // 単一基本ブロックはインライン化効果大
    }

    // ホットパス
    if (call_site.profile_count > 1000) {
        benefit += 20;
    }

    return benefit;
}
```

### 4. LLVMバックエンドとの統合

#### 4.1 属性の伝播

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
void apply_inline_attributes(llvm::Function* llvmFunc, const MirFunction& mirFunc) {
    switch (mirFunc.inline_hint) {
        case InlineHint::Always:
            llvmFunc->addFnAttr(llvm::Attribute::AlwaysInline);
            break;

        case InlineHint::Never:
            llvmFunc->addFnAttr(llvm::Attribute::NoInline);
            break;

        case InlineHint::Hint:
            llvmFunc->addFnAttr(llvm::Attribute::InlineHint);
            break;

        case InlineHint::Cost:
            // LLVM固有の閾値設定
            llvmFunc->addFnAttr("inline-threshold",
                std::to_string(mirFunc.inline_cost));
            break;

        case InlineHint::Default:
            // LLVMのデフォルト判断
            break;
    }
}
```

### 5. パーサーの拡張

```cpp
// src/frontend/parser/parser_decl.cpp
std::vector<ast::AttributeNode> Parser::parse_attributes() {
    std::vector<ast::AttributeNode> attrs;

    while (peek().kind == TokenKind::Hash) {
        consume();  // #
        expect(TokenKind::LBracket);  // [

        auto name = expect_identifier();

        if (peek().kind == TokenKind::LParen) {
            consume();  // (

            std::vector<std::string> args;

            if (name == "inline") {
                if (peek().kind == TokenKind::Identifier) {
                    args.push_back(consume().text);  // always/never
                } else if (peek().text == "cost") {
                    args.push_back("cost");
                    expect(TokenKind::Eq);
                    args.push_back(expect_number().text);
                }
            }

            expect(TokenKind::RParen);  // )
        }

        attrs.emplace_back(name, args);
        expect(TokenKind::RBracket);  // ]
    }

    return attrs;
}
```

## 使用例

### 基本的な使用

```cm
// 小さなユーティリティ関数は積極的にインライン化
#[inline(always)]
int abs(int x) {
    return x < 0 ? -x : x;
}

// パフォーマンスクリティカルな関数
#[inline]
double distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

// デバッグ用関数はインライン化しない
#[inline(never)]
void assert_valid(void* ptr, string msg) {
    if (ptr == null) {
        panic("Assertion failed: {msg}");
    }
}

// コスト制限付き
#[inline(cost = 30)]
int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n-1) + fibonacci(n-2);
}
```

### 標準ライブラリでの活用

```cm
// std/algorithm.cm
module std::algorithm;

// 述語を受け取る高階関数は積極的にインライン化
#[inline]
export <T> bool all_of(T[] array, bool (*pred)(T)) {
    for (T elem in array) {
        if (!pred(elem)) return false;
    }
    return true;
}

// ソートは大きいのでインライン化しない
#[inline(never)]
export <T: Ord> void sort(T[] array) {
    // クイックソート実装
}
```

## パフォーマンスへの影響

### ベンチマーク例

```cm
// ベンチマーク: 配列操作
#[inline(always)]
int sum_array(int[] arr) {
    int total = 0;
    for (int x in arr) {
        total += x;
    }
    return total;
}

void benchmark() {
    int[1000] data;
    // データ初期化

    auto start = clock();
    for (int i = 0; i < 1000000; i++) {
        sum_array(data);
    }
    auto end = clock();

    println("Time: {}ms", end - start);
}
```

### 期待される結果

| 最適化 | 実行時間 | コードサイズ |
|--------|----------|--------------|
| インライン化なし | 450ms | 2KB |
| 自動インライン化 | 320ms | 3KB |
| 強制インライン化 | 280ms | 5KB |

## 実装計画

### Phase 1: 属性パーサー（1週間）
- [ ] AttributeNodeの拡張
- [ ] パーサーでの#[inline]サポート
- [ ] AST→HIRでの属性伝播

### Phase 2: MIRインラインパス（2週間）
- [ ] InlineExpansionPassの実装
- [ ] コスト/利益計算
- [ ] 関数本体の複製と変数リネーミング

### Phase 3: LLVMバックエンド統合（1週間）
- [ ] LLVM属性の設定
- [ ] 最適化レベルとの連携
- [ ] デバッグ情報の保持

### Phase 4: テストと最適化（1週間）
- [ ] ユニットテスト作成
- [ ] パフォーマンステスト
- [ ] ヒューリスティックの調整

## デバッグとプロファイリング

### インライン化の可視化

```bash
# インライン化レポートの生成
cm build --inline-report main.cm

# 出力例：
[INLINE] Function 'add' inlined into 'calculate' at line 42 (cost: 5, benefit: 15)
[INLINE] Function 'max' NOT inlined into 'process' (cost: 55 > threshold: 50)
[INLINE] Total functions inlined: 23
[INLINE] Code size increase: +2.3KB (15%)
```

### プロファイルガイド最適化（将来）

```cm
// プロファイル情報を活用
#[hot]  // 頻繁に呼ばれる関数
int hot_function() { ... }

#[cold]  // めったに呼ばれない関数
void error_handler() { ... }
```

## 他言語との比較

### C++
```cpp
inline int add(int a, int b) { return a + b; }
__forceinline int max(int a, int b) { return a > b ? a : b; }
__declspec(noinline) void debug() { }
```

### Rust
```rust
#[inline]
fn add(a: i32, b: i32) -> i32 { a + b }

#[inline(always)]
fn max(a: i32, b: i32) -> i32 { if a > b { a } else { b } }

#[inline(never)]
fn debug() { }
```

### Go
```go
//go:noinline
func debug() { }

// Goは明示的なインライン制御なし（コンパイラ判断）
```

## リスクと対策

### リスク1: コードサイズの爆発
**対策**:
- サイズ制限の設定
- プロファイル情報の活用
- -Os（サイズ最適化）オプションの追加

### リスク2: コンパイル時間の増大
**対策**:
- インクリメンタルコンパイル
- インライン化の段階的実行
- キャッシュの活用

### リスク3: デバッグの困難化
**対策**:
- デバッグビルドではインライン化を制限
- ソースマップの生成
- インライン化レポート

## まとめ

現在のCm言語は基本的なインライン展開機能を持っていますが、以下の改善により、より高度な最適化が可能になります：

1. **明示的な制御**: `#[inline]`属性による細かな制御
2. **MIRレベル最適化**: 言語固有の情報を活用した賢いインライン化
3. **コスト/利益分析**: データドリブンなインライン化判断
4. **デバッグサポート**: 可視化とレポート機能

これらの機能により、パフォーマンスとコードサイズのバランスを取りながら、効果的な最適化が実現できます。

---

**作成者:** Claude Code
**ステータス:** 設計提案
**次のステップ:** Phase 1の属性パーサー実装から開始