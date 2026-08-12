// ============================================================
// MIRノードの実装
// ============================================================
// nodes.hpp で宣言された非テンプレートメンバ関数の実装。
// ヘッダーには宣言のみを置き、再コンパイル範囲を抑える。

#include "nodes.hpp"

#include <string>

namespace cm::mir {

// ============================================================
// PlaceProjection
// ============================================================

PlaceProjection PlaceProjection::field(FieldId id) {
    PlaceProjection p;
    p.kind = ProjectionKind::Field;
    p.field_id = id;
    return p;
}

PlaceProjection PlaceProjection::index(LocalId local) {
    PlaceProjection p;
    p.kind = ProjectionKind::Index;
    p.index_local = local;
    return p;
}

PlaceProjection PlaceProjection::deref() {
    PlaceProjection p;
    p.kind = ProjectionKind::Deref;
    return p;
}

PlaceProjection PlaceProjection::field(FieldId id, hir::TypePtr result_type) {
    PlaceProjection p;
    p.kind = ProjectionKind::Field;
    p.field_id = id;
    p.result_type = result_type;
    return p;
}

PlaceProjection PlaceProjection::index(LocalId local, hir::TypePtr result_type) {
    PlaceProjection p;
    p.kind = ProjectionKind::Index;
    p.index_local = local;
    p.result_type = result_type;
    return p;
}

PlaceProjection PlaceProjection::deref(hir::TypePtr result_type, hir::TypePtr pointee_type) {
    PlaceProjection p;
    p.kind = ProjectionKind::Deref;
    p.result_type = result_type;
    p.pointee_type = pointee_type;
    return p;
}

// ============================================================
// MirPlace
// ============================================================

MirPlace::MirPlace(LocalId l, hir::TypePtr t) : local(l), type(t) {
    // ポインタ型の場合、pointee_typeを設定
    if (t && t->kind == hir::TypeKind::Pointer) {
        pointee_type = t->element_type;
    }
}

MirPlace::MirPlace(LocalId l, std::vector<PlaceProjection> p, hir::TypePtr t)
    : local(l), projections(std::move(p)), type(t) {
    // ポインタ型の場合、pointee_typeを設定
    if (t && t->kind == hir::TypeKind::Pointer) {
        pointee_type = t->element_type;
    }
}

// ============================================================
// MirOperand
// ============================================================

MirOperandPtr MirOperand::move(MirPlace place) {
    auto op = std::make_unique<MirOperand>();
    op->kind = Move;
    op->data = std::move(place);
    return op;
}

MirOperandPtr MirOperand::copy(MirPlace place) {
    auto op = std::make_unique<MirOperand>();
    op->kind = Copy;
    op->data = std::move(place);
    return op;
}

MirOperandPtr MirOperand::move(MirPlace place, hir::TypePtr type) {
    auto op = std::make_unique<MirOperand>();
    op->kind = Move;
    op->data = std::move(place);
    op->type = type;
    return op;
}

MirOperandPtr MirOperand::copy(MirPlace place, hir::TypePtr type) {
    auto op = std::make_unique<MirOperand>();
    op->kind = Copy;
    op->data = std::move(place);
    op->type = type;
    return op;
}

MirOperandPtr MirOperand::constant(MirConstant c) {
    auto op = std::make_unique<MirOperand>();
    op->kind = Constant;
    // Constantの場合、MirConstant自体に型情報があるので、それを使用。
    // move後の c.type は nullptr になるため、move前に取得する
    op->type = c.type;
    op->data = std::move(c);
    return op;
}

MirOperandPtr MirOperand::function_ref(std::string func_name, hir::TypePtr type) {
    auto op = std::make_unique<MirOperand>();
    op->kind = FunctionRef;
    op->data = std::move(func_name);
    op->type = type;
    return op;
}

// ============================================================
// MirRvalue
// ============================================================

MirRvaluePtr MirRvalue::use(MirOperandPtr op) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = Use;
    rv->data = UseData{std::move(op)};
    return rv;
}

MirRvaluePtr MirRvalue::binary(MirBinaryOp op, MirOperandPtr lhs, MirOperandPtr rhs,
                               hir::TypePtr result_type) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = BinaryOp;
    rv->data = BinaryOpData{op, std::move(lhs), std::move(rhs), std::move(result_type)};
    return rv;
}

MirRvaluePtr MirRvalue::unary(MirUnaryOp op, MirOperandPtr operand) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = UnaryOp;
    rv->data = UnaryOpData{op, std::move(operand)};
    return rv;
}

