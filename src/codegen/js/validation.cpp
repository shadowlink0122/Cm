// JSターゲットのポインタ使用制限バリデーション
// malloc/free/void* の使用を検出してコンパイルエラーを発生させる

#include "codegen.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace cm::codegen::js {

using ast::TypeKind;

// JSで禁止されるメモリ管理関数
static const std::unordered_set<std::string> kProhibitedFunctions = {
    "malloc",          "free",           "calloc",   "realloc", "__cm_alloc", "__cm_free",
    "__cm_heap_alloc", "__cm_heap_free", "cm_alloc", "cm_free"};

// 関数名が禁止リストに含まれるか（修飾名の末尾部分もチェック）
static bool isProhibitedFunction(const std::string& name) {
    if (kProhibitedFunctions.count(name)) {
        return true;
    }
    // モジュール修飾名（例: std::mem::malloc）の末尾をチェック
    auto pos = name.rfind("::");
    if (pos != std::string::npos) {
        std::string basename = name.substr(pos + 2);
        if (kProhibitedFunctions.count(basename)) {
            return true;
        }
    }
    return false;
}

// void*型かどうかを判定
static bool isVoidPointer(const hir::TypePtr& type) {
    if (!type)
        return false;
    if (type->kind != TypeKind::Pointer)
        return false;
    if (!type->element_type)
        return false;
    return type->element_type->kind == TypeKind::Void;
}

// バリデーション結果
struct ValidationError {
    std::string message;
    std::string function_name;  // エラーが発生した関数名
};

// MIRプログラム全体をスキャンしてJS非対応のポインタ使用を検出
bool JSCodeGen::validatePointerUsage(const mir::MirProgram& program) {
    std::vector<ValidationError> errors;

    for (const auto& func : program.functions) {
        if (!func)
            continue;

        // 1. ローカル変数の型をチェック: void*の使用を検出
        for (size_t i = 0; i < func->locals.size(); ++i) {
            const auto& local = func->locals[i];
            if (isVoidPointer(local.type)) {
                errors.push_back(
                    {"JSターゲットでは void* 型は使用できません（変数: " + local.name + "）",
                     func->name});
            }
        }

        // 2. 各ブロックのターミネーターをチェック: malloc/free呼び出しの検出
        for (const auto& block : func->basic_blocks) {
            if (!block || !block->terminator)
                continue;

            if (block->terminator->kind == mir::MirTerminator::Call) {
                const auto& callData =
                    std::get<mir::MirTerminator::CallData>(block->terminator->data);

                // 関数名を取得
                if (callData.func && callData.func->kind == mir::MirOperand::FunctionRef) {
                    const auto& funcName = std::get<std::string>(callData.func->data);
                    if (isProhibitedFunction(funcName)) {
                        errors.push_back({"JSターゲットでは " + funcName +
                                              "() は使用できません。"
                                              "JavaScriptにはヒープメモリ管理機能がありません",
                                          func->name});
                    }
                }
            }
        }

        // 3. 各ステートメントのRvalueをチェック: void*キャストの検出
        for (const auto& block : func->basic_blocks) {
            if (!block)
                continue;
            for (const auto& stmt : block->statements) {
                if (!stmt)
                    continue;
                if (stmt->kind != mir::MirStatement::Assign)
                    continue;

                const auto& assignData = std::get<mir::MirStatement::AssignData>(stmt->data);
                if (!assignData.rvalue)
                    continue;

                // Castの場合、ターゲット型がvoid*かチェック
                if (assignData.rvalue->kind == mir::MirRvalue::Cast) {
                    const auto& castData =
                        std::get<mir::MirRvalue::CastData>(assignData.rvalue->data);
                    if (isVoidPointer(castData.target_type)) {
                        errors.push_back(
                            {"JSターゲットでは void* へのキャストは使用できません", func->name});
                    }
                    // void*からのキャスト（as T*）もチェック
                    if (castData.target_type && castData.target_type->kind == TypeKind::Pointer &&
                        castData.operand && isVoidPointer(castData.operand->type)) {
                        errors.push_back(
                            {"JSターゲットでは void* からのポインタキャストは使用できません",
                             func->name});
                    }
                }
            }
        }
    }

    // エラーを出力
    if (!errors.empty()) {
        for (const auto& err : errors) {
            std::cerr << "エラー[JS]: " << err.message;
            if (!err.function_name.empty()) {
                std::cerr << " (関数: " << err.function_name << ")";
            }
            std::cerr << std::endl;
        }
        // 最初のエラーだけでコンパイル中止（重複エラーを避ける）
        std::cerr << "ヒント: malloc/free/void* はJSターゲットでは利用できません。"
                  << "配列とスライスを使用してください。" << std::endl;
        return false;
    }

    return true;
}

}  // namespace cm::codegen::js
