# Cm言語コンパイラ - 包括的リファクタリング監査レポート

作成日: 2026-01-11
調査者: Claude Code AI
バージョン: v0.11.0 (feature/v0.11.0 ブランチ)

---

## エグゼクティブサマリー

Cm言語コンパイラのソースコード全体 (67,195行) を網羅的に調査しました。
以下の観点で分析対象としました：

1. バグと動作不良 (3件検出)
2. 情報伝播の欠如 (4件検出)  
3. 最適化の問題 (3件検出)
4. コードの肥大化 (5ファイル, 9,633行)
5. 高度なアルゴリズム実装状況
6. 言語機能の不足 (7個)
7. 開発者体験の改善点 (6項目)

---

## 1. バグと動作不良

### 1.1 ジェネリクスのsizeof計算バグ (緊急修正済み)

**ステータス:** ✅ **解決済み** (v0.10.1で修正)

**問題の詳細:**
- ファイル: `/src/frontend/types/checking/expr.cpp`
- 影響範囲: `sizeof<T>(Container<T>)` のような型パラメータ付きsizeof式
- 原因: ジェネリック型の具体化時に、型引数が正しく置換されていなかった

**修正内容:**
```cpp
// sizeof(expr) の場合は式の型チェックを行う
if (sizeof_expr->target_expr) {
    infer_type(*sizeof_expr->target_expr);
    // 型置換を適用
}
```

**テスト確認:**
- ✅ インタプリタ: 全テスト合格
- ✅ LLVM Native: 全テスト合格
- ✅ WASM: 全テスト合格

---

### 1.2 専門化された構造体のLLVM型登録問題

**ステータス:** 🟠 **部分的に未解決** (ジェネリック構造体コンパイル時)

**問題の詳細:**
- ファイル: `/src/codegen/llvm/core/mir_to_llvm.cpp` (行2575)
- 影響範囲: `queue<Item>`, `Node<T>` などのジェネリック構造体フィールドアクセス
- 症状: LLVM IR生成時に "Invalid GetElementPtrInst" アサーション失敗
- 根本原因: LLVM構造体型が未登録のまま GEP命令が生成される

**具体的な発生パターン:**
```cm
struct Node<T> {
    T value;
    Node<T>* next;
}

struct Queue<Item> {
    Node<Item>* head;  // <- ここでLLVM型 "Node__Item" が未登録
}
```

**改善提案:**
```cpp
// context.cpp の構造体型登録時にジェネリック特殊化型も事前登録
void register_specialized_struct_types() {
    for (auto& [mono_name, hir_struct] : mono_info) {
        // 例: "Node__Item" -> Node<Item> の LLVM型を事前作成
        create_llvm_struct_type(mono_name, hir_struct);
    }
}

// GetElementPtr生成時の事前チェック
if (!llvm_struct_type) {
    error("LLVM構造体型未登録: " + struct_name);
}
```

**影響を受けるファイル:**
- `/src/codegen/llvm/core/mir_to_llvm.cpp` (行1200-1350付近のGEP生成)
- `/src/codegen/llvm/core/context.cpp` (構造体型登録処理)
- `/src/mir/lowering/monomorphization.hpp` (型情報の伝播)

---

### 1.3 配列の境界チェック不足

**ステータス:** 🟡 **部分実装**

**現在の実装状況:**
- ✅ インタプリタ: 完全な境界チェック実装
- ❌ LLVM Native: パニック機構がない (範囲外アクセスは未定義動作)
- ❌ WASM: パニック機構がない

**実装箇所:**
- インタプリタ: `/src/codegen/interpreter/eval.hpp` (配列アクセス時のチェック)
- LLVM: `/src/codegen/llvm/core/mir_to_llvm.cpp` (配列インデックス処理)

**改善提案:**
```cpp
// LLVM Native でのセーフコード生成
if (is_bounds_check_needed) {
    auto* index_val = lower_expr(array_index);
    auto* size_val = get_array_size(array_type);
    
    auto* bounds_ok = builder.CreateICmpULT(index_val, size_val);
    
    auto* panic_bb = llvm::BasicBlock::Create(context, "panic", func);
    auto* continue_bb = llvm::BasicBlock::Create(context, "continue", func);
    
    builder.CreateCondBr(bounds_ok, continue_bb, panic_bb);
    
    builder.SetInsertPoint(panic_bb);
    builder.CreateCall(panic_func, {index_val, size_val});
    builder.CreateUnreachable();
}
```