MirRvaluePtr MirRvalue::format_convert(MirOperandPtr op, const std::string& format_spec) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = FormatConvert;
    rv->data = FormatConvertData{std::move(op), format_spec};
    return rv;
}

MirRvaluePtr MirRvalue::ref(MirPlace place, bool is_mutable) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = Ref;
    rv->data = RefData{is_mutable ? BorrowKind::Mutable : BorrowKind::Shared, std::move(place)};
    return rv;
}

MirRvaluePtr MirRvalue::cast(MirOperandPtr operand, hir::TypePtr target_type, bool check_only) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = Cast;
    CastData data;
    data.operand = std::move(operand);
    data.target_type = target_type;
    data.check_only = check_only;
    rv->data = std::move(data);
    return rv;
}

MirRvaluePtr MirRvalue::iface_upcast(MirOperandPtr operand, hir::TypePtr iface_type,
                                     const std::string& concrete_name, bool from_pointer,
                                     bool boxed) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = Cast;
    CastData data;
    data.operand = std::move(operand);
    data.target_type = iface_type;
    data.iface_concrete = concrete_name;
    data.iface_from_pointer = from_pointer;
    data.iface_boxed = boxed;
    rv->data = std::move(data);
    return rv;
}

// ============================================================
// MirStatement
// ============================================================

MirStatementPtr MirStatement::assign(MirPlace place, MirRvaluePtr rvalue, Span s) {
    auto stmt = std::make_unique<MirStatement>();
    stmt->kind = Assign;
    stmt->span = s;
    stmt->data = AssignData{std::move(place), std::move(rvalue)};
    return stmt;
}

MirStatementPtr MirStatement::storage_live(LocalId local, Span s) {
    auto stmt = std::make_unique<MirStatement>();
    stmt->kind = StorageLive;
    stmt->span = s;
    stmt->data = StorageData{local};
    return stmt;
}

MirStatementPtr MirStatement::storage_dead(LocalId local, Span s) {
    auto stmt = std::make_unique<MirStatement>();
    stmt->kind = StorageDead;
    stmt->span = s;
    stmt->data = StorageData{local};
    return stmt;
}

MirStatementPtr MirStatement::asm_stmt(std::string code, bool is_must,
                                       std::vector<MirAsmOperand> operands,
                                       std::vector<std::string> clobbers, Span s) {
    auto stmt = std::make_unique<MirStatement>();
    stmt->kind = Asm;
    stmt->span = s;
    stmt->data = AsmData{std::move(code), is_must, std::move(clobbers), std::move(operands)};
    return stmt;
}

// ============================================================
// MirTerminator
// ============================================================

MirTerminatorPtr MirTerminator::goto_block(BlockId target, Span s) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = Goto;
    term->span = s;
    term->data = GotoData{target};
    return term;
}

MirTerminatorPtr MirTerminator::return_value(Span s) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = Return;
    term->span = s;
    return term;
}

MirTerminatorPtr MirTerminator::unreachable(Span s) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = Unreachable;
    term->span = s;
    return term;
}

MirTerminatorPtr MirTerminator::switch_int(MirOperandPtr discriminant,
                                           std::vector<std::pair<int64_t, BlockId>> targets,
                                           BlockId otherwise, Span s) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = SwitchInt;
    term->span = s;
    term->data = SwitchIntData{std::move(discriminant), std::move(targets), otherwise, {}};
    return term;
}

// ============================================================
// BasicBlock
// ============================================================

void BasicBlock::set_terminator(MirTerminatorPtr term) {
    terminator = std::move(term);
    update_successors();
}

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

LocalId MirFunction::add_local(std::string name, hir::TypePtr type, bool is_mutable, bool is_user,
                               bool is_static, bool is_global) {
    LocalId id = locals.size();
    locals.emplace_back(id, std::move(name), std::move(type), is_mutable, is_user, is_static,
                        is_global);
    return id;
}

BlockId MirFunction::add_block() {
    BlockId id = basic_blocks.size();
    basic_blocks.push_back(std::make_unique<BasicBlock>(id));
    return id;
}

BasicBlock* MirFunction::get_block(BlockId id) {
    if (id < basic_blocks.size()) {
        return basic_blocks[id].get();
    }
    return nullptr;
}

const BasicBlock* MirFunction::get_block(BlockId id) const {
    if (id < basic_blocks.size()) {
        return basic_blocks[id].get();
    }
    return nullptr;
}

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

bool MirEnum::is_tagged_union() const {
    for (const auto& m : members) {
        if (m.has_data())
            return true;
    }
    return false;
}

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
