// lowering.cpp - メインエントリポイントとヘルパー関数
#include "lowering_fwd.hpp"

namespace cm::hir {

// メインエントリポイント
HirProgram HirLowering::lower(ast::Program& program) {
    debug::hir::log(debug::hir::Id::LowerStart);

    HirProgram hir;
    hir.filename = program.filename;

    // Pass 1: 構造体定義、enum定義、関数定義、コンストラクタ情報を収集
    for (auto& decl : program.declarations) {
        if (auto* st = decl->as<ast::StructDecl>()) {
            struct_defs_[st->name] = st;
        } else if (auto* func = decl->as<ast::FunctionDecl>()) {
            func_defs_[func->name] = func;
        } else if (auto* en = decl->as<ast::EnumDecl>()) {
            for (const auto& member : en->members) {
                if (member.value.has_value()) {
                    enum_values_[en->name + "::" + member.name] = *member.value;
                }
            }
        } else if (auto* impl = decl->as<ast::ImplDecl>()) {
            if (impl->is_ctor_impl && impl->target_type) {
                std::string type_name = type_to_string(*impl->target_type);
                for (auto& ctor : impl->constructors) {
                    if (ctor->params.empty()) {
                        types_with_default_ctor_.insert(type_name);
                    }
                }
            }
        }
    }

    // Pass 2: 宣言を変換
    for (auto& decl : program.declarations) {
        if (auto* mod = decl->as<ast::ModuleDecl>()) {
            process_namespace(*mod, "", hir);
        } else if (auto hir_decl = lower_decl(*decl)) {
            hir.declarations.push_back(std::move(hir_decl));
        }
    }

    debug::hir::log(debug::hir::Id::LowerEnd,
                    std::to_string(hir.declarations.size()) + " declarations");
    return hir;
}

// ネストしたnamespaceを再帰的に処理
void HirLowering::process_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace,
                                    HirProgram& hir) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::hir::log(debug::hir::Id::NodeCreate, "processing namespace " + full_namespace,
                    debug::Level::Debug);

    for (auto& inner_decl : mod.declarations) {
        if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
            process_namespace(*nested_mod, full_namespace, hir);
        } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
            std::string original_name = func->name;
            func->name = full_namespace + "::" + original_name;
            if (auto hir_decl = lower_function(*func)) {
                hir.declarations.push_back(std::move(hir_decl));
            }
            func->name = original_name;
        } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
            std::string original_name = st->name;
            st->name = full_namespace + "::" + original_name;
            if (auto hir_decl = lower_struct(*st)) {
                hir.declarations.push_back(std::move(hir_decl));
            }
            st->name = original_name;
        } else {
            if (auto hir_decl = lower_decl(*inner_decl)) {
                hir.declarations.push_back(std::move(hir_decl));
            }
        }
    }
}

// 構造体のdefaultメンバ名を取得
std::string HirLowering::get_default_member_name(const std::string& struct_name) const {
    auto it = struct_defs_.find(struct_name);
    if (it == struct_defs_.end())
        return "";
    for (const auto& field : it->second->fields) {
        if (field.is_default) {
            return field.name;
        }
    }
    return "";
}

// 演算子変換ヘルパー
HirOperatorKind HirLowering::convert_operator_kind(ast::OperatorKind kind) {
    switch (kind) {
        case ast::OperatorKind::Eq:
            return HirOperatorKind::Eq;
        case ast::OperatorKind::Ne:
            return HirOperatorKind::Ne;
        case ast::OperatorKind::Lt:
            return HirOperatorKind::Lt;
        case ast::OperatorKind::Gt:
            return HirOperatorKind::Gt;
        case ast::OperatorKind::Le:
            return HirOperatorKind::Le;
        case ast::OperatorKind::Ge:
            return HirOperatorKind::Ge;
        case ast::OperatorKind::Add:
            return HirOperatorKind::Add;
        case ast::OperatorKind::Sub:
            return HirOperatorKind::Sub;
        case ast::OperatorKind::Mul:
            return HirOperatorKind::Mul;
        case ast::OperatorKind::Div:
            return HirOperatorKind::Div;
        case ast::OperatorKind::Mod:
            return HirOperatorKind::Mod;
        case ast::OperatorKind::BitAnd:
            return HirOperatorKind::BitAnd;
        case ast::OperatorKind::BitOr:
            return HirOperatorKind::BitOr;
        case ast::OperatorKind::BitXor:
            return HirOperatorKind::BitXor;
        case ast::OperatorKind::Shl:
            return HirOperatorKind::Shl;
        case ast::OperatorKind::Shr:
            return HirOperatorKind::Shr;
        case ast::OperatorKind::Neg:
            return HirOperatorKind::Neg;
        case ast::OperatorKind::Not:
            return HirOperatorKind::Not;
        case ast::OperatorKind::BitNot:
            return HirOperatorKind::BitNot;
        default:
            return HirOperatorKind::Eq;
    }
}

bool HirLowering::is_compound_assign(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::AddAssign:
        case ast::BinaryOp::SubAssign:
        case ast::BinaryOp::MulAssign:
        case ast::BinaryOp::DivAssign:
        case ast::BinaryOp::ModAssign:
        case ast::BinaryOp::BitAndAssign:
        case ast::BinaryOp::BitOrAssign:
        case ast::BinaryOp::BitXorAssign:
        case ast::BinaryOp::ShlAssign:
        case ast::BinaryOp::ShrAssign:
            return true;
        default:
            return false;
    }
}