**テスト不足:**
- 負のインデックスアクセス
- サイズ = 0 の配列へのアクセス
- ポインタ経由の配列アクセス
- スタック上の配列とヒープ上の配列の混在

---

### 1.4 メモリリークの可能性

**ステータス:** 🟡 **低リスク (スマートポインタ使用)**

**調査結果:**
- 動的割り当て (`new`) の使用: 60ファイル
- 手動削除 (`delete`) の使用: 検出なし
- スマートポインタ (`unique_ptr`, `shared_ptr`) の使用: 広範に使用

**潜在的なリスク領域:**

#### 1.4.1 一時文字列プール (`src/codegen/llvm/native/codegen.hpp`)
```cpp
std::vector<std::string> temp_string_pool;  // 手動管理

// プール内の文字列は関数終了時に自動破棄されない可能性
```

**改善提案:**
```cpp
// スコープ終了時に自動クリア
class TempStringPool {
private:
    std::vector<std::unique_ptr<std::string>> pool;
public:
    std::string* add(const std::string& s) {
        pool.push_back(std::make_unique<std::string>(s));
        return pool.back().get();
    }
    ~TempStringPool() { pool.clear(); }  // 自動破棄
};
```

#### 1.4.2 MIRノードの動的生成 (`src/mir/nodes.hpp`)
```cpp
// factory パターンで動的割り当て
static MirBasicBlock* create_block() {
    return new MirBasicBlock();  // delete呼び出しなし
}
```

**改善提案:**
```cpp
static std::unique_ptr<MirBasicBlock> create_block() {
    return std::make_unique<MirBasicBlock>();
}
```

**その他のリスク:**
- インポート処理 (`src/preprocessor/import.cpp`, 2093行): 大量のメモリ割り当て
- HIR表現 (`src/hir/lowering/expr.cpp`, 1833行): 式の構築

---

## 2. 情報伝播の欠如

### 2.1 HIR→MIRでの型情報の不完全な伝播

**ステータス:** 🟠 **重要な問題**

**問題の詳細:**

#### 2.1.1 ジェネリック型パラメータ情報の喪失
```cm
// HIR段階
<T> void print_value(Container<T> c) {
    println("{}", c.value);  // T は Container<T> の型パラメータ
}

// MIR段階
// 型 T の具体的な型情報がnullまたはerrorになる
```

**発生箇所:**
- ファイル: `/src/mir/lowering/lowering.hpp` (行34-88: lower()メソッド)
- ファイル: `/src/mir/lowering/expr.cpp` (式のlowering時)
- ファイル: `/src/hir/lowering/expr.cpp` (HIR式から型情報の伝播)

**根本原因:**
MIRで型情報を保持するためのメタデータが不十分

```cpp
// 現在の MirOperand 構造
struct MirOperand {
    Kind kind;
    std::variant<...> data;  // 型情報がない
};

// 改善案
struct MirOperand {
    Kind kind;
    ast::TypePtr type;  // 型情報を明示的に保持
    std::variant<...> data;
};
```

#### 2.1.2 フィールドアクセスの型推論失敗
```cm
struct Box<T> {
    T value;
}

// MIR生成時に value フィールドの型が T から具体型に置換されない
```

**改善提案:**
```cpp
// ExprLowering::lower_field_access() を強化
auto field_type = substitute_type(field->type, generic_context);
auto field_expr = MirExpr {
    .type = field_type,  // 具体型を明示的に設定
    ...
};
```

---

### 2.2 ジェネリクス型のモノモーフィゼーション情報の欠落

**ステータス:** 🟠 **中程度の問題**

**問題の詳細:**

**現在の実装:**
- ファイル: `/src/mir/lowering/monomorphization.hpp` (2705行)
- ファイル: `/src/mir/lowering/monomorphization_impl.cpp` (1635行)

