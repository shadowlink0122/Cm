// ============================================================
// HIR lowering - 式の種別ディスパッチ（lower_expr）
// ============================================================

#include "internal/hir/lowering/fwd.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace cm::hir {

// 式の変換
HirExprPtr HirLowering::lower_expr(ast::Expr& expr) {
    debug::hir::log(debug::hir::Id::ExprLower, "", debug::Level::Trace);
    TypePtr type = expr.type ? expr.type : make_error();

    if (type && type->kind != ast::TypeKind::Error) {
        debug::hir::log(debug::hir::Id::ExprType, type_to_string(*type), debug::Level::Trace);
    }

    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        return lower_literal(*lit, type);
    } else if (auto* ident = expr.as<ast::IdentExpr>()) {
        debug::hir::log(debug::hir::Id::IdentifierLower, ident->name, debug::Level::Debug);

        // enum値アクセスかチェック
        auto it = enum_values_.find(ident->name);
        if (it != enum_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "enum value: " + ident->name + " = " + std::to_string(it->second),
                            debug::Level::Debug);

            // Tagged Union enumの場合はHirEnumConstructを生成
            // これにより、Option::Noneのようなペイロードなしバリアントも__TaggedUnion_型で正しく初期化される（tag + payload）
            std::string enum_name;
            std::string variant_name;
            auto sep = ident->name.find("::");
            if (sep != std::string::npos) {
                enum_name = ident->name.substr(0, sep);
                variant_name = ident->name.substr(sep + 2);
            }

            bool is_tagged = false;
            if (!enum_name.empty()) {
                auto def_it = enum_defs_.find(enum_name);
                if (def_it != enum_defs_.end() && def_it->second) {
                    is_tagged = def_it->second->is_tagged_union();
                }
            }

            if (is_tagged) {
                // Tagged Union: HirEnumConstructを生成（ペイロードなし）
                auto enum_construct = std::make_unique<HirEnumConstruct>();
                enum_construct->enum_name = enum_name;
                enum_construct->variant_name = variant_name;
                enum_construct->tag_value = it->second;
                // ペイロードなし（payload = nullptr）

                auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
                tagged_union_type->name = "__TaggedUnion_" + enum_name;

                return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
            }

            // 通常のenum（Tagged Unionでない）はintリテラルとして処理
            auto lit = std::make_unique<HirLiteral>();
            lit->value = it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
        }

        // v0.13.0: int型マクロ定数アクセスかチェック
        auto macro_it = macro_values_.find(ident->name);
        if (macro_it != macro_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "macro int: " + ident->name + " = " + std::to_string(macro_it->second),
                            debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
        }

        // v0.13.0: string型マクロ定数アクセスかチェック
        auto macro_str_it = macro_string_values_.find(ident->name);
        if (macro_str_it != macro_string_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "macro string: " + ident->name + " = \"" + macro_str_it->second + "\"",
                            debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_str_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_string());
        }

        // v0.13.0: bool型マクロ定数アクセスかチェック
        auto macro_bool_it = macro_bool_values_.find(ident->name);
        if (macro_bool_it != macro_bool_values_.end()) {
            debug::hir::log(
                debug::hir::Id::IdentifierRef,
                "macro bool: " + ident->name + " = " + (macro_bool_it->second ? "true" : "false"),
                debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_bool_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_bool());
        }

        debug::hir::log(debug::hir::Id::IdentifierRef, "variable: " + ident->name,
                        debug::Level::Trace);
        auto var_ref = std::make_unique<HirVarRef>();
        var_ref->name = ident->name;
        if (func_defs_.find(ident->name) != func_defs_.end()) {
            var_ref->is_function_ref = true;
            debug::hir::log(debug::hir::Id::IdentifierRef, "function reference: " + ident->name,
                            debug::Level::Debug);
        }
        return std::make_unique<HirExpr>(std::move(var_ref), type);
    } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
        return lower_binary(*binary, type);
    } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
        return lower_unary(*unary, type);
    } else if (auto* call = expr.as<ast::CallExpr>()) {
        return lower_call(*call, type);
    } else if (auto* idx = expr.as<ast::IndexExpr>()) {
        return lower_index(*idx, type);
    } else if (auto* slice = expr.as<ast::SliceExpr>()) {
        return lower_slice(*slice, type);
    } else if (auto* mem = expr.as<ast::MemberExpr>()) {
        return lower_member(*mem, type);
    } else if (auto* tern = expr.as<ast::TernaryExpr>()) {
        return lower_ternary(*tern, type);
    } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
        return lower_match(*match_expr, type);
    } else if (auto* struct_lit = expr.as<ast::StructLiteralExpr>()) {
        return lower_struct_literal(*struct_lit, type);
    } else if (auto* array_lit = expr.as<ast::ArrayLiteralExpr>()) {
        return lower_array_literal(*array_lit, type);
    } else if (auto* lambda_expr = expr.as<ast::LambdaExpr>()) {
        return lower_lambda(*lambda_expr, type);
    } else if (auto* sizeof_expr = expr.as<ast::SizeofExpr>()) {
        // sizeof(T) または sizeof(expr) をコンパイル時定数として評価
        int64_t size = 0;
        std::string type_name;
        ast::TypePtr target_type_ptr;
        if (sizeof_expr->target_type) {
            size = calculate_type_size(sizeof_expr->target_type);
            type_name = ast::type_to_string(*sizeof_expr->target_type);
            target_type_ptr = sizeof_expr->target_type;
        } else if (sizeof_expr->target_expr && sizeof_expr->target_expr->type) {
            size = calculate_type_size(sizeof_expr->target_expr->type);
            type_name = ast::type_to_string(*sizeof_expr->target_expr->type);
            target_type_ptr = sizeof_expr->target_expr->type;
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "sizeof(" + type_name + ") = " + std::to_string(size), debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = size;

        // ジェネリック型パラメータ（T, U, V, K, E等）を検出
        // 型名が単一の大文字、または大文字＋数字（T1, T2等）の場合
        // マーカー型情報を持つuintとして返す（モノモーフィゼーション時に再計算）
        bool is_generic_param = false;
        if (target_type_ptr) {
            const std::string& tname = target_type_ptr->name;
            // TypeKind::Generic または TypeKind::Struct でも
            // 型名が単一大文字または大文字+数字の場合はジェネリックパラメータとみなす
            if (target_type_ptr->kind == ast::TypeKind::Generic ||
                target_type_ptr->kind == ast::TypeKind::Struct) {
                if (tname.length() == 1 && std::isupper(tname[0])) {
                    is_generic_param = true;
                } else if (tname.length() == 2 && std::isupper(tname[0]) &&
                           std::isdigit(tname[1])) {
                    is_generic_param = true;
                }
            }
        }

        if (is_generic_param) {
            // ジェネリック型パラメータのsizeofの場合
            // 特別なマーカー型を作成（sizeof_for_T形式）
            // モノモーフィゼーション時にこの型を検出して正しいサイズを計算
            auto marker_type = std::make_shared<ast::Type>(ast::TypeKind::Generic);
            marker_type->name = "sizeof_for_" + target_type_ptr->name;
            debug::hir::log(debug::hir::Id::LiteralLower,
                            "sizeof for generic param: " + target_type_ptr->name +
                                " (marker: " + marker_type->name + ")",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(lit), marker_type);
        }

        // 型引数にジェネリックパラメータを含む複合型（QueueNode<T> 等）もマーカー化してモノモーフィゼーション時に実サイズへ置換する。
        // HIR時点の推定サイズはT=longや構造体Tで実レイアウトを下回り、mallocの下回り確保による隣接ヒープ破壊の原因になっていた
        if (target_type_ptr && !target_type_ptr->type_args.empty()) {
            std::function<bool(const ast::TypePtr&)> has_generic_param =
                [&](const ast::TypePtr& t) -> bool {
                if (!t) {
                    return false;
                }
                const std::string& n = t->name;
                bool param_like =
                    (n.length() == 1 && std::isupper(static_cast<unsigned char>(n[0]))) ||
                    (n.length() == 2 && std::isupper(static_cast<unsigned char>(n[0])) &&
                     std::isdigit(static_cast<unsigned char>(n[1])));
                if ((t->kind == ast::TypeKind::Generic || t->kind == ast::TypeKind::Struct) &&
                    param_like) {
                    return true;
                }
                for (const auto& a : t->type_args) {
                    if (has_generic_param(a)) {
                        return true;
                    }
                }
                if (t->element_type) {
                    return has_generic_param(t->element_type);
                }
                return false;
            };
            if (has_generic_param(target_type_ptr)) {
                auto marker_type = std::make_shared<ast::Type>(ast::TypeKind::Generic);
                std::string composite = target_type_ptr->name + "<";
                for (size_t i = 0; i < target_type_ptr->type_args.size(); ++i) {
                    if (i > 0) {
                        composite += ", ";
                    }
                    const auto& a = target_type_ptr->type_args[i];
                    composite +=
                        (a && !a->name.empty()) ? a->name : (a ? ast::type_to_string(*a) : "?");
                }
                composite += ">";
                marker_type->name = "sizeof_for_" + composite;
                debug::hir::log(debug::hir::Id::LiteralLower,
                                "sizeof for generic composite: " + composite +
                                    " (marker: " + marker_type->name + ")",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(lit), marker_type);
            }
        }

        return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
    } else if (auto* typeof_expr = expr.as<ast::TypeofExpr>()) {
        // typeof(expr) - 式の型を返すが、値としては0を返す（型コンテキストで使用）
        // 式として評価される場合はエラーとして扱う
        (void)typeof_expr;  // 未使用警告を抑制
        debug::hir::log(debug::hir::Id::Warning, "typeof expression used in value context",
                        debug::Level::Warn);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = static_cast<int64_t>(0);
        return std::make_unique<HirExpr>(std::move(lit), ast::make_error());
    } else if (auto* alignof_expr = expr.as<ast::AlignofExpr>()) {
        // alignof(T) - 型のアラインメントをコンパイル時定数として評価
        int64_t alignment = 0;
        std::string type_name;
        if (alignof_expr->target_type) {
            alignment = calculate_type_align(alignof_expr->target_type);
            type_name = ast::type_to_string(*alignof_expr->target_type);
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "alignof(" + type_name + ") = " + std::to_string(alignment),
                        debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = alignment;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
    } else if (auto* typename_expr = expr.as<ast::TypenameOfExpr>()) {
        // typename(T) または typename(expr) - 型の名前を文字列として返す
        std::string type_name;
        if (typename_expr->target_type) {
            // 型が直接指定された場合
            type_name = ast::type_to_string(*typename_expr->target_type);
        } else if (typename_expr->target_expr) {
            // 式が指定された場合、式の型を取得
            auto lowered = lower_expr(*typename_expr->target_expr);
            if (lowered && lowered->type) {
                type_name = ast::type_to_string(*lowered->type);
            } else {
                type_name = "<unknown>";
            }
        }
        debug::hir::log(debug::hir::Id::LiteralLower, "typename = \"" + type_name + "\"",
                        debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = type_name;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_string());
    } else if (auto* cast_expr = expr.as<ast::CastExpr>()) {
        // キャスト式: expr as Type
        debug::hir::log(debug::hir::Id::CastExprLower, "Lowering cast expression",
                        debug::Level::Debug);
        auto operand = lower_expr(*cast_expr->operand);
        auto hir_cast = std::make_unique<HirCast>();
        hir_cast->operand = std::move(operand);
        hir_cast->target_type = cast_expr->target_type;
        // is（型判別）はboolを返す
        hir_cast->check_only = cast_expr->type_check;
        auto result_type = cast_expr->type_check ? ast::make_bool() : cast_expr->target_type;
        return std::make_unique<HirExpr>(std::move(hir_cast), result_type);
    } else if (auto* move_expr = expr.as<ast::MoveExpr>()) {
        // move式: move x は x そのものを返す（所有権移動、ゼロコスト）
        debug::hir::log(debug::hir::Id::ExprLower, "Lowering move expression", debug::Level::Debug);
        // moveは単にオペランドを返す - 所有権追跡は型チェッカーで行われている
        auto moved = lower_expr(*move_expr->operand);
        // 変数参照のmoveはMIRへ伝搬し、moved-out変数のデストラクタ登録を解除させる
        // （これが無いと move で所有権を渡した変数がスコープ終了時にも解放され二重解放になる）
        if (moved) {
            if (auto* var_ref = std::get_if<std::unique_ptr<HirVarRef>>(&moved->kind)) {
                if (*var_ref)
                    (*var_ref)->is_moved_from = true;
            }
        }
        return moved;
    } else if (auto* await_expr = expr.as<ast::AwaitExpr>()) {
        // await式: オペランドを評価し、is_awaitedフラグを設定
        debug::hir::log(debug::hir::Id::ExprLower, "Lowering await expression",
                        debug::Level::Debug);
        auto hir_operand = lower_expr(*await_expr->operand);

        // オペランドがHirCallの場合、is_awaitedをtrueに設定
        if (auto* hir_call = std::get_if<std::unique_ptr<HirCall>>(&hir_operand->kind)) {
            (*hir_call)->is_awaited = true;
        }

        return hir_operand;
    }

    debug::hir::log(debug::hir::Id::Warning, "Unknown expression type, using null literal",
                    debug::Level::Warn);
    auto lit = std::make_unique<HirLiteral>();
    return std::make_unique<HirExpr>(std::move(lit), type);
}

}  // namespace cm::hir
