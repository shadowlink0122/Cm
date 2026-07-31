// ============================================================
// TypeChecker 実装 - 式型推論のディスパッチとリテラル・構造体リテラル・識別子の推論、move状態の追跡
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

ast::TypePtr TypeChecker::infer_type(ast::Expr& expr) {
    debug::tc::log(debug::tc::Id::CheckExpr, "", debug::Level::Trace);

    // エラー表示用に現在の式のSpanを保存
    current_span_ = expr.span;

    ast::TypePtr inferred_type;

    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        inferred_type = infer_literal(*lit);
    } else if (auto* ident = expr.as<ast::IdentExpr>()) {
        inferred_type = infer_ident(*ident);
    } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
        inferred_type = infer_binary(*binary);
    } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
        inferred_type = infer_unary(*unary);
    } else if (auto* call = expr.as<ast::CallExpr>()) {
        inferred_type = infer_call(*call);
    } else if (auto* member = expr.as<ast::MemberExpr>()) {
        inferred_type = infer_member(*member);
    } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
        inferred_type = infer_ternary(*ternary);
    } else if (auto* idx = expr.as<ast::IndexExpr>()) {
        inferred_type = infer_index(*idx);
    } else if (auto* slice = expr.as<ast::SliceExpr>()) {
        inferred_type = infer_slice(*slice);
    } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
        inferred_type = infer_match(*match_expr);
    } else if (auto* array_lit = expr.as<ast::ArrayLiteralExpr>()) {
        inferred_type = infer_array_literal(*array_lit);
    } else if (auto* struct_lit = expr.as<ast::StructLiteralExpr>()) {
        inferred_type = infer_struct_literal(*struct_lit);
    } else if (auto* lambda_expr = expr.as<ast::LambdaExpr>()) {
        inferred_type = infer_lambda(*lambda_expr);
    } else if (auto* sizeof_expr = expr.as<ast::SizeofExpr>()) {
        // sizeof(型)の場合、型が有効かチェック
        // 無効な場合は変数として解釈を試みる
        if (sizeof_expr->target_type) {
            auto& target_type = sizeof_expr->target_type;
            // 構造体型として解析されたが、実際には変数かもしれない
            if (target_type->kind == ast::TypeKind::Struct) {
                std::string name = target_type->name;
                // typedefや構造体として定義されているかチェック
                bool is_valid_type = false;
                if (typedef_defs_.count(name) > 0) {
                    is_valid_type = true;
                } else if (struct_defs_.count(name) > 0) {
                    is_valid_type = true;
                }

                if (!is_valid_type) {
                    // 変数として解決を試みる
                    auto sym = scopes_.current().lookup(name);
                    if (sym && sym->type && sym->type->kind != ast::TypeKind::Error) {
                        // 変数として有効 - target_exprを設定
                        sizeof_expr->target_expr = ast::make_ident(name, {});
                        sizeof_expr->target_expr->type = sym->type;
                        sizeof_expr->target_type = nullptr;
                    }
                }
            }
        }
        // sizeof(式) の場合は式の型チェックを行う（コンパイル時のメタ情報取得であり値は読まないため、未初期化チェックの対象外）
        if (sizeof_expr->target_expr) {
            if (auto* target_ident = sizeof_expr->target_expr->as<ast::IdentExpr>()) {
                mark_variable_initialized(target_ident->name);
            }
            infer_type(*sizeof_expr->target_expr);
        }
        // sizeof は常に uint (符号なし整数) を返す
        inferred_type = ast::make_uint();
    } else if (auto* typeof_expr = expr.as<ast::TypeofExpr>()) {
        // typeof(式) - 式の型を推論（メタ情報取得のため未初期化チェックの対象外）
        if (typeof_expr->target_expr) {
            if (auto* target_ident = typeof_expr->target_expr->as<ast::IdentExpr>()) {
                mark_variable_initialized(target_ident->name);
            }
            infer_type(*typeof_expr->target_expr);
            // typeof自体は型を返すが、ここでは式としてerrorを返す
            // (typeofは通常、型コンテキストで使用される)
        }
        inferred_type = ast::make_error();
    } else if (auto* typename_expr = expr.as<ast::TypenameOfExpr>()) {
        // __typename__(型) または __typename__(式) - 文字列を返す（メタ情報取得のため未初期化チェックの対象外）
        if (typename_expr->target_expr) {
            if (auto* target_ident = typename_expr->target_expr->as<ast::IdentExpr>()) {
                mark_variable_initialized(target_ident->name);
            }
            infer_type(*typename_expr->target_expr);
        }
        inferred_type = ast::make_string();
    } else if (auto* cast_expr = expr.as<ast::CastExpr>()) {
        // キャスト式: expr as Type / 型判別式: expr is Type
        // オペランドの型を推論
        ast::TypePtr operand_type;
        if (cast_expr->operand) {
            operand_type = infer_type(*cast_expr->operand);
        }
        if (cast_expr->type_check) {
            // is はユニオン型の値にのみ使用できる。対象型は変種のいずれかであること
            auto resolved = resolve_typedef(operand_type);
            auto variants = ast::union_variant_types(resolved);
            if (!resolved || resolved->kind != ast::TypeKind::Union || variants.empty()) {
                error(expr.span,
                      i18n::msgf(i18n::MsgId::TypeIsCanOnlyBeUsed,
                                 (operand_type ? ast::type_to_string(*operand_type)
                                               : i18n::msg(i18n::MsgId::TypeLabelUnknown))));
            } else if (cast_expr->target_type) {
                std::string target_name = ast::type_to_string(*cast_expr->target_type);
                bool found = false;
                for (const auto& v : variants) {
                    if (v && ast::type_to_string(*v) == target_name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    error(expr.span, i18n::msgf(i18n::MsgId::TypeTheTargetTypeIsNot, target_name));
                }
            }
            inferred_type = ast::make_bool();
        } else {
            // ターゲット型を返す
            inferred_type = cast_expr->target_type;
            // 不正な as キャストを拒否する（C10）。
            // 数値スカラ → string はビット再解釈になりクラッシュ・空文字化の原因のため型検査で弾く。
            // ユニオン downcast（union as variant）やポインタ/cstring → string は正当なので対象外。
            auto rop = resolve_typedef(operand_type);
            auto rtgt = resolve_typedef(cast_expr->target_type);
            if (rop && rtgt &&
                (rtgt->kind == ast::TypeKind::String || rtgt->kind == ast::TypeKind::CString)) {
                bool operand_is_numeric = false;
                switch (rop->kind) {
                    case ast::TypeKind::Bool:
                    case ast::TypeKind::Tiny:
                    case ast::TypeKind::Short:
                    case ast::TypeKind::Int:
                    case ast::TypeKind::Long:
                    case ast::TypeKind::UTiny:
                    case ast::TypeKind::UShort:
                    case ast::TypeKind::UInt:
                    case ast::TypeKind::ULong:
                    case ast::TypeKind::ISize:
                    case ast::TypeKind::USize:
                    case ast::TypeKind::Float:
                    case ast::TypeKind::Double:
                    case ast::TypeKind::UFloat:
                    case ast::TypeKind::UDouble:
                    case ast::TypeKind::Char:
                        operand_is_numeric = true;
                        break;
                    default:
                        break;
                }
                if (operand_is_numeric) {
                    error(expr.span, i18n::msgf(i18n::MsgId::TypeCannotCastNumericToString,
                                                ast::type_to_string(*rop)));
                }
            }
            // 整数リテラルの縮小キャストで値が収まらない場合は警告する（M4）。
            // 例: 300 as tiny は 44 に切り捨てられるが以前は無警告だった。
            if (rtgt && cast_expr->operand) {
                if (auto* lit = cast_expr->operand->as<ast::LiteralExpr>()) {
                    if (std::holds_alternative<int64_t>(lit->value)) {
                        int64_t v = std::get<int64_t>(lit->value);
                        bool has_range = true;
                        bool fits = true;
                        switch (rtgt->kind) {
                            case ast::TypeKind::Tiny:
                                fits = (v >= -128 && v <= 127);
                                break;
                            case ast::TypeKind::UTiny:
                            case ast::TypeKind::Char:
                                fits = (v >= 0 && v <= 255);
                                break;
                            case ast::TypeKind::Short:
                                fits = (v >= -32768 && v <= 32767);
                                break;
                            case ast::TypeKind::UShort:
                                fits = (v >= 0 && v <= 65535);
                                break;
                            case ast::TypeKind::Int:
                                fits = (v >= -2147483648LL && v <= 2147483647LL);
                                break;
                            case ast::TypeKind::UInt:
                                fits = (v >= 0 && v <= 4294967295LL);
                                break;
                            default:
                                has_range = false;
                                break;
                        }
                        if (has_range && !fits) {
                            warning(expr.span,
                                    i18n::msgf(i18n::MsgId::TypeIntegerLiteralNarrowingTruncates,
                                               std::to_string(v), ast::type_to_string(*rtgt)));
                        }
                    }
                }
            }
        }
    } else if (auto* move_expr = expr.as<ast::MoveExpr>()) {
        // move式: オペランドの型を推論し、変数をmoved状態にマーク
        if (move_expr->operand) {
            inferred_type = infer_type(*move_expr->operand);
            // オペランドが識別子の場合、その変数を移動済みとしてマーク
            if (auto* ident = move_expr->operand->as<ast::IdentExpr>()) {
                // 借用中の変数はmove禁止（借用安全性）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(current_span_, "Cannot move '" + ident->name + "' while it is borrowed");
                    return ast::make_error();
                }
                mark_variable_moved(ident->name);
                debug::tc::log(debug::tc::Id::CheckExpr,
                               "Marked variable '" + ident->name + "' as moved",
                               debug::Level::Debug);
            }
            // フィールド経由のmove（move obj.field）は基底変数を移動済み扱いにする
            // （H12: 従来はASTパターン不一致でマークされず、move後使用がすり抜けていた）
            else if (auto* member = move_expr->operand->as<ast::MemberExpr>()) {
                const ast::Expr* base = member->object.get();
                while (base) {
                    if (const auto* inner = base->as<ast::MemberExpr>()) {
                        base = inner->object.get();
                    } else {
                        break;
                    }
                }
                if (base) {
                    if (const auto* base_ident = base->as<ast::IdentExpr>()) {
                        if (scopes_.current().is_borrowed(base_ident->name)) {
                            error(current_span_,
                                  "Cannot move '" + base_ident->name + "' while it is borrowed");
                            return ast::make_error();
                        }
                        mark_variable_moved(base_ident->name);
                        debug::tc::log(
                            debug::tc::Id::CheckExpr,
                            "Marked variable '" + base_ident->name + "' as moved (field move)",
                            debug::Level::Debug);
                    }
                }
            }
        } else {
            inferred_type = ast::make_error();
        }
    } else if (auto* await_expr = expr.as<ast::AwaitExpr>()) {
        // await式: Future<T>を待機してTを返す
        // 現時点では単にオペランドの型を返す（同期的実行）
        if (await_expr->operand) {
            inferred_type = infer_type(*await_expr->operand);
            debug::tc::log(debug::tc::Id::CheckExpr, "Inferred await expression type",
                           debug::Level::Debug);
        } else {
            inferred_type = ast::make_error();
        }
    } else {
        inferred_type = ast::make_error();
    }

    if (inferred_type && !expr.type) {
        expr.type = inferred_type;
    } else if (inferred_type && expr.type) {
        // 推論された型がexpr.typeより情報豊富な場合は上書き
        // 例: パーサーがname=空のStruct型を設定していても、スコープから取得した型がname="Result"でtype_argsを持つ場合
        bool inferred_has_more_info = false;
        if (expr.type->name.empty() && !inferred_type->name.empty()) {
            inferred_has_more_info = true;
        }
        if (expr.type->type_args.empty() && !inferred_type->type_args.empty()) {
            inferred_has_more_info = true;
        }
        if (inferred_has_more_info) {
            expr.type = inferred_type;
        }
    } else if (!expr.type) {
        expr.type = ast::make_error();
    }

    return expr.type;
}