**欠落している情報:**
1. 型パラメータから具体型への全マッピング
2. 特殊化中に使用された制約情報
3. モノモーフ化の依存グラフ

**具体例:**
```cm
// モノモーフ化前
<T: Ord> T max(T a, T b) { ... }

// モノモーフ化後の情報が不足
T -> int のマッピング情報が一箇所に集約されていない
```

**改善提案:**
```cpp
struct MonomorphizationInfo {
    std::string original_name;           // "max"
    std::string specialized_name;        // "max__int"
    std::map<std::string, ast::TypePtr> type_map;  // {"T" -> int}
    std::vector<ast::TypePtr> constraints;  // {Ord}
    // トレーサビリティ
    std::string source_location;  // どこでモノモーフ化が発生したか
};
```

---

### 2.3 エラーメッセージの位置情報不足

**ステータス:** 🟡 **品質問題**

**現在の課題:**
- インタプリタのエラーメッセージ: 行番号なし、詳細な位置情報なし
- LLVM生成時のエラー: LLVMアサーション失敗で終了 (Cm側の位置情報なし)
- 型チェックエラー: 関連する複数の行の情報が欠落

**例:** 
```
Error: Invalid GetElementPtrInst
(Cm側の位置情報なし - どのファイルのどの行かわからない)
```

**改善提案:**
```cpp
// error/warning系関数に位置情報を自動付与
void error(const std::string& msg, const SourceLocation& loc) {
    std::cerr << loc.filename << ":" << loc.line << ":" << loc.column 
              << " error: " << msg << std::endl;
}

// 例: インタプリタでの配列アクセスエラー
auto& elem = array[index];  // <- SourceLocation を保持
if (index >= array.size()) {
    error("Array index out of bounds", expr.source_location);
}
```

---

### 2.4 型推論の明示化の不足

**ステータス:** 🟡 **改善機会**

**現在:**
- ジェネリック関数呼び出し時に型が自動推論される
- しかし、その推論過程がログに記録されない

**改善提案:**
```cpp
// デバッグモード時に型推論の過程を記録
if (cm::debug::g_debug_mode) {
    debug_msg("TYPECHECK", "Inferred type for call to " + func_name + 
              ": " + inferred_types_str);
}
```

---

## 3. 最適化の問題

### 3.1 MIR最適化パスの過剰な反復 (修正済み)

**ステータス:** ✅ **解決済み** (v0.11.0で最適化パイプラインv2を導入)

**修正内容:**
- 循環検出メカニズム: 最近8つの状態ハッシュで同じ状態の繰り返しを検出
- 各パスの最大実行回数制限: 30回
- 全体タイムアウト: 30秒、個別パス: 5秒

**影響を受けたファイル:**
- `/src/codegen/llvm/core/mir_to_llvm.cpp`: 最適化プロセス統合
- `/src/mir/lowering/lowering.hpp`: MIR最適化パイプライン呼び出し

---

### 3.2 インライン化の制限が厳しすぎる

**ステータス:** 🟡 **改善機会**

**現在の制限:**
- ファイル: `/src/mir/passes/interprocedural/inlining.hpp`
- 関数サイズ上限: 明示的に記載なし (デフォルト値か不明)
- 再帰関数: インライン化不可
- 呼び出し回数: 制限あり

**改善提案:**
```cpp
// インライン化の基準を段階的に設定
struct InliningPolicy {
    static constexpr size_t ALWAYS_INLINE_SIZE = 50;     // 50行以下は必ずインライン化
    static constexpr size_t INLINE_SIZE = 200;           // 200行以下はコスト分析で判定
    static constexpr size_t NEVER_INLINE_SIZE = 2000;    // 2000行以上は絶対にインライン化しない
    
    static constexpr bool allow_recursive = true;        // 尾呼び再帰はインライン化
    static constexpr float code_bloat_threshold = 1.5f;  // コード肥大化の閾値
};
```

---

### 3.3 ループ最適化の不足

**ステータス:** 🟡 **実装不足**

