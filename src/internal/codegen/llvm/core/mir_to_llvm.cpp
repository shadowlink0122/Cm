/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器の共通ヘルパー（typedef解決・小型構造体判定）
/// プログラム/関数変換の本体はtranslate/配下に分離

#include "mir_to_llvm.hpp"

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

// TypeAlias（typedef）を基底型に再帰的に解決するヘルパー
// MemAddr → ulong, FsError → ulong のように typedef チェーンを展開する
hir::TypePtr MIRToLLVM::resolveTypeAlias(const hir::TypePtr& type) const {
    if (!type)
        return nullptr;
    auto current = type;
    // TypeAlias チェーンを辿って基底型まで解決
    while (current && current->kind == hir::TypeKind::TypeAlias) {
        if (current->element_type) {
            current = current->element_type;
        } else {
            // element_type が未設定の場合、typedefDefsで名前ベースの解決を試行
            auto it = typedefDefs.find(current->name);
            if (it != typedefDefs.end()) {
                current = it->second;
            } else {
                break;
            }
        }
    }
    // Struct kindだがtypedefの場合も解決（MIRでStruct名としてtypedef名が残る場合）
    if (current && current->kind == hir::TypeKind::Struct) {
        auto it = typedefDefs.find(current->name);
        if (it != typedefDefs.end()) {
            return resolveTypeAlias(it->second);
        }
    }
    return current;
}

// 構造体がABI上「小さい」かどうかをチェック（値渡し可能かどうか）
// System V ABI: 16バイト以下の構造体はレジスタで値渡し
bool MIRToLLVM::isSmallStruct(const hir::TypePtr& type) {
    if (!type || type->kind != hir::TypeKind::Struct) {
        return false;
    }

    // 構造体定義が未登録なら安全のためポインタ渡し
    if (structDefs.find(type->name) == structDefs.end()) {
        return false;
    }

    // サイズはLLVM DataLayoutの正確な値で判定する（C14 Phase 4）。
    // 従来の手計算switchはArray・ネスト構造体フィールドをdefault 8バイトと見積もるため、
    // int[16384]等の大配列フィールド構造体を「小」と誤判定して第一級集約の値渡しになり、
    // SROA全展開でO2/Ozのコンパイル時間が爆発していた（16バイト超はポインタ渡し+呼び出し先コピーへ）
    auto llvmType = convertType(type);
    if (!llvmType || !llvmType->isSized()) {
        return false;
    }
    return module->getDataLayout().getTypeAllocSize(llvmType) <= 16;
}

// 戻り値をsret（隠し出力ポインタ）で返すべき関数か（C14 Phase 4）。
// 大きな構造体の第一級集約returnはSROAが全要素展開しO2で二次爆発するため、
// 呼び出し元バッファへの直接書き込みに変換する。extern関数はFFIのABI互換のため、
// アドレス取得された関数は間接呼び出しのシグネチャ不一致を避けるため対象外
bool MIRToLLVM::needsSretReturn(const mir::MirFunction& func) {
    if (func.is_extern || func.name == "main") {
        return false;
    }
    if (func.return_local >= func.locals.size()) {
        return false;
    }
    const auto& ret_type = func.locals[func.return_local].type;
    if (!ret_type || ret_type->kind != hir::TypeKind::Struct) {
        return false;
    }
    // インターフェイス値はfat pointer（16バイト）でsret不要
    if (isInterfaceType(ret_type->name)) {
        return false;
    }
    // サイズ判定はLLVM DataLayoutの正確な値を使う
    // （isSmallStructはArrayフィールドをdefault 8バイトと見積もるため大配列フィールドを見逃す）
    auto llvmType = convertType(ret_type);
    if (!llvmType || !llvmType->isSized()) {
        return false;
    }
    auto size = module->getDataLayout().getTypeAllocSize(llvmType);
    if (size <= 16) {
        return false;
    }
    return addressTakenFunctions.count(func.name) == 0;
}

// プログラム全体からアドレス取得された関数を収集する。
// FunctionRefオペランドが「Call終端の呼び出し先」以外の位置（rvalueオペランド・呼び出し引数）に
// 現れる関数は、関数ポインタ・クロージャ・vtable経由で間接呼び出されうるためsret対象から除外する
void MIRToLLVM::collectAddressTakenFunctions(const std::vector<mir::MirFunctionPtr>& functions) {
    auto note_operand = [&](const mir::MirOperandPtr& op) {
        if (op && op->kind == mir::MirOperand::FunctionRef) {
            if (const auto* name = std::get_if<std::string>(&op->data)) {
                addressTakenFunctions.insert(*name);
            }
        }
    };
    for (const auto& func : functions) {
        if (!func) {
            continue;
        }
        for (const auto& block : func->basic_blocks) {
            if (!block) {
                continue;
            }
            for (const auto& stmt : block->statements) {
                if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                    continue;
                }
                const auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                if (!assign.rvalue) {
                    continue;
                }
                switch (assign.rvalue->kind) {
                    case mir::MirRvalue::Use:
                        note_operand(
                            std::get<mir::MirRvalue::UseData>(assign.rvalue->data).operand);
                        break;
                    case mir::MirRvalue::BinaryOp: {
                        const auto& bin =
                            std::get<mir::MirRvalue::BinaryOpData>(assign.rvalue->data);
                        note_operand(bin.lhs);
                        note_operand(bin.rhs);
                        break;
                    }
                    case mir::MirRvalue::UnaryOp:
                        note_operand(
                            std::get<mir::MirRvalue::UnaryOpData>(assign.rvalue->data).operand);
                        break;
                    case mir::MirRvalue::Cast:
                        note_operand(
                            std::get<mir::MirRvalue::CastData>(assign.rvalue->data).operand);
                        break;
                    case mir::MirRvalue::Aggregate:
                        for (const auto& op :
                             std::get<mir::MirRvalue::AggregateData>(assign.rvalue->data)
                                 .operands) {
                            note_operand(op);
                        }
                        break;
                    default:
                        break;
                }
            }
            if (block->terminator && block->terminator->kind == mir::MirTerminator::Call) {
                const auto& call = std::get<mir::MirTerminator::CallData>(block->terminator->data);
                // 呼び出し先（call.func）は対象外。引数として渡るFunctionRefのみ収集する
                for (const auto& arg : call.args) {
                    note_operand(arg);
                }
            }
        }
    }
}

}  // namespace cm::codegen::llvm_backend
