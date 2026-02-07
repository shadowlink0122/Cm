# Cm言語 - リファクタリング実装ガイド

作成日: 2026-01-11
対象版: v0.11.0
目的: 検出されたリファクタリング項目の実装手順と詳細

---

## クイックスタート

### 優先順位が高い順に実装

1. **ジェネリック構造体のLLVM型登録修正** (4-6h)
2. **MIR型情報伝播の強化** (6-8h)
3. **配列境界チェックのLLVM実装** (3-4h)

合計工数: 13-18時間

---

## 1. ジェネリック構造体のLLVM型登録修正

### 1.1 問題の詳細

```cm
// 問題のある例
struct Node<T> {
    T value;
    Node<T>* next;
}

void test() {
    Node<int> node;  // <- LLVM型 "Node__int" の登録が必要
}
```

**発生箇所:**
```
LLVM IR生成時に "Invalid GetElementPtrInst" アサーション失敗
```

### 1.2 実装手順

#### Step 1: LLVM構造体型の事前登録メカニズム追加

**ファイル**: `/src/codegen/llvm/core/context.hpp`

```cpp
class LlvmContext {
private:
    // 既存のフィールド
    std::map<std::string, llvm::StructType*> struct_types;
    
    // 以下を追加
    std::set<std::string> registered_types;  // 登録済み型のトラッキング
    
public:
    // 新規メソッド
    void pre_register_monomorphized_types(
        const std::map<std::string, hir::HirStruct*>& mono_structs);
    
    bool is_struct_type_registered(const std::string& name) const;
    
    void register_missing_struct_types(
        const MirProgram& mir_program);
};
```

**ファイル**: `/src/codegen/llvm/core/context.cpp`

```cpp
void LlvmContext::pre_register_monomorphized_types(
    const std::map<std::string, hir::HirStruct*>& mono_structs) {
    
    for (auto& [mono_name, hir_struct] : mono_structs) {
        // 既に登録されているかチェック
        if (registered_types.count(mono_name)) {
            continue;
        }
        
        // LLVM構造体型を作成
        std::vector<llvm::Type*> field_types;
        for (auto& field : hir_struct->fields) {
            auto llvm_type = get_llvm_type(field->type);
            if (!llvm_type) {
                error("Cannot get LLVM type for field: " + field->name);
                continue;
            }
            field_types.push_back(llvm_type);
        }
        
        // 構造体型を作成・登録
        auto struct_type = llvm::StructType::create(
            context, 
            field_types, 
            mono_name
        );
        
        struct_types[mono_name] = struct_type;
        registered_types.insert(mono_name);
        
        debug_msg("LLVM", "Pre-registered struct type: " + mono_name);
    }
}

void LlvmContext::register_missing_struct_types(
    const MirProgram& mir_program) {
    
    // MIRプログラムをスキャンして、使用されているすべての構造体型を登録
    for (auto& func : mir_program.functions) {
        if (!func) continue;
        
        for (auto& block : func->basic_blocks) {
            if (!block) continue;
            
            for (auto& stmt : block->statements) {
                // ここでStruct型の使用をスキャン
                scan_types_in_statement(stmt);
            }
        }
    }
}
```

#### Step 2: MIRローワリング時にモノモーフィゼーション情報を伝播

**ファイル**: `/src/mir/lowering/lowering.hpp`

```cpp
class MirLowering : public MirLoweringBase {
private:
    // 既存フィールド
    // ...
    
    // 以下を追加: モノモーフィゼーション情報の記録
    std::map<std::string, hir::HirStruct*> monomorphized_structs;

public:
    // lower() メソッドの後に以下を追加
    void collect_monomorphized_struct_info() {
        // Monomorphization クラスから情報を取得
        for (auto& [spec_name, hir_struct] : monomorphizer.get_specialized_structs()) {
            monomorphized_structs[spec_name] = hir_struct;
        }
    }
    
    const std::map<std::string, hir::HirStruct*>& 
    get_monomorphized_structs() const {
        return monomorphized_structs;
    }
};
```

**ファイル**: `/src/mir/lowering/lowering.hpp` の lower() メソッド