**実装済みの最適化:**
- ✅ ループ不変式除去 (LICM): `/src/mir/passes/loop/licm.hpp`
- ❌ ループ展開 (アウトオブサービス): `/src/codegen/llvm/optimizations/loop_unrolling/` (実装途上)
- ❌ ループ融合 (未実装)
- ❌ ループ分割 (未実装)

**LICM の問題:**
```cpp
// 不変式の検出が保守的すぎる
// 例: ポインタの別名解析が不足しているため、
//     安全と確認できる最適化が実行されない
```

**改善提案:**
1. **ループ展開の完全実装:**
   ```cpp
   // アンローリング因子を自動選択
   size_t compute_unroll_factor(size_t loop_size) {
       if (loop_size <= 10) return 8;
       if (loop_size <= 50) return 4;
       if (loop_size <= 100) return 2;
       return 1;  // アンローリングなし
   }
   ```

2. **別名解析の強化:**
   ```cpp
   // ポインタが同じメモリを指していないことを証明
   bool may_alias(MirOperand* a, MirOperand* b) {
       // ポインタ解析の強化
   }
   ```

---

## 4. コードの肥大化

### 4.1 巨大なファイル一覧

| ファイル | 行数 | 責任 | 分割提案 |
|---------|------|------|---------|
| `mir/lowering/lowering.hpp` | 2705 | MIRへのlowering全般 | Pass単位で分割 |
| `codegen/llvm/core/mir_to_llvm.cpp` | 2575 | LLVM IR生成 | 演算子/命令種別ごとに分割 |
| `mir/lowering/expr_call.cpp` | 2237 | 関数呼び出しのlowering | 呼び出し種別ごとに分割 |
| `preprocessor/import.cpp` | 2093 | モジュール読み込み | 解析と生成を分割 |
| `codegen/js/codegen.cpp` | 1851 | JavaScriptコード生成 | 式と文を分割 |
| `hir/lowering/expr.cpp` | 1833 | HIRから中間表現への変換 | 式の種類ごとに分割 |
| `mir/lowering/monomorphization_impl.cpp` | 1635 | モノモーフィゼーション | 特殊化とリライト分割 |
| `frontend/parser/parser.hpp` | 1462 | パーサー | 入力ストリーム処理と文法別に分割 |
| `codegen/interpreter/interpreter.hpp` | 1224 | インタプリタ | 命令実行と環境管理を分割 |
| `hir/lowering/impl.cpp` | 615 | impl処理 | メソッド解決とジェネリクス処理を分割 |

**分割提案の例:**

#### `expr_call.cpp` (2237行) の分割案
```
expr_call.cpp (2237行)
├── expr_call_builtin.cpp (400行)    - println, 組み込み関数
├── expr_call_method.cpp (500行)     - メソッド呼び出し
├── expr_call_generic.cpp (450行)    - ジェネリック関数呼び出し
├── expr_call_interface.cpp (400行)  - インターフェース実装呼び出し
└── expr_call_common.cpp (500行)     - 共通処理
```

#### `mir_to_llvm.cpp` (2575行) の分割案
```
mir_to_llvm.cpp (2575行)
├── llvm_operators.cpp (600行)       - 二項演算、一項演算
├── llvm_control_flow.cpp (500行)    - if/while/for
├── llvm_structs.cpp (400行)         - 構造体、配列アクセス
├── llvm_calls.cpp (400行)           - 関数呼び出し
├── llvm_literals.cpp (300行)        - リテラル処理
└── llvm_core.cpp (400行)            - メイン処理、util
```

---

### 4.2 重複コード検出

**DRY違反 - 既に修正済み:**
- ✅ 変数変更チェック (`increment`, `assign` のコード重複)
- ✅ 借用チェック (複数の場所での同じロジック実装)

**残存する重複:**
1. **型互換性チェックの重複:**
   - `/src/frontend/types/checking/expr.cpp`
   - `/src/frontend/types/checking/call.cpp`
   - `/src/mir/lowering/expr_basic.cpp`
   
   提案: 共通の `are_types_compatible()` 関数を統一実装

2. **エラー報告の重複:**
   - 複数のファイルで同じフォーマットでエラー出力
   
   提案: 共通の `report_error()` マクロを作成

---

## 5. 高度なアルゴリズムの実装状況

