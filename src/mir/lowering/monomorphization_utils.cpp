#include "monomorphization_utils.hpp"

#include "../../common/debug.hpp"

namespace cm::mir {

// ヘルパー関数：MirOperandのクローン
MirOperandPtr clone_operand(const MirOperandPtr& op) {
    if (!op)
        return nullptr;

    auto result = std::make_unique<MirOperand>();
    result->kind = op->kind;
    result->data = op->data;  // variant内の値をコピー（参照型がないのでOK）
    return result;
}

// ヘルパー関数：MirRvalueのクローン
MirRvaluePtr clone_rvalue(const MirRvaluePtr& rv) {
    if (!rv)
        return nullptr;

    auto result = std::make_unique<MirRvalue>();
    result->kind = rv->kind;

    switch (rv->kind) {
        case MirRvalue::Use: {
            auto& use_data = std::get<MirRvalue::UseData>(rv->data);
            result->data = MirRvalue::UseData{clone_operand(use_data.operand)};
            break;
        }
        case MirRvalue::BinaryOp: {
            auto& bin_data = std::get<MirRvalue::BinaryOpData>(rv->data);
            result->data =
                MirRvalue::BinaryOpData{bin_data.op, clone_operand(bin_data.lhs),
                                        clone_operand(bin_data.rhs), bin_data.result_type};
            break;
        }
        case MirRvalue::UnaryOp: {
            auto& un_data = std::get<MirRvalue::UnaryOpData>(rv->data);
            result->data = MirRvalue::UnaryOpData{un_data.op, clone_operand(un_data.operand)};
            break;
        }
        case MirRvalue::Ref: {
            auto& ref_data = std::get<MirRvalue::RefData>(rv->data);
            result->data = ref_data;
            break;
        }
        case MirRvalue::Aggregate: {
            auto& agg_data = std::get<MirRvalue::AggregateData>(rv->data);
            std::vector<MirOperandPtr> cloned_ops;
            for (const auto& op : agg_data.operands) {
                cloned_ops.push_back(clone_operand(op));
            }
            result->data = MirRvalue::AggregateData{agg_data.kind, std::move(cloned_ops)};
            break;
        }
        case MirRvalue::Cast: {
            auto& cast_data = std::get<MirRvalue::CastData>(rv->data);
            result->data =
                MirRvalue::CastData{clone_operand(cast_data.operand), cast_data.target_type};
            break;
        }
        case MirRvalue::FormatConvert: {
            auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rv->data);
            result->data =
                MirRvalue::FormatConvertData{clone_operand(fmt_data.operand), fmt_data.format_spec};
            break;
        }
    }

    return result;
}

// ヘルパー関数：MirStatementのクローン
MirStatementPtr clone_statement(const MirStatementPtr& stmt) {
    if (!stmt)
        return nullptr;

    auto result = std::make_unique<MirStatement>();
    result->kind = stmt->kind;
    result->span = stmt->span;

    switch (stmt->kind) {
        case MirStatement::Assign: {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
            result->data =
                MirStatement::AssignData{assign_data.place, clone_rvalue(assign_data.rvalue)};
            break;
        }
        case MirStatement::StorageLive:
        case MirStatement::StorageDead: {
            auto& storage_data = std::get<MirStatement::StorageData>(stmt->data);
            result->data = storage_data;
            break;
        }
        case MirStatement::Nop:
            result->data = std::monostate{};
            break;
    }

    return result;
}

// ヘルパー関数：型置換マップを使ってMirTerminatorをクローンし、メソッド呼び出しを書き換え
MirTerminatorPtr clone_terminator_with_subst(
    const MirTerminatorPtr& term,
    const std::unordered_map<std::string, std::string>& type_name_subst) {
    if (!term)
        return nullptr;

    auto result = std::make_unique<MirTerminator>();
    result->kind = term->kind;
    result->span = term->span;

    switch (term->kind) {
        case MirTerminator::Goto: {
            auto& goto_data = std::get<MirTerminator::GotoData>(term->data);
            result->data = goto_data;
            break;
        }
        case MirTerminator::SwitchInt: {
            auto& switch_data = std::get<MirTerminator::SwitchIntData>(term->data);
            result->data = MirTerminator::SwitchIntData{clone_operand(switch_data.discriminant),
                                                        switch_data.targets, switch_data.otherwise};
            break;
        }
        case MirTerminator::Call: {
            auto& call_data = std::get<MirTerminator::CallData>(term->data);
            std::vector<MirOperandPtr> cloned_args;
            for (const auto& arg : call_data.args) {
                cloned_args.push_back(clone_operand(arg));
            }

            // 関数名を書き換え（T__method -> ConcreteType__method）
            auto cloned_func = clone_operand(call_data.func);
            if (cloned_func && cloned_func->kind == MirOperand::FunctionRef) {
                auto& func_name = std::get<std::string>(cloned_func->data);
                // 関数名が "TypeParam__method" の形式かチェック
                for (const auto& [type_param, concrete_type] : type_name_subst) {
                    std::string prefix = type_param + "__";
                    if (func_name.find(prefix) == 0) {
                        // TypeParam__method -> ConcreteType__method
                        func_name = concrete_type + func_name.substr(type_param.length());
                        debug_msg("MONO",
                                  "Rewriting method call: " + type_param + "__* -> " + func_name);
                        break;
                    }
                }
            }

            result->data = MirTerminator::CallData{std::move(cloned_func), std::move(cloned_args),
                                                   call_data.destination,  call_data.success,
                                                   call_data.unwind,       call_data.interface_name,
                                                   call_data.method_name,  call_data.is_virtual};
            break;
        }
        case MirTerminator::Return:
        case MirTerminator::Unreachable:
            result->data = std::monostate{};
            break;
    }

    return result;
}