```cpp
MirProgram lower(const hir::HirProgram& hir_program) {
    // ... 既存の処理 ...
    
    // Pass 4: モノモーフィゼーション実行
    perform_monomorphization();
    
    // Pass 4.5: NEW - モノモーフィゼーション情報を収集
    collect_monomorphized_struct_info();
    
    // ... 以降の処理 ...
    
    return std::move(mir_program);
}
```

#### Step 3: LLVM CodeGen時に型を事前登録

**ファイル**: `/src/codegen/llvm/native/codegen.hpp`

```cpp
class NativeCodeGenerator {
private:
    // 既存フィールド
    // ...

public:
    // generate_llvm メソッドを修正
    std::string generate_llvm(
        const MirProgram& mir_program,
        const std::map<std::string, hir::HirStruct*>& mono_structs) {
        
        // Step 1: コンテキストを初期化
        auto context = LlvmContext();
        
        // Step 2: モノモーフィゼーション情報から構造体型を事前登録
        context.pre_register_monomorphized_types(mono_structs);
        
        // Step 3: 通常のコード生成処理
        // ... 既存の処理 ...
        
        return llvm_ir_code;
    }
};
```

#### Step 4: GetElementPtr生成時の安全性チェック

**ファイル**: `/src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
// 既存の GetElementPtr 生成コードを修正

llvm::Value* generate_gep(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    const std::string& struct_type_name,
    size_t field_index,
    LlvmContext& ctx) {
    
    // 構造体型が登録されているか確認
    if (!ctx.is_struct_type_registered(struct_type_name)) {
        error("LLVM struct type not registered: " + struct_type_name + 
              " (Field index: " + std::to_string(field_index) + ")");
        return nullptr;  // エラーハンドリング
    }
    
    auto struct_type = ctx.get_struct_type(struct_type_name);
    
    // GetElementPtr を生成
    auto gep = builder.CreateStructGEP(
        struct_type,
        base,
        field_index
    );
    
    return gep;
}
```

### 1.3 テスト

**テストファイル**: `/tests/test_programs/generics/generic_struct_llvm_safe.cm`

```cm
struct Node<T> {
    T value;
    Node<T>* next;
}

struct Queue<Item> {
    Node<Item>* head;
    Node<Item>* tail;
}

int main() {
    Queue<int> q;
    q.head = null;
    q.tail = null;
    
    println("Queue<int> successfully created");
    return 0;
}
```

**期待される出力:**
```
Queue<int> successfully created
```

---

## 2. MIR型情報伝播の強化

### 2.1 問題の詳細

```cm
<T> void print_value(Container<T> c) {
    println("{}", c.value);  // T の型情報が MIR で失われている
}
```

### 2.2 実装手順

#### Step 1: MirOperand に型情報フィールドを追加

**ファイル**: `/src/mir/nodes.hpp`

```cpp
struct MirOperand {
    // 既存のフィールド
    Kind kind;
    std::variant<...> data;
    SourceLocation location;
    
    // 以下を追加
    ast::TypePtr type;  // 型情報を明示的に保持
    bool type_inferred = false;  // 型が推論された場合のフラグ
    
    // ヘルパーメソッド
    bool has_valid_type() const {
        return type && type->kind != TypeKind::Error;
    }
    
    std::string type_to_string() const {
        if (!type) return "null_type";
        return type->to_string();
    }
};
```

#### Step 2: ExprLowering で型情報を伝播

**ファイル**: `/src/mir/lowering/expr.hpp`