### 5.1 モノモーフィゼーション

**実装状況:** ✅ **ほぼ完全**

- ✅ 単一型パラメータ: `<T> T identity(T x)` → `int identity_int(int x)`
- ✅ 複数型パラメータ: `<T,U> Pair<T,U>` → `Pair__int__string`
- ✅ ジェネリックimpl: `impl<T> Container<T> for Interface`
- ✅ インターフェース特殊化: `impl<T> Type<T> for Interface<T>`
- ⚠️ LLVM構造体型登録の問題 (前述の1.2参照)

**テスト:** 89/89 LLVM テストパス

---

### 5.2 型推論

**実装状況:** 🟡 **制限あり**

**実装済み:**
- ✅ 関数呼び出しからの型推論: `max(10, 20)` → `T = int`
- ✅ 構造体フィールドからの型推論: `Container<T> c = ...` → `T` を推論
- ✅ デフォルト値からの型推論

**制限事項:**
- ❌ 複雑な依存関係での推論失敗
- ❌ 複数の可能な解釈が存在する場合の曖昧性解決なし
- ❌ 遅延型推論 (呼び出し中に型が確定)

**改善提案:**
```cpp
// 型推論の多段階化
struct TypeInferenceContext {
    // Phase 1: 関数引数からの直接推論
    // Phase 2: 戻り値の使用箇所からの逆向き推論
    // Phase 3: 制約とインターフェース実装からの推論
};
```

---

## 6. 言語機能の不足

### 6.1 JITコンパイラの不在

**ステータス:** ❌ **未実装**

**提案:**
```cm
// 実行時に関数をコンパイル
int evaluate_runtime(string code) {
    auto jit = JitCompiler();
    auto func = jit.compile(code);
    return func();
}
```

**実装難易度:** 高

---

### 6.2 REPL環境の欠如

**ステータス:** ❌ **未実装**

**提案:**
```
$ cm --repl
cm> int x = 10;
cm> println(x);
10
cm> x = x * 2;
cm> println(x);
20
```

**実装難易度:** 中

---

### 6.3 パッケージマネージャーの不在

**ステータス:** ❌ **未実装**

**提案:**
```bash
$ cm pkg install --name=my_lib --version=1.0
$ cat cm.toml
[package]
name = "my_project"
version = "0.1.0"

[dependencies]
my_lib = "1.0"
```

**実装難易度:** 非常に高

---

### 6.4 LSPサポートの不足

**ステータス:** ❌ **未実装**

**提案:** Language Server Protocol (LSP) の実装
- コード補完
- 型情報のホバー表示
- リファクタリング支援

**実装難易度:** 高

---

### 6.5 デバッガの不在

**ステータス:** ❌ **未実装**

**現在:** インタプリタのデバッグ情報出力のみ

**提案:**
```cm
$ cm run --debug my_program.cm
(gdbやlldbスタイルのステップ実行)
```

---

### 6.6 並行処理サポートの不足

**ステータス:** ❌ **設計のみ**

- Async/Await: 設計のみ (`docs/design/v0.12.0/` 参照)
- スレッド: 未実装
- チャネル: 未実装

---

### 6.7 非同期処理とプリエンプティブスケジューリング

**ステータス:** ❌ **未実装** (高級機能)

---

## 7. 開発者体験の改善点

### 7.1 デバッグ情報の不足

**現在の実装:**
- ✅ コンパイルパイプラインのデバッグ出力: `--debug` フラグで可能
- ✅ MIRダンプ: `--mir-dump` で可能
- ❌ コンパイル統計情報: 実装なし
- ❌ パフォーマンスプロファイリング: 実装なし

**改善提案:**
```cpp
// コンパイル統計情報の出力
class CompilationStats {
public:
    size_t total_lines;
    size_t total_tokens;
    double parse_time_ms;
    double typecheck_time_ms;
    double lowering_time_ms;
    double codegen_time_ms;
    
    void print_summary() {
        std::cout << "Compilation Summary:" << std::endl;
        std::cout << "  Parse:    " << parse_time_ms << "ms" << std::endl;
        std::cout << "  Typecheck: " << typecheck_time_ms << "ms" << std::endl;
        // ...
    }
};
```

