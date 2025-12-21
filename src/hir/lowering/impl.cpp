// lowering.cpp - メインエントリポイントとヘルパー関数
#include "../../frontend/ast/typedef.hpp"
#include "fwd.hpp"

namespace cm::hir {

// メインエントリポイント
HirProgram HirLowering::lower(ast::Program& program) {
    debug::hir::log(debug::hir::Id::LowerStart);

    HirProgram hir;
    hir.filename = program.filename;

    // Pass 1: 構造体定義、enum定義、関数定義、コンストラクタ情報、importを収集
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
        } else if (auto* td = decl->as<ast::TypedefDecl>()) {
            typedef_defs_[td->name] = td->type;
        } else if (auto* impl = decl->as<ast::ImplDecl>()) {
            if (impl->is_ctor_impl && impl->target_type) {
                std::string type_name = type_to_string(*impl->target_type);
                for (auto& ctor : impl->constructors) {
                    if (ctor->params.empty()) {
                        types_with_default_ctor_.insert(type_name);
                    }
                }
            }
        } else if (auto* imp = decl->as<ast::ImportDecl>()) {
            // importエイリアスを先に登録
            std::string path_str = imp->path.to_string();
            debug::hir::log(debug::hir::Id::NodeCreate, "import path: " + path_str, debug::Level::Debug);
            if (path_str == "std::io::println") {
                import_aliases_["println"] = "__println__";
                debug::hir::log(debug::hir::Id::NodeCreate, "registered alias: println -> __println__", debug::Level::Debug);
            } else if (path_str == "std::io::print") {
                import_aliases_["print"] = "__print__";
            } else if (path_str == "std::io") {
                for (const auto& item : imp->items) {
                    if (item.name == "println") {
                        import_aliases_["println"] = "__println__";
                    } else if (item.name == "print") {
                        import_aliases_["print"] = "__print__";
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

// 型アラインメント計算（sizeof/alignof用）
int64_t HirLowering::calculate_type_align(const TypePtr& type) {
    if (!type)
        return 1;

    switch (type->kind) {
        case ast::TypeKind::Bool:
        case ast::TypeKind::Tiny:
        case ast::TypeKind::UTiny:
        case ast::TypeKind::Char:
            return 1;

        case ast::TypeKind::Short:
        case ast::TypeKind::UShort:
            return 2;

        case ast::TypeKind::Int:
        case ast::TypeKind::UInt:
        case ast::TypeKind::Float:
        case ast::TypeKind::UFloat:
            return 4;

        case ast::TypeKind::Long:
        case ast::TypeKind::ULong:
        case ast::TypeKind::Double:
        case ast::TypeKind::UDouble:
            return 8;

        case ast::TypeKind::Pointer:
        case ast::TypeKind::Reference:
        case ast::TypeKind::String:
            return 8;  // 64-bit pointer

        case ast::TypeKind::Array:
            if (type->element_type) {
                return calculate_type_align(type->element_type);
            }
            return 8;

        case ast::TypeKind::Struct: {
            // まずtypedefとして解決を試みる
            auto td_it = typedef_defs_.find(type->name);
            if (td_it != typedef_defs_.end()) {
                return calculate_type_align(td_it->second);
            }
            // 構造体のアラインメント = 最大フィールドのアラインメント
            auto it = struct_defs_.find(type->name);
            if (it != struct_defs_.end()) {
                int64_t max_align = 1;
                for (const auto& field : it->second->fields) {
                    int64_t field_align = calculate_type_align(field.type);
                    if (field_align > max_align)
                        max_align = field_align;
                }
                return max_align;
            }
            return 8;
        }

        case ast::TypeKind::Union: {
            // union型のアラインメント = 最大バリアントのアラインメント
            // static_castで UnionType にキャスト（TypeKindがUnionなので安全）
            auto* union_type = static_cast<const ast::UnionType*>(type.get());
            int64_t max_align = 4;  // タグのアラインメント
            for (const auto& variant : union_type->variants) {
                for (const auto& field_type : variant.fields) {
                    int64_t field_align = calculate_type_align(field_type);
                    if (field_align > max_align)
                        max_align = field_align;
                }
            }
            return max_align;
        }

        case ast::TypeKind::TypeAlias: {
            // typedef: 元の型のアラインメントを返す
            auto it = typedef_defs_.find(type->name);
            if (it != typedef_defs_.end()) {
                return calculate_type_align(it->second);
            }
            return 8;
        }

        case ast::TypeKind::Void:
            return 1;

        default:
            return 8;
    }
}

// 構造体のレイアウト計算（サイズ, アラインメント）
std::pair<int64_t, int64_t> HirLowering::calculate_struct_layout(
    const std::vector<ast::Field>& fields) {
    int64_t offset = 0;
    int64_t max_align = 1;

    for (const auto& field : fields) {
        int64_t field_size = calculate_type_size(field.type);
        int64_t field_align = calculate_type_align(field.type);

        if (field_align > max_align)
            max_align = field_align;

        // フィールドのアラインメントに合わせてオフセットを調整
        if (field_align > 0 && offset % field_align != 0) {
            offset = ((offset / field_align) + 1) * field_align;
        }

        offset += field_size;
    }

    // 構造体全体のサイズをアラインメントの倍数に切り上げ
    if (max_align > 0 && offset % max_align != 0) {
        offset = ((offset / max_align) + 1) * max_align;
    }

    return {offset > 0 ? offset : 1, max_align};  // 空の構造体は最小1バイト
}

// 型サイズ計算（sizeof用）
int64_t HirLowering::calculate_type_size(const TypePtr& type) {
    if (!type)
        return 0;

    switch (type->kind) {
        case ast::TypeKind::Bool:
        case ast::TypeKind::Tiny:
        case ast::TypeKind::UTiny:
        case ast::TypeKind::Char:
            return 1;

        case ast::TypeKind::Short:
        case ast::TypeKind::UShort:
            return 2;

        case ast::TypeKind::Int:
        case ast::TypeKind::UInt:
        case ast::TypeKind::Float:
        case ast::TypeKind::UFloat:
            return 4;

        case ast::TypeKind::Long:
        case ast::TypeKind::ULong:
        case ast::TypeKind::Double:
        case ast::TypeKind::UDouble:
            return 8;

        case ast::TypeKind::Pointer:
        case ast::TypeKind::Reference:
            return 8;  // 64-bit pointer

        case ast::TypeKind::Array:
            if (type->element_type && type->array_size.has_value()) {
                return calculate_type_size(type->element_type) * type->array_size.value();
            }
            return 8;  // 動的配列はポインタサイズ

        case ast::TypeKind::Struct: {
            // まずtypedefとして解決を試みる
            auto td_it = typedef_defs_.find(type->name);
            if (td_it != typedef_defs_.end()) {
                return calculate_type_size(td_it->second);
            }
            // アラインメントを考慮した構造体のサイズを計算
            auto it = struct_defs_.find(type->name);
            if (it != struct_defs_.end()) {
                auto [size, align] = calculate_struct_layout(it->second->fields);
                return size;
            }
            return 8;  // 不明な構造体はポインタサイズと仮定
        }

        case ast::TypeKind::Union: {
            // union型のサイズ = 最大バリアントのサイズ + タグ（int）
            // static_castで UnionType にキャスト（TypeKindがUnionなので安全）
            auto* union_type = static_cast<const ast::UnionType*>(type.get());
            int64_t max_size = 0;
            int64_t max_align = 4;  // タグのアラインメント
            for (const auto& variant : union_type->variants) {
                // 各バリアントのサイズとアラインメントを計算
                int64_t variant_size = 0;
                int64_t variant_align = 1;
                for (const auto& field_type : variant.fields) {
                    int64_t field_size = calculate_type_size(field_type);
                    int64_t field_align = calculate_type_align(field_type);
                    if (field_align > variant_align)
                        variant_align = field_align;
                    // アラインメントに合わせてオフセットを調整
                    if (field_align > 0 && variant_size % field_align != 0) {
                        variant_size = ((variant_size / field_align) + 1) * field_align;
                    }
                    variant_size += field_size;
                }
                if (variant_size > max_size)
                    max_size = variant_size;
                if (variant_align > max_align)
                    max_align = variant_align;
            }
            // タグ (4 bytes) + padding + データ
            int64_t data_offset = 4;
            if (max_align > 4 && data_offset % max_align != 0) {
                data_offset = ((data_offset / max_align) + 1) * max_align;
            }
            int64_t total = data_offset + max_size;
            // 全体のアラインメント
            if (max_align > 0 && total % max_align != 0) {
                total = ((total / max_align) + 1) * max_align;
            }
            return total > 0 ? total : 4;  // 空のunionでもタグは必要
        }

        case ast::TypeKind::TypeAlias: {
            // typedef: 元の型のサイズを返す
            auto it = typedef_defs_.find(type->name);
            if (it != typedef_defs_.end()) {
                return calculate_type_size(it->second);
            }
            return 8;
        }

        case ast::TypeKind::LiteralUnion:
            // リテラルユニオンは文字列として扱う
            return 8;

        case ast::TypeKind::String:
            return 8;  // 文字列はポインタサイズ

        case ast::TypeKind::Void:
            return 0;

        default:
            return 8;  // 不明な型はポインタサイズと仮定
    }
}

}  // namespace cm::hir