```cpp
class ExprLowering {
private:
    // 型情報コンテキスト
    std::map<std::string, ast::TypePtr> type_context;
    
public:
    // lower_expr メソッドを強化
    MirOperand lower_expr(
        const hir::HirExpr& expr,
        const std::map<std::string, ast::TypePtr>& generic_types) {
        
        MirOperand result;
        
        // ... 既存の式lowering処理 ...
        
        // 型情報を設定
        result.type = expr.type;  // HIRから型を取得
        
        // ジェネリック型の場合は置換
        if (result.type && is_generic_type(result.type)) {
            result.type = substitute_generic_type(
                result.type, 
                generic_types
            );
            result.type_inferred = true;
        }
        
        return result;
    }
    
private:
    ast::TypePtr substitute_generic_type(
        ast::TypePtr type,
        const std::map<std::string, ast::TypePtr>& mapping) {
        
        if (!type) return type;
        
        // T -> int のような置換
        if (type->kind == TypeKind::Generic) {
            auto name = type->name;
            auto it = mapping.find(name);
            if (it != mapping.end()) {
                return it->second;
            }
        }
        
        // Container<T> -> Container<int> のような置換
        if (type->kind == TypeKind::Struct && type->generic_args) {
            auto new_args = type->generic_args;
            for (auto& arg : new_args) {
                arg = substitute_generic_type(arg, mapping);
            }
            return make_generic_type(type->name, new_args);
        }
        
        return type;
    }
};
```

#### Step 3: FieldAccess時の型推論を強化

**ファイル**: `/src/mir/lowering/expr_basic.cpp`

```cpp
// lower_field_access を修正

MirExpr lower_field_access(
    const hir::FieldAccessExpr& expr,
    const std::map<std::string, ast::TypePtr>& generic_types) {
    
    MirExpr result;
    
    // オブジェクトの型を取得
    auto obj_type = expr.object->type;
    
    // ジェネリック型の場合は置換
    if (is_generic_struct(obj_type)) {
        obj_type = substitute_generic_type(obj_type, generic_types);
    }
    
    // フィールドの定義を探す
    auto field = find_field_in_type(obj_type, expr.field_name);
    
    if (field) {
        // フィールドの型を取得し、ジェネリック置換を適用
        auto field_type = field->type;
        
        if (is_generic_type(field_type)) {
            field_type = substitute_generic_type(
                field_type, 
                generic_types
            );
        }
        
        // MIR式に型情報を設定
        result.type = field_type;
        result.type_inferred = true;
    }
    
    return result;
}
```

#### Step 4: デバッグ出力を追加

**ファイル**: `/src/mir/lowering/lowering.hpp`

```cpp
void lower_functions(const hir::HirProgram& hir_program) {
    for (auto& hir_func : hir_program.functions) {
        if (!hir_func) continue;
        
        auto mir_func = lower_function(*hir_func);
        
        // デバッグ出力
        if (cm::debug::g_debug_mode) {
            debug_msg("MIR_TYPES", 
                "Function: " + hir_func->name);
            
            for (auto& block : mir_func->basic_blocks) {
                for (auto& stmt : block->statements) {
                    if (stmt.kind == MirStatement::Assign) {
                        auto& assign = std::get<MirStatement::AssignData>(stmt.data);
                        if (assign.rvalue && assign.rvalue->type) {
                            debug_msg("MIR_TYPES",
                                "  Var " + assign.local_name + 
                                " has type " + assign.rvalue->type->to_string());
                        }
                    }
                }
            }
        }
    }
}
```

### 2.3 テスト

**テストファイル**: `/tests/test_programs/generics/generic_type_propagation.cm`

```cm
struct Container<T> {
    T value;
}

<T> void print_container(Container<T> c) {
    println("Container type: {}", c.value);
}

int main() {
    Container<int> ci;
    ci.value = 42;
    print_container(ci);
    
    Container<double> cd;
    cd.value = 3.14;
    print_container(cd);
    
    return 0;
}
```

---

## 3. 配列境界チェックのLLVM実装

### 3.1 問題の詳細

```cm
int[10] arr;
arr[20] = 100;  // 範囲外アクセス -> 未定義動作（修正すべき）
```

### 3.2 実装手順

#### Step 1: 配列情報を MIR に追加

**ファイル**: `/src/mir/nodes.hpp`

```cpp
struct MirArrayInfo {
    size_t element_size;
    size_t array_size;
    ast::TypePtr element_type;
    
    bool is_valid() const {
        return element_size > 0 && array_size > 0 && element_type;
    }
};

struct MirOperand {
    // 既存フィールド
    // ...
    
    // 以下を追加
    MirArrayInfo array_info;  // 配列の場合に設定
};
```