HirBinaryOp HirLowering::get_base_op(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::AddAssign:
            return HirBinaryOp::Add;
        case ast::BinaryOp::SubAssign:
            return HirBinaryOp::Sub;
        case ast::BinaryOp::MulAssign:
            return HirBinaryOp::Mul;
        case ast::BinaryOp::DivAssign:
            return HirBinaryOp::Div;
        case ast::BinaryOp::ModAssign:
            return HirBinaryOp::Mod;
        case ast::BinaryOp::BitAndAssign:
            return HirBinaryOp::BitAnd;
        case ast::BinaryOp::BitOrAssign:
            return HirBinaryOp::BitOr;
        case ast::BinaryOp::BitXorAssign:
            return HirBinaryOp::BitXor;
        case ast::BinaryOp::ShlAssign:
            return HirBinaryOp::Shl;
        case ast::BinaryOp::ShrAssign:
            return HirBinaryOp::Shr;
        default:
            return HirBinaryOp::Add;
    }
}

HirBinaryOp HirLowering::convert_binary_op(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::Add:
            return HirBinaryOp::Add;
        case ast::BinaryOp::Sub:
            return HirBinaryOp::Sub;
        case ast::BinaryOp::Mul:
            return HirBinaryOp::Mul;
        case ast::BinaryOp::Div:
            return HirBinaryOp::Div;
        case ast::BinaryOp::Mod:
            return HirBinaryOp::Mod;
        case ast::BinaryOp::BitAnd:
            return HirBinaryOp::BitAnd;
        case ast::BinaryOp::BitOr:
            return HirBinaryOp::BitOr;
        case ast::BinaryOp::BitXor:
            return HirBinaryOp::BitXor;
        case ast::BinaryOp::Shl:
            return HirBinaryOp::Shl;
        case ast::BinaryOp::Shr:
            return HirBinaryOp::Shr;
        case ast::BinaryOp::And:
            return HirBinaryOp::And;
        case ast::BinaryOp::Or:
            return HirBinaryOp::Or;
        case ast::BinaryOp::Eq:
            return HirBinaryOp::Eq;
        case ast::BinaryOp::Ne:
            return HirBinaryOp::Ne;
        case ast::BinaryOp::Lt:
            return HirBinaryOp::Lt;
        case ast::BinaryOp::Gt:
            return HirBinaryOp::Gt;
        case ast::BinaryOp::Le:
            return HirBinaryOp::Le;
        case ast::BinaryOp::Ge:
            return HirBinaryOp::Ge;
        case ast::BinaryOp::Assign:
            return HirBinaryOp::Assign;
        default:
            return HirBinaryOp::Add;
    }
}

HirUnaryOp HirLowering::convert_unary_op(ast::UnaryOp op) {
    switch (op) {
        case ast::UnaryOp::Neg:
            return HirUnaryOp::Neg;
        case ast::UnaryOp::Not:
            return HirUnaryOp::Not;
        case ast::UnaryOp::BitNot:
            return HirUnaryOp::BitNot;
        case ast::UnaryOp::Deref:
            return HirUnaryOp::Deref;
        case ast::UnaryOp::AddrOf:
            return HirUnaryOp::AddrOf;
        case ast::UnaryOp::PreInc:
            return HirUnaryOp::PreInc;
        case ast::UnaryOp::PreDec:
            return HirUnaryOp::PreDec;
        case ast::UnaryOp::PostInc:
            return HirUnaryOp::PostInc;
        case ast::UnaryOp::PostDec:
            return HirUnaryOp::PostDec;
        default:
            return HirUnaryOp::Neg;
    }
}

bool HirLowering::is_comparison_op(ast::BinaryOp op) {
    return op == ast::BinaryOp::Eq || op == ast::BinaryOp::Ne || op == ast::BinaryOp::Lt ||
           op == ast::BinaryOp::Gt || op == ast::BinaryOp::Le || op == ast::BinaryOp::Ge;
}

std::string HirLowering::hir_binary_op_to_string(HirBinaryOp op) {
    switch (op) {
        case HirBinaryOp::Add:
            return "Add";
        case HirBinaryOp::Sub:
            return "Sub";
        case HirBinaryOp::Mul:
            return "Mul";
        case HirBinaryOp::Div:
            return "Div";
        case HirBinaryOp::Mod:
            return "Mod";
        case HirBinaryOp::BitAnd:
            return "BitAnd";
        case HirBinaryOp::BitOr:
            return "BitOr";
        case HirBinaryOp::BitXor:
            return "BitXor";
        case HirBinaryOp::Shl:
            return "Shl";
        case HirBinaryOp::Shr:
            return "Shr";
        case HirBinaryOp::And:
            return "And";
        case HirBinaryOp::Or:
            return "Or";
        case HirBinaryOp::Eq:
            return "Eq";
        case HirBinaryOp::Ne:
            return "Ne";
        case HirBinaryOp::Lt:
            return "Lt";
        case HirBinaryOp::Gt:
            return "Gt";
        case HirBinaryOp::Le:
            return "Le";
        case HirBinaryOp::Ge:
            return "Ge";
        case HirBinaryOp::Assign:
            return "Assign";
        default:
            return "Unknown";
    }
}

std::string HirLowering::hir_unary_op_to_string(HirUnaryOp op) {
    switch (op) {
        case HirUnaryOp::Neg:
            return "Neg";
        case HirUnaryOp::Not:
            return "Not";
        case HirUnaryOp::BitNot:
            return "BitNot";
        case HirUnaryOp::Deref:
            return "Deref";
        case HirUnaryOp::AddrOf:
            return "AddrOf";
        case HirUnaryOp::PreInc:
            return "PreInc";
        case HirUnaryOp::PreDec:
            return "PreDec";
        case HirUnaryOp::PostInc:
            return "PostInc";
        case HirUnaryOp::PostDec:
            return "PostDec";
        default:
            return "Unknown";
    }
}

}  // namespace cm::hir