ast::TypePtr TypeChecker::infer_literal(ast::LiteralExpr& lit) {
    if (lit.is_null())
        return ast::make_void();
    if (lit.is_bool())
        return std::make_shared<ast::Type>(ast::TypeKind::Bool);
    if (lit.is_int()) {
        int64_t val = std::get<int64_t>(lit.value);

        // unsignedリテラル（hex/binary/octalで32bit超の値）の場合
        // lexerが uint64_t → int64_t にbit_castしているため、int64_t値だけでは元の大きさを判別できない
        if (lit.is_unsigned_literal) {
            uint64_t uval = static_cast<uint64_t>(val);
            // int64_t正範囲 (INT32_MAX+1 ～ INT64_MAX) なら long
            // UIntではなくLongを使う: LLVM codegenでi32→i64の符号拡張を避ける
            if (uval <= static_cast<uint64_t>(INT64_MAX)) {
                return std::make_shared<ast::Type>(ast::TypeKind::Long);
            }
            // それ以上 (0x8000000000000000 ～ 0xFFFFFFFFFFFFFFFF) なら ulong
            return std::make_shared<ast::Type>(ast::TypeKind::ULong);
        }

        // 通常の10進数リテラルの型推論
        // i32範囲: -2147483648 ～ 2147483647
        if (val >= INT32_MIN && val <= INT32_MAX) {
            return ast::make_int();
        }
        // i32範囲外: long型
        return std::make_shared<ast::Type>(ast::TypeKind::Long);
    }
    if (lit.is_float())
        return ast::make_double();
    if (lit.is_char())
        return ast::make_char();
    if (lit.is_string()) {
        // 文字列リテラルはどこでも補間される（string s = "{x}" 等）ため、プレースホルダ内の変数参照を使用としてマークする（W001誤検出防止）
        mark_interpolation_uses(std::get<std::string>(lit.value));
        return ast::make_string();
    }
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_array_literal(ast::ArrayLiteralExpr& lit) {
    if (lit.elements.empty()) {
        return ast::make_array(ast::make_int(), 0);
    }

    auto first_type = infer_type(*lit.elements[0]);

    for (size_t i = 1; i < lit.elements.size(); ++i) {
        auto elem_type = infer_type(*lit.elements[i]);
    }

    return ast::make_array(first_type, lit.elements.size());
}

// リテラル式へ期待型を再帰的に伝播する（W1/X3/X4）。
// 「構造体リテラル > 配列リテラル > 無名構造体リテラル」等のネストで要素側の期待型が
// 伝わらず、型不明のままゼロ/未初期化blobとしてlowerされてフィールド喪失・ゴミ値になっていた
void TypeChecker::propagate_literal_expected_type(ast::Expr& expr, const ast::TypePtr& expected) {
    if (!expected) {
        return;
    }
    auto resolved = resolve_typedef(expected);
    if (!resolved) {
        return;
    }
    if (auto* slit = expr.as<ast::StructLiteralExpr>()) {
        if (resolved->kind != ast::TypeKind::Struct) {
            return;
        }
        if (slit->type_name.empty()) {
            slit->type_name = resolved->name;
        }
        expr.type = resolved;
        // フィールド値へ再帰伝播（フィールド型は構造体定義から引く）
        const ast::StructDecl* sd = get_struct(resolved->name);
        if (sd) {
            for (auto& fv : slit->fields) {
                for (const auto& sf : sd->fields) {
                    if (sf.name == fv.name && fv.value) {
                        propagate_literal_expected_type(*fv.value, sf.type);
                        break;
                    }
                }
            }
        }
        return;
    }
    if (auto* alit = expr.as<ast::ArrayLiteralExpr>()) {
        if (resolved->kind != ast::TypeKind::Array) {
            return;
        }
        expr.type = resolved;
        if (resolved->element_type) {
            for (auto& el : alit->elements) {
                if (el) {
                    propagate_literal_expected_type(*el, resolved->element_type);
                }
            }
        }
        return;
    }
}

ast::TypePtr TypeChecker::infer_struct_literal(ast::StructLiteralExpr& lit) {
    if (lit.type_name.empty()) {
        return ast::make_error();
    }

    auto struct_it = struct_defs_.find(lit.type_name);
    if (struct_it == struct_defs_.end()) {
        // 名前空間内の非修飾名は「現在の名前空間::名前」として解決し、リテラルの型名を修飾名へ書き換える（HIR/コード生成へ伝播）
        if (auto qualified = resolve_in_namespace(lit.type_name)) {
            lit.type_name = *qualified;
            struct_it = struct_defs_.find(lit.type_name);
        }
    }
    // typedef別名（typedef P = Point; / typedef IntPair = Pair<int,int>;）は再帰的に基底名へ解決してから構造体表を引く（B8）
    ast::TypePtr alias_target;
    if (struct_it == struct_defs_.end()) {
        std::set<std::string> visited;
        std::string base_name = lit.type_name;
        while (struct_defs_.find(base_name) == struct_defs_.end() &&
               visited.insert(base_name).second) {
            auto td_it = typedef_defs_.find(base_name);
            if (td_it == typedef_defs_.end() || !td_it->second)
                break;
            const auto& target = td_it->second;
            // 別名の基底が名前付き構造体型でなければ構造体リテラルの対象外
            if (target->kind != ast::TypeKind::Struct || target->name.empty())
                break;
            alias_target = target;
            base_name = target->name;
        }
        auto base_it = struct_defs_.find(base_name);
        if (base_it != struct_defs_.end()) {
            // 基底名へ書き換えてHIR/コード生成へ伝播する（名前空間解決と同じ方式）
            debug::tc::log(debug::tc::Id::TypeInfer,
                           "Resolved struct literal alias: " + lit.type_name + " -> " + base_name,
                           debug::Level::Debug);
            lit.type_name = base_name;
            struct_it = base_it;
        } else {
            debug::tc::log(
                debug::tc::Id::TypeInfer,
                "Struct literal alias unresolved: " + lit.type_name + " (base " + base_name + ")",
                debug::Level::Debug);
        }
    }
    if (struct_it == struct_defs_.end()) {
        error(current_span_, "Unknown struct type: " + lit.type_name);
        return ast::make_error();
    }

    for (auto& field : lit.fields) {
        // フィールド型を期待型としてリテラル値へ伝播してから推論する（W1）
        {
            const ast::StructDecl* sd0 = struct_it->second;
            for (const auto& sf : sd0->fields) {
                if (sf.name == field.name && field.value) {
                    propagate_literal_expected_type(*field.value, sf.type);
                    break;
                }
            }
        }
        infer_type(*field.value);
        // キャプチャ付きクロージャの構造体フィールド格納は環境喪失でゴミ値になるため拒否（V6）
        if (field.value && is_capturing_closure_expr(*field.value)) {
            const ast::StructDecl* sd = struct_it->second;
            for (const auto& sf : sd->fields) {
                if (sf.name == field.name && sf.type && sf.type->kind == ast::TypeKind::Function) {
                    error(current_span_,
                          "Cannot store a capturing closure in struct field '" + field.name +
                              "' of '" + lit.type_name +
                              "': closures lose their captured environment when stored as "
                              "values (bind to a local variable and call it directly)");
                    break;
                }
            }
        }
    }

    // ジェネリック特殊化別名（typedef IntPair = Pair<int,int>;）は型引数付きの基底型をそのまま返す
    if (alias_target && !alias_target->type_args.empty()) {
        return alias_target;
    }

    auto type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
    type->name = lit.type_name;
    return type;
}

std::optional<Symbol> TypeChecker::lookup_var_ident(ast::IdentExpr& ident) {
    auto sym = scopes_.current().lookup(ident.name);
    // ローカル変数・パラメータ（レベル1以上）は名前空間より優先する
    if (sym && sym->scope_level > 0) {
        return sym;
    }
    if (!current_namespace_.empty() && ident.name.find("::") == std::string::npos) {
        // 名前空間内の非修飾参照は「現在の名前空間::名前」を優先して解決する（内側から外側へ探索し、同名のトップレベルグローバルより名前空間内を優先。
        // 関数はcall側で解決されるため変数のみ対象）
        std::string ns = current_namespace_;
        while (!ns.empty()) {
            std::string qualified = ns + "::" + ident.name;
            if (auto ns_sym = scopes_.current().lookup(qualified)) {
                if (!ns_sym->is_function) {
                    ident.name = qualified;
                    return ns_sym;
                }
            }
            auto pos = ns.rfind("::");
            if (pos == std::string::npos) {
                break;
            }
            ns = ns.substr(0, pos);
        }
    }
    // 最終フォールバック: いずれかの名前空間のグローバルに一意に一致すれば解決する
    // （importモジュールの名前空間外へ複製された関数のモジュール内グローバル参照用）
    if (!sym && ident.name.find("::") == std::string::npos) {
        if (auto suffix_sym = scopes_.global().lookup_suffix_unique(ident.name)) {
            if (!suffix_sym->is_function) {
                ident.name = suffix_sym->name;
                return suffix_sym;
            }
        }
    }
    return sym;
}

ast::TypePtr TypeChecker::infer_ident(ast::IdentExpr& ident) {
    auto sym = lookup_var_ident(ident);
    if (!sym) {
        // 暗黙的selfは許可しない - 明示的にself.fieldを使用する必要がある
        error(current_span_, "Undefined variable '" + ident.name + "'");
        return ast::make_error();
    }

    // 変数使用をマーク（未使用変数検出用 W001）
    scopes_.current().mark_used(ident.name);

    // 初期化前使用のチェック
    check_uninitialized_use(ident.name, current_span_);

    // 移動後使用のチェック（Move Semantics）
    check_use_after_move(ident.name, current_span_);

    debug::tc::log(debug::tc::Id::Resolved, ident.name + " : " + ast::type_to_string(*sym->type),
                   debug::Level::Trace);
    return sym->type;
}

// ============================================================
// Move Semantics ヘルパー関数
// ============================================================

void TypeChecker::mark_variable_moved(const std::string& name) {
    // Scopeベースの移動状態管理
    scopes_.current().mark_moved(name);
}

void TypeChecker::check_use_after_move(const std::string& name, Span span) {
    // Symbolのis_movedフラグをチェック
    auto sym = scopes_.current().lookup(name);
    if (sym && sym->is_moved) {
        error(span, i18n::msgf(i18n::MsgId::TypeUseAfterMove, name));
    }
}

}  // namespace cm