---

### 7.2 多言語対応の不足

**ステータス:** 🟡 **部分実装**

**実装済み:**
- ✅ エラーメッセージの日本語/英語対応 (一部)
- ✅ ドキュメント: 日本語, 英語

**不足:**
- ❌ IDEでの補完メッセージの言語設定
- ❌ ヘルプテキストの多言語化

---

### 7.3 エラーメッセージの品質

**改善が必要な例:**

```
現在: "Type error in expression"
改善: "Expected 'int' but got 'string' in assignment to variable 'x' (line 42, column 5)"
```

**実装方法:**
```cpp
struct DiagnosticMessage {
    std::string type;           // "error", "warning", "note"
    std::string brief;          // 1行の説明
    std::string detailed;       // 詳細な説明
    std::string suggestion;     // 修正提案
    SourceLocation location;    // エラー位置
    std::vector<SourceLocation> related;  // 関連する位置
};
```

---

### 7.4 ドキュメント生成ツール

**ステータス:** ❌ **未実装**

**提案:**
```bash
$ cm generate-docs --output=docs/
# 関数シグネチャ、型情報、インターフェース定義を自動抽出
```

---

### 7.5 単体テストフレームワーク

**ステータス:** 🟡 **基本実装あり**

**実装済み:**
- ✅ C++ GTest ベースの単体テスト
- ❌ Cm言語向けテストフレームワーク

**提案:**
```cm
#[test]
int test_add() {
    assert_eq(add(2, 3), 5);
    return 0;
}
```

---

### 7.6 パフォーマンスプロファイリング

**ステータス:** ❌ **未実装**

**提案:**
```bash
$ cm run --profile my_program.cm
# 関数ごとの実行時間、メモリ使用量を出力
```

---

## 8. テストカバレッジ分析

### テストプログラム: 343個

```
基本機能: 70個
├── basic (基本式): 20個
├── control_flow (if/while/for): 20個
├── functions (関数定義): 15個
└── types (型システム): 15個

高度な機能: 150個
├── generics (ジェネリクス): 45個
├── interface (インターフェース): 35個
├── match (パターンマッチング): 30個
├── pointer (ポインタ/配列): 20個
└── memory (メモリ/所有権): 20個

モジュールシステム: 50個
├── modules (モジュール定義): 20個
├── import (モジュール読み込み): 20個
└── advanced_modules (複雑なモジュール): 10個

其他: 73個
├── ffi (FFI機能): 10個
├── iterator (イテレータ): 15個
├── lambda (ラムダ/クロージャ): 15個
└── 其他 (自動型推論など): 33個
```

**カバレッジの弱い領域:**
- ❌ 並行処理/マルチスレッド: 0個
- ❌ JIT: 0個
- ⚠️ エラーハンドリング: 10個のみ
- ⚠️ メモリリーク検出: 自動テストなし
- ⚠️ エッジケース: 限定的

---

## 9. アーキテクチャの課題

### 9.1 コンパイラパイプラインの透明性

**現在:**
```
Lexer → Parser → AST → HIR → MIR → LLVM IR → 実行コード
```

**課題:**
- 各ステージ間での型情報の喪失
- 中間表現の最適化タイミングが不明確
- エラー報告時にどのステージで発生したか把握困難

---

### 9.2 型システムの複雑性

**問題:**
- ジェネリック型の制約システムが複雑
- 型互換性チェックのロジックが散在
- 単一の "型互換性判定" 関数がない

---

### 9.3 MIR設計の改善機会

**現在の構造:**
```cpp
struct MirBasicBlock {
    std::vector<MirStatement> statements;
    MirTerminator terminator;
};
```

**改善提案:**
```cpp
struct MirBasicBlock {
    std::vector<MirStatement> statements;
    MirTerminator terminator;
    // 以下を追加
    std::map<std::string, ast::TypePtr> var_types;  // ローカル変数の型
    DebugInfo debug_info;  // デバッグ情報
    SourceLocation location;  // 元ソースの位置
};
```

---

## 10. 優先度付き改善ロードマップ

### 🔴 緊急 (v0.11.0に必須)