// 型から型名文字列を取得するヘルパー
std::string get_type_name(const hir::TypePtr& type) {
    if (!type)
        return "";

    // kindに基づいて名前を返す
    switch (type->kind) {
        case hir::TypeKind::Int:
            return "int";
        case hir::TypeKind::UInt:
            return "uint";
        case hir::TypeKind::Long:
            return "long";
        case hir::TypeKind::ULong:
            return "ulong";
        case hir::TypeKind::Short:
            return "short";
        case hir::TypeKind::UShort:
            return "ushort";
        case hir::TypeKind::Tiny:
            return "tiny";
        case hir::TypeKind::UTiny:
            return "utiny";
        case hir::TypeKind::Float:
            return "float";
        case hir::TypeKind::Double:
            return "double";
        case hir::TypeKind::Char:
            return "char";
        case hir::TypeKind::Bool:
            return "bool";
        case hir::TypeKind::String:
            return "string";
        case hir::TypeKind::Void:
            return "void";
        case hir::TypeKind::Pointer: {
            // ポインタ型: ptr_xxx 形式を返す
            std::string result = "ptr_";
            if (type->element_type) {
                result += get_type_name(type->element_type);
            } else {
                result += "void";
            }
            return result;
        }
        case hir::TypeKind::Struct:
        case hir::TypeKind::Interface:
        case hir::TypeKind::Generic:
            return type->name;
        default:
            return type->name.empty() ? "" : type->name;
    }
}

// 型名から正しいTypePtr型を作成するヘルパー
hir::TypePtr make_type_from_name(const std::string& name) {
    // プリミティブ型にはnameも設定（モノモーフィック化後にtype->nameが必要）
    if (name == "int") {
        auto t = hir::make_int();
        t->name = "int";
        return t;
    }
    if (name == "uint") {
        auto t = hir::make_uint();
        t->name = "uint";
        return t;
    }
    if (name == "long") {
        auto t = hir::make_long();
        t->name = "long";
        return t;
    }
    if (name == "ulong") {
        auto t = hir::make_ulong();
        t->name = "ulong";
        return t;
    }
    if (name == "short") {
        auto t = hir::make_short();
        t->name = "short";
        return t;
    }
    if (name == "ushort") {
        auto t = hir::make_ushort();
        t->name = "ushort";
        return t;
    }
    if (name == "tiny") {
        auto t = hir::make_tiny();
        t->name = "tiny";
        return t;
    }
    if (name == "utiny") {
        auto t = hir::make_utiny();
        t->name = "utiny";
        return t;
    }
    if (name == "float") {
        auto t = hir::make_float();
        t->name = "float";
        return t;
    }
    if (name == "double") {
        auto t = hir::make_double();
        t->name = "double";
        return t;
    }
    if (name == "char") {
        auto t = hir::make_char();
        t->name = "char";
        return t;
    }
    if (name == "bool") {
        auto t = hir::make_bool();
        t->name = "bool";
        return t;
    }
    if (name == "string") {
        auto t = hir::make_string();
        t->name = "string";
        return t;
    }
    if (name == "void") {
        auto t = hir::make_void();
        t->name = "void";
        return t;
    }
    // ポインタ型（ptr_xxx形式）
    if (name.size() > 4 && name.substr(0, 4) == "ptr_") {
        std::string elem_name = name.substr(4);
        auto elem_type = make_type_from_name(elem_name);
        auto t = hir::make_pointer(elem_type);
        t->name = name;
        return t;
    }
    // ユーザー定義型（構造体など）
    return hir::make_named(name);
}

}  // namespace cm::mir
