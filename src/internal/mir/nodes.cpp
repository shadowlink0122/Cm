// ============================================================
// MIRノードの実装
// ============================================================
// nodes.hpp で宣言された非テンプレートメンバ関数の実装。
// ヘッダーには宣言のみを置き、再コンパイル範囲を抑える。

#include "nodes.hpp"

#include <string>

namespace cm::mir {

// ============================================================
// BasicBlock
// ============================================================

void BasicBlock::update_successors() {
    successors.clear();
    if (!terminator)
        return;

    switch (terminator->kind) {
        case MirTerminator::Goto: {
            auto& data = std::get<MirTerminator::GotoData>(terminator->data);
            successors.push_back(data.target);
            break;
        }
        case MirTerminator::SwitchInt: {
            auto& data = std::get<MirTerminator::SwitchIntData>(terminator->data);
            for (const auto& [_, target] : data.targets) {
                successors.push_back(target);
            }
            successors.push_back(data.otherwise);
            break;
        }
        case MirTerminator::Call: {
            auto& data = std::get<MirTerminator::CallData>(terminator->data);
            successors.push_back(data.success);
            if (data.unwind) {
                successors.push_back(*data.unwind);
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================
// MirFunction
// ============================================================

void MirFunction::build_cfg() {
    // まずすべてのpredecessorをクリア
    for (auto& block : basic_blocks) {
        if (!block)
            continue;
        block->predecessors.clear();
        // terminatorの変更を反映させるためにsuccessorsも更新
        block->update_successors();
    }

    // successorからpredecessorを計算
    for (size_t i = 0; i < basic_blocks.size(); ++i) {
        if (!basic_blocks[i])
            continue;
        for (BlockId succ : basic_blocks[i]->successors) {
            if (auto* succ_block = get_block(succ)) {
                succ_block->predecessors.push_back(i);
            }
        }
    }
}

// ============================================================
// MirEnum
// ============================================================

uint32_t MirEnum::max_payload_size() const {
    uint32_t maxSize = 0;
    for (const auto& member : members) {
        uint32_t memberSize = 0;
        for (const auto& [name, type] : member.fields) {
            if (!type)
                continue;
            switch (type->kind) {
                case hir::TypeKind::Bool:
                case hir::TypeKind::Char:
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                    memberSize += 1;
                    break;
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                    memberSize += 2;
                    break;
                case hir::TypeKind::Int:
                case hir::TypeKind::UInt:
                case hir::TypeKind::Float:
                    memberSize += 4;
                    break;
                case hir::TypeKind::Long:
                case hir::TypeKind::ULong:
                case hir::TypeKind::Double:
                case hir::TypeKind::Pointer:
                case hir::TypeKind::String:
                    memberSize += 8;
                    break;
                default:
                    memberSize += 8;  // デフォルトはポインタサイズ
                    break;
            }
        }
        if (memberSize > maxSize) {
            maxSize = memberSize;
        }
    }
    return maxSize;
}

// ============================================================
// MirProgram
// ============================================================

const MirFunction* MirProgram::find_function(const std::string& name) const {
    for (const auto& func : functions) {
        if (func && func->name == name) {
            return func.get();
        }
    }
    return nullptr;
}

const MirFunction* MirProgram::find_function_qualified(const std::string& qualified_name) const {
    // モジュール修飾名を分割
    size_t pos = qualified_name.find("::");
    if (pos != std::string::npos) {
        std::string module = qualified_name.substr(0, pos);
        std::string func_name = qualified_name.substr(pos + 2);

        for (const auto& func : functions) {
            if (func && func->name == func_name && func->module_path == module) {
                return func.get();
            }
        }
    } else {
        // 修飾なしの場合は通常の検索
        return find_function(qualified_name);
    }
    return nullptr;
}

const MirStruct* MirProgram::find_struct(const std::string& name) const {
    for (const auto& st : structs) {
        if (st && st->name == name) {
            return st.get();
        }
    }
    return nullptr;
}

const VTable* MirProgram::find_vtable(const std::string& type_name,
                                      const std::string& interface_name) const {
    for (const auto& vt : vtables) {
        if (vt && vt->type_name == type_name && vt->interface_name == interface_name) {
            return vt.get();
        }
    }
    return nullptr;
}

}  // namespace cm::mir