#### Step 2: LLVM IR生成時に境界チェックを追加

**ファイル**: `/src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
llvm::Value* generate_array_access_with_bounds_check(
    llvm::IRBuilder<>& builder,
    llvm::Value* array_ptr,
    llvm::Value* index,
    size_t array_size,
    llvm::Function* func,
    LlvmContext& ctx) {
    
    // index が負でないことをチェック
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.context), 0);
    auto is_non_negative = builder.CreateICmpSGE(index, zero);
    
    // index が配列サイズより小さいことをチェック
    auto size = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(ctx.context), 
        array_size
    );
    auto is_in_bounds = builder.CreateICmpSLT(index, size);
    
    // 両方の条件を AND
    auto bounds_ok = builder.CreateAnd(is_non_negative, is_in_bounds);
    
    // 分岐: bounds_ok なら continue_bb へ、そうでなら panic_bb へ
    auto panic_bb = llvm::BasicBlock::Create(
        ctx.context, 
        "array_bounds_panic", 
        func
    );
    auto continue_bb = llvm::BasicBlock::Create(
        ctx.context, 
        "array_continue", 
        func
    );
    
    builder.CreateCondBr(bounds_ok, continue_bb, panic_bb);
    
    // panic_bb: パニック関数を呼び出し
    builder.SetInsertPoint(panic_bb);
    auto panic_func = ctx.get_or_create_panic_func();
    auto msg = builder.CreateGlobalStringPtr(
        "array index out of bounds"
    );
    builder.CreateCall(panic_func, {index, size, msg});
    builder.CreateUnreachable();
    
    // continue_bb: 実際のアクセスを行う
    builder.SetInsertPoint(continue_bb);
    auto gep = builder.CreateInBoundsGEP(
        llvm::Type::getInt32Ty(ctx.context),
        array_ptr,
        {index}
    );
    
    return gep;
}
```

#### Step 3: パニック関数を定義

**ファイル**: `/src/codegen/llvm/core/intrinsics.hpp`

```cpp
llvm::Function* create_panic_function(llvm::Module* module) {
    auto& context = module->getContext();
    
    // 関数シグネチャ: void panic(int64 index, int64 size, string msg)
    auto panic_func = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {
                llvm::Type::getInt64Ty(context),   // index
                llvm::Type::getInt64Ty(context),   // size
                llvm::Type::getInt8PtrTy(context)  // message
            },
            false
        ),
        llvm::Function::ExternalLinkage,
        "cm_array_panic",
        module
    );
    
    // 属性: noreturn
    panic_func->addFnAttr(llvm::Attribute::NoReturn);
    
    return panic_func;
}
```

#### Step 4: ランタイム実装を追加

**ファイル**: `/src/codegen/llvm/native/runtime_safety.c` (新規作成)

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void cm_array_panic(int64_t index, int64_t size, const char* msg) {
    fprintf(stderr, "fatal: array panic: %s\n", msg);
    fprintf(stderr, "  index: %lld\n", (long long)index);
    fprintf(stderr, "  size: %lld\n", (long long)size);
    exit(1);
}
```

### 3.3 テスト

**テストファイル**: `/tests/test_programs/array/array_bounds_check.cm`

```cm
int main() {
    int[5] arr;
    
    // 正常なアクセス
    arr[0] = 10;
    arr[4] = 50;
    
    println("Normal access OK");
    
    // 範囲外アクセス（パニック）
    // arr[10] = 100;  // <- Should panic
    
    return 0;
}
```

---

## 4. エラーメッセージ品質向上

### 4.1 統一されたDiagnosticシステム

**ファイル**: `/src/common/diagnostics.hpp` (新規作成)

```cpp
#pragma once

#include <string>
#include <vector>
#include "source_location.hpp"

namespace cm {

enum class DiagnosticKind {
    Error,
    Warning,
    Note,
    Help
};

class Diagnostic {
public:
    DiagnosticKind kind;
    std::string code;           // "E0001", "W0042" など
    std::string message;        // メインメッセージ
    SourceLocation location;    // 発生位置
    
    std::vector<std::pair<SourceLocation, std::string>> notes;  // 関連情報
    std::string suggestion;     // 修正提案
    