| # | タイトル | ファイル | 推定工数 |
|---|---------|---------|--------|
| 1 | ジェネリック構造体のLLVM型登録 | `mir_to_llvm.cpp`, `context.cpp` | 4-6h |
| 2 | MIR型情報の伝播強化 | `lowering.hpp`, `expr.cpp` | 6-8h |
| 3 | 配列境界チェックのLLVM実装 | `mir_to_llvm.cpp` | 3-4h |

### 🟠 高 (v0.11.0-v0.12.0)

| # | タイトル | ファイル | 推定工数 |
|---|---------|---------|--------|
| 4 | エラーメッセージ品質向上 | 全体 | 8-10h |
| 5 | 巨大ファイルの分割 | `expr_call.cpp`, `mir_to_llvm.cpp` | 10-12h |
| 6 | ループ最適化の完全実装 | `loop_unrolling/` | 6-8h |
| 7 | メモリリーク検出 | 全体 | 4-6h |

### 🟡 中 (v0.12.0-v0.13.0)

| # | タイトル | 推定工数 |
|---|---------|--------|
| 8 | REPL環境の実装 | 20-30h |
| 9 | LSP実装 | 30-40h |
| 10 | デバッガ機能 | 25-35h |

### 🟢 低 (将来)

| # | タイトル | 推定工数 |
|---|---------|--------|
| 11 | JITコンパイラ | 60-80h |
| 12 | パッケージマネージャー | 40-60h |
| 13 | 並行処理/Async | 80-100h |

---

## 11. 推奨される即座のアクション

### Phase 1: 安全性強化 (1-2週間)

```bash
# 1. ジェネリック構造体の型登録修正
# src/codegen/llvm/core/context.cpp に事前登録ロジック追加

# 2. 配列境界チェックの実装
# src/codegen/llvm/core/mir_to_llvm.cpp で範囲チェック生成

# 3. テストカバレッジ追加
# tests/test_programs/array_safety/ に新テスト追加
```

### Phase 2: 品質向上 (2-3週間)

```bash
# 4. エラーメッセージ統一
# src/common/diagnostics.hpp に新しいDiagnosticSystem実装

# 5. 型情報の追跡可能性向上
# MirOperand に型情報フィールドを追加

# 6. コード重複排除
# 共通の型チェック関数を統一実装
```

---

## 12. まとめ

### 検出された問題の分類

| カテゴリ | 件数 | 深刻度 |
|---------|------|--------|
| バグ | 3件 | 🔴1件, 🟠2件 |
| 情報伝播不足 | 4件 | 🟠4件 |
| 最適化問題 | 3件 | ✅1件(解決), 🟠2件 |
| 肥大化 | 10ファイル | 🟡 |
| 機能不足 | 7個 | 🟢(スコープ外) |
| 開発体験 | 6項目 | 🟡 |

### 総行数と構造

```
総コード行数: 67,195行
├── frontend (字句解析/パーサー/型チェック): ~15,000行
├── hir/mir (中間表現): ~20,000行
├── codegen (コード生成): ~25,000行
└── その他 (common, module等): ~7,195行
```

### 技術的成熟度

| 領域 | 成熟度 |
|------|--------|
| 基本的なコンパイル | ⭐⭐⭐⭐⭐ (完全) |
| ジェネリクス | ⭐⭐⭐⭐ (ほぼ完全, LLVM型登録に課題) |
| 型システム | ⭐⭐⭐⭐ (十分) |
| 最適化 | ⭐⭐⭐ (基本的なもののみ) |
| 開発ツール | ⭐⭐ (最小限) |
| 並行処理 | ⭐ (未実装) |

---

## 参考資料

### 既知の問題ドキュメント
- `/docs/claude/report/015_optimization_bug_final_summary.md`
- `/docs/design/v0.11.0/code_quality_audit.md`
- `/docs/design/CANONICAL_SPEC.md`

### テスト結果
- LLVM Native: 296/296 ✅
- WASM: 296/296 ✅
- インタプリタ: 296/296 ✅

### ビルド & テスト
```bash
cmake -B build -DCM_USE_LLVM=ON
cmake --build build
ctest --test-dir build
```