    // 等々...
};

class DiagnosticCollector {
private:
    std::vector<Diagnostic> diagnostics;
    
public:
    void error(const std::string& msg, const SourceLocation& loc) {
        diagnostics.push_back({
            DiagnosticKind::Error,
            generate_error_code(),
            msg,
            loc
        });
    }
    
    void print_all() const {
        for (auto& d : diagnostics) {
            print_diagnostic(d);
        }
    }
    
private:
    static std::string generate_error_code();
    static void print_diagnostic(const Diagnostic& d);
};

}  // namespace cm
```

---

## 5. 巨大ファイルの分割

### 5.1 expr_call.cpp の分割

**分割案:**

```
expr_call.cpp (2237行) を以下に分割：

expr_call.cpp (500行)
├── 定義、共通util、メインloweerer関数

expr_call_builtin.cpp (400行)
├── println, print, 組み込み関数

expr_call_method.cpp (500行)
├── メソッド呼び出しの処理

expr_call_generic.cpp (400行)
├── ジェネリック関数呼び出し

expr_call_interface.cpp (350行)
├── インターフェース実装呼び出し
```

**具体的な実装:**

1. `/src/mir/lowering/expr_call_builtin.cpp` を作成
2. `/src/mir/lowering/expr_call_method.cpp` を作成
3. 既存の `expr_call.cpp` をリファクタリング
4. ヘッダファイルで宣言を統一

---

## 6. 実装チェックリスト

### ジェネリック構造体のLLVM型登録
- [ ] `context.hpp` に新規メソッド追加
- [ ] `context.cpp` に実装追加
- [ ] `lowering.hpp` に型情報収集ロジック追加
- [ ] `mir_to_llvm.cpp` の GetElementPtr 生成修正
- [ ] テスト追加
- [ ] デバッグ出力確認

### MIR型情報伝播
- [ ] `MirOperand` に型情報フィールド追加
- [ ] `ExprLowering` で型置換実装
- [ ] FieldAccess 処理強化
- [ ] デバッグ出力追加
- [ ] テスト追加

### 配列境界チェック
- [ ] `MirArrayInfo` 構造体追加
- [ ] LLVM IR生成時の界チェック実装
- [ ] パニック関数定義
- [ ] ランタイム実装 (`runtime_safety.c`)
- [ ] テスト追加

### エラーメッセージ品質
- [ ] `diagnostics.hpp` 作成
- [ ] `DiagnosticCollector` 実装
- [ ] 既存エラー出力をDiagnosticに置き換え

### 巨大ファイル分割
- [ ] 新規 `.cpp` ファイル作成
- [ ] 関数を新規ファイルに移動
- [ ] ヘッダファイル整理
- [ ] インクルード依存関係の確認
- [ ] コンパイル確認

---

## 7. テスト戦略

### 単体テスト

```bash
# 各機能ごとのテスト
make test_generics          # ジェネリクス関連
make test_array_safety      # 配列安全性
make test_type_propagation  # 型伝播
```

### 回帰テスト

```bash
# 全テスト実行
make tlp   # LLVM Native
make twp   # WASM
make tip   # インタプリタ
```

### パフォーマンステスト

```bash
# コンパイル時間の測定
time cmake --build build
```

---

## 8. 推奨スケジュール

### Week 1
- Day 1-2: ジェネリック構造体のLLVM型登録
- Day 3-4: MIR型情報伝播
- Day 5: テスト、統合テスト

### Week 2
- Day 1-2: 配列境界チェック実装
- Day 3: エラーメッセージシステム
- Day 4-5: テスト、バグ修正

### Week 3
- Day 1-2: 巨大ファイル分割
- Day 3-4: リファクタリング、整理
- Day 5: 総合テスト、ドキュメント

---

## 参考資料

- `docs/refactoring_audit_comprehensive_20260111.md` - 全体監査レポート
- `docs/design/CANONICAL_SPEC.md` - 言語仕様
- `docs/design/v0.11.0/code_quality_audit.md` - コード品質監査
- `ROADMAP.md` - 全体ロードマップ

