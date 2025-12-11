#pragma once

#include "../../common/debug/tc.hpp"
#include "../ast/decl.hpp"
#include "../ast/module.hpp"
#include "../parser/parser.hpp"
#include "scope.hpp"

#include <functional>
#include <regex>

namespace cm {

// ============================================================
// 型チェッカー
// ============================================================
class TypeChecker {
   public:
    TypeChecker() {
        // 組み込み関数は import 時に登録される
    }

    // 構造体定義を登録
    void register_struct(const std::string& name, const ast::StructDecl& decl) {
        struct_defs_[name] = &decl;
    }

    // 構造体定義を取得
    const ast::StructDecl* get_struct(const std::string& name) const {
        auto it = struct_defs_.find(name);
        return it != struct_defs_.end() ? it->second : nullptr;
    }

    // 構造体のdefaultメンバの型を取得（なければnullptr）
    ast::TypePtr get_default_member_type(const std::string& struct_name) const {
        const ast::StructDecl* decl = get_struct(struct_name);
        if (!decl)
            return nullptr;
        for (const auto& field : decl->fields) {
            if (field.is_default) {
                return field.type;
            }
        }
        return nullptr;
    }

    // 構造体のdefaultメンバの名前を取得（なければ空文字列）
    std::string get_default_member_name(const std::string& struct_name) const {
        const ast::StructDecl* decl = get_struct(struct_name);
        if (!decl)
            return "";
        for (const auto& field : decl->fields) {
            if (field.is_default) {
                return field.name;
            }
        }
        return "";
    }

    // プログラム全体をチェック
    bool check(ast::Program& program) {
        debug::tc::log(debug::tc::Id::Start);

        // Pass 1: 関数シグネチャを登録
        for (auto& decl : program.declarations) {
            register_declaration(*decl);
        }

        // Pass 2: 関数本体をチェック
        for (auto& decl : program.declarations) {
            check_declaration(*decl);
        }

        debug::tc::log(debug::tc::Id::End, std::to_string(diagnostics_.size()) + " issues");
        return !has_errors();
    }

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.kind == DiagKind::Error)
                return true;
        }
        return false;
    }

   private:
    // メソッド情報
    struct MethodInfo {
        std::string name;
        std::vector<ast::TypePtr> param_types;
        ast::TypePtr return_type;
        ast::Visibility visibility = ast::Visibility::Export;  // デフォルトは公開
    };

    // 型ごとのメソッド情報 (型名 → メソッド名 → メソッド情報)
    std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>> type_methods_;

    // 現在チェック中のimplのターゲット型（privateメソッド呼び出しチェック用）
    std::string current_impl_target_type_;

    // フォーマット文字列から変数名を抽出
    std::vector<std::string> extract_format_variables(const std::string& format_str) {
        std::vector<std::string> variables;
        std::regex placeholder_regex(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
        std::smatch match;
        std::string::const_iterator search_start(format_str.cbegin());

        while (std::regex_search(search_start, format_str.cend(), match, placeholder_regex)) {
            std::string var_name = match[1];
            // 同じ変数を重複して追加しない
            if (std::find(variables.begin(), variables.end(), var_name) == variables.end()) {
                variables.push_back(var_name);
            }
            search_start = match.suffix().first;
        }

        return variables;
    }

    // 宣言の登録（パス1）
    void register_declaration(ast::Decl& decl) {
        if (auto* func = decl.as<ast::FunctionDecl>()) {
            std::vector<ast::TypePtr> param_types;
            for (const auto& p : func->params) {
                param_types.push_back(p.type);
            }
            scopes_.global().define_function(func->name, std::move(param_types), func->return_type);
        } else if (auto* st = decl.as<ast::StructDecl>()) {
            // 構造体を型として登録
            scopes_.global().define(st->name, ast::make_named(st->name));
            // 構造体定義を保存（フィールドアクセス用）
            register_struct(st->name, *st);
        } else if (auto* impl = decl.as<ast::ImplDecl>()) {
            // impl定義からメソッド情報を登録
            register_impl(*impl);
        }
    }

    // 宣言のチェック（パス2）
    void check_declaration(ast::Decl& decl) {
        debug::tc::log(debug::tc::Id::CheckDecl, "", debug::Level::Trace);

        if (auto* func = decl.as<ast::FunctionDecl>()) {
            check_function(*func);
        } else if (auto* import = decl.as<ast::ImportDecl>()) {
            check_import(*import);
        } else if (auto* impl = decl.as<ast::ImplDecl>()) {
            check_impl(*impl);
        }
    }

    // impl定義の登録
    void register_impl(ast::ImplDecl& impl) {
        if (!impl.target_type)
            return;

        std::string type_name = ast::type_to_string(*impl.target_type);

        // コンストラクタ専用implの場合
        if (impl.is_ctor_impl) {
            // コンストラクタを関数として登録
            for (const auto& ctor : impl.constructors) {
                std::string mangled_name = type_name + "__ctor";
                if (ctor->is_overload) {
                    // オーバーロードの場合、パラメータの数で区別
                    mangled_name += "_" + std::to_string(ctor->params.size());
                }
                std::vector<ast::TypePtr> param_types;
                param_types.push_back(impl.target_type);  // thisポインタ
                for (const auto& param : ctor->params) {
                    param_types.push_back(param.type);
                }
                scopes_.global().define_function(mangled_name, std::move(param_types),
                                                 ast::make_void());
            }
            // デストラクタを登録
            if (impl.destructor) {
                std::string mangled_name = type_name + "__dtor";
                std::vector<ast::TypePtr> param_types;
                param_types.push_back(impl.target_type);  // thisポインタ
                scopes_.global().define_function(mangled_name, std::move(param_types),
                                                 ast::make_void());
            }
            return;
        }

        // メソッド実装の場合
        for (const auto& method : impl.methods) {
            MethodInfo info;
            info.name = method->name;
            info.return_type = method->return_type;
            info.visibility = method->visibility;  // visibilityを保存
            for (const auto& param : method->params) {
                info.param_types.push_back(param.type);
            }
            type_methods_[type_name][method->name] = std::move(info);

            // グローバル関数としても登録（TypeName__methodName形式）
            std::string mangled_name = type_name + "__" + method->name;
            std::vector<ast::TypePtr> all_param_types;
            all_param_types.push_back(impl.target_type);  // selfパラメータ
            for (const auto& param : method->params) {
                all_param_types.push_back(param.type);
            }
            scopes_.global().define_function(mangled_name, std::move(all_param_types),
                                             method->return_type);
        }
    }

    // impl本体のチェック
    void check_impl(ast::ImplDecl& impl) {
        if (!impl.target_type)
            return;

        std::string type_name = ast::type_to_string(*impl.target_type);

        // コンストラクタ専用implの場合
        if (impl.is_ctor_impl) {
            // コンストラクタをチェック
            for (auto& ctor : impl.constructors) {
                scopes_.push();
                current_return_type_ = ast::make_void();

                // selfをスコープに追加（target_type型のポインタ）
                scopes_.current().define("self", impl.target_type, false);

                // パラメータをスコープに追加
                for (const auto& param : ctor->params) {
                    scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
                }

                // 本体をチェック
                for (auto& stmt : ctor->body) {
                    check_statement(*stmt);
                }

                scopes_.pop();
            }

            // デストラクタをチェック
            if (impl.destructor) {
                scopes_.push();
                current_return_type_ = ast::make_void();

                // selfをスコープに追加
                scopes_.current().define("self", impl.target_type, false);

                for (auto& stmt : impl.destructor->body) {
                    check_statement(*stmt);
                }

                scopes_.pop();
            }

            current_return_type_ = nullptr;
            return;
        }

        // impl内でprivateメソッドを呼び出せるように、現在のターゲット型を設定
        current_impl_target_type_ = type_name;

        for (auto& method : impl.methods) {
            scopes_.push();
            current_return_type_ = method->return_type;

            // selfをスコープに追加（target_type型）
            scopes_.current().define("self", impl.target_type, false);

            // パラメータをスコープに追加
            for (const auto& param : method->params) {
                scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
            }

            // 本体をチェック
            for (auto& stmt : method->body) {
                check_statement(*stmt);
            }

            scopes_.pop();
        }
        current_return_type_ = nullptr;
        current_impl_target_type_.clear();  // implチェック完了後にクリア
    }

    // インポートのチェックと解決
    void check_import(ast::ImportDecl& import) {
        // std::io::printlnのインポートを処理
        if (import.path.to_string() == "std::io") {
            // std::io全体のインポート
            for (const auto& item : import.items) {
                if (item.name == "println") {
                    // printlnのオーバーロードを登録
                    register_println();
                } else if (item.name.empty()) {
                    // モジュール全体をインポート
                    register_println();
                }
            }
        } else if (import.path.segments.size() >= 3 && import.path.segments[0] == "std" &&
                   import.path.segments[1] == "io" && import.path.segments[2] == "println") {
            // std::io::printlnの直接インポート
            register_println();
        }
    }

    void register_println() {
        // printlnを特別な組み込み関数として登録
        // 任意の型を受け取れるように、ダミーの型を使用
        scopes_.global().define_function("println", {ast::make_void()}, ast::make_void());
    }

    // 関数チェック
    void check_function(ast::FunctionDecl& func) {
        scopes_.push();
        current_return_type_ = func.return_type;

        // パラメータをスコープに追加
        for (const auto& param : func.params) {
            scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
        }

        // 本体をチェック
        for (auto& stmt : func.body) {
            check_statement(*stmt);
        }

        scopes_.pop();
        current_return_type_ = nullptr;
    }

    // 文のチェック
    void check_statement(ast::Stmt& stmt) {
        debug::tc::log(debug::tc::Id::CheckStmt, "", debug::Level::Trace);

        if (auto* let = stmt.as<ast::LetStmt>()) {
            check_let(*let);
        } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
            check_return(*ret);
        } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
            if (expr_stmt->expr) {
                infer_type(*expr_stmt->expr);
            }
        } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            check_if(*if_stmt);
        } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            check_while(*while_stmt);
        } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            check_for(*for_stmt);
        } else if (auto* block = stmt.as<ast::BlockStmt>()) {
            scopes_.push();
            for (auto& s : block->stmts) {
                check_statement(*s);
            }
            scopes_.pop();
        }
    }

    // let文
    void check_let(ast::LetStmt& let) {
        ast::TypePtr init_type;
        if (let.init) {
            init_type = infer_type(*let.init);
        }

        // コンストラクタ呼び出しのチェック
        if (let.has_ctor_call && let.type) {
            std::string type_name = ast::type_to_string(*let.type);
            // コンストラクタが存在するかチェック
            std::string ctor_name = type_name + "__ctor";
            if (!let.ctor_args.empty()) {
                ctor_name += "_" + std::to_string(let.ctor_args.size());
            }

            // コンストラクタ引数の型チェック
            for (auto& arg : let.ctor_args) {
                infer_type(*arg);  // 引数の型を解決
            }

            debug::tc::log(debug::tc::Id::Resolved, "Constructor call: " + ctor_name,
                           debug::Level::Debug);
        }

        if (let.type) {
            // 明示的型あり
            if (init_type && !types_compatible(let.type, init_type)) {
                error(Span{}, "Type mismatch in variable declaration '" + let.name + "'");
            }
            scopes_.current().define(let.name, let.type, let.is_const);
        } else if (init_type) {
            // 型推論
            let.type = init_type;
            scopes_.current().define(let.name, init_type, let.is_const);
            debug::tc::log(debug::tc::Id::TypeInfer,
                           let.name + " : " + ast::type_to_string(*init_type), debug::Level::Trace);
        } else {
            error(Span{}, "Cannot infer type for '" + let.name + "'");
        }
    }

    // return文
    void check_return(ast::ReturnStmt& ret) {
        if (!current_return_type_)
            return;

        if (ret.value) {
            auto val_type = infer_type(*ret.value);
            if (!types_compatible(current_return_type_, val_type)) {
                error(Span{}, "Return type mismatch");
            }
        } else if (current_return_type_->kind != ast::TypeKind::Void) {
            error(Span{}, "Missing return value");
        }
    }

    // if文
    void check_if(ast::IfStmt& if_stmt) {
        auto cond_type = infer_type(*if_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(Span{}, "Condition must be bool");
        }

        scopes_.push();
        for (auto& s : if_stmt.then_block) {
            check_statement(*s);
        }
        scopes_.pop();

        if (!if_stmt.else_block.empty()) {
            scopes_.push();
            for (auto& s : if_stmt.else_block) {
                check_statement(*s);
            }
            scopes_.pop();
        }
    }

    // while文
    void check_while(ast::WhileStmt& while_stmt) {
        auto cond_type = infer_type(*while_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(Span{}, "Condition must be bool");
        }

        scopes_.push();
        for (auto& s : while_stmt.body) {
            check_statement(*s);
        }
        scopes_.pop();
    }

    // for文
    void check_for(ast::ForStmt& for_stmt) {
        scopes_.push();

        if (for_stmt.init) {
            check_statement(*for_stmt.init);
        }
        if (for_stmt.condition) {
            auto cond_type = infer_type(*for_stmt.condition);
            if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
                error(Span{}, "For condition must be bool");
            }
        }
        if (for_stmt.update) {
            infer_type(*for_stmt.update);
        }

        for (auto& s : for_stmt.body) {
            check_statement(*s);
        }

        scopes_.pop();
    }

    // 式の型推論
    ast::TypePtr infer_type(ast::Expr& expr) {
        debug::tc::log(debug::tc::Id::CheckExpr, "", debug::Level::Trace);

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
        } else {
            inferred_type = ast::make_error();
        }

        // 推論した型を式に設定
        if (inferred_type && !expr.type) {
            expr.type = inferred_type;
        } else if (!expr.type) {
            expr.type = ast::make_error();
        }

        return expr.type;
    }

    // リテラル
    ast::TypePtr infer_literal(ast::LiteralExpr& lit) {
        if (lit.is_null())
            return ast::make_void();
        if (lit.is_bool())
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);
        if (lit.is_int())
            return ast::make_int();
        if (lit.is_float())
            return ast::make_double();
        if (lit.is_char())
            return ast::make_char();
        if (lit.is_string())
            return ast::make_string();
        return ast::make_error();
    }

    // 識別子
    ast::TypePtr infer_ident(ast::IdentExpr& ident) {
        auto sym = scopes_.current().lookup(ident.name);
        if (!sym) {
            error(Span{}, "Undefined variable '" + ident.name + "'");
            return ast::make_error();
        }
        debug::tc::log(debug::tc::Id::Resolved,
                       ident.name + " : " + ast::type_to_string(*sym->type), debug::Level::Trace);
        return sym->type;
    }

    // 二項演算
    ast::TypePtr infer_binary(ast::BinaryExpr& binary) {
        auto ltype = infer_type(*binary.left);
        auto rtype = infer_type(*binary.right);

        if (!ltype || !rtype)
            return ast::make_error();

        switch (binary.op) {
            // 比較演算子 → bool
            case ast::BinaryOp::Eq:
            case ast::BinaryOp::Ne:
            case ast::BinaryOp::Lt:
            case ast::BinaryOp::Gt:
            case ast::BinaryOp::Le:
            case ast::BinaryOp::Ge:
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);

            // 論理演算子 → bool
            case ast::BinaryOp::And:
            case ast::BinaryOp::Or:
                if (ltype->kind != ast::TypeKind::Bool || rtype->kind != ast::TypeKind::Bool) {
                    error(Span{}, "Logical operators require bool operands");
                }
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);

            // 代入 → 左辺の型
            case ast::BinaryOp::Assign:
            case ast::BinaryOp::AddAssign:
            case ast::BinaryOp::SubAssign:
            case ast::BinaryOp::MulAssign:
            case ast::BinaryOp::DivAssign:
                if (!types_compatible(ltype, rtype)) {
                    error(Span{}, "Assignment type mismatch");
                }
                return ltype;

            // 加算演算子 - 文字列連結もサポート
            case ast::BinaryOp::Add:
                // 少なくとも一方が文字列の場合、文字列連結として扱う
                if (ltype->kind == ast::TypeKind::String || rtype->kind == ast::TypeKind::String) {
                    return ast::make_string();
                }
                // 両方が数値の場合は通常の加算
                if (ltype->is_numeric() && rtype->is_numeric()) {
                    return common_type(ltype, rtype);
                }
                error(Span{}, "Add operator requires numeric operands or string concatenation");
                return ast::make_error();

            // その他の算術演算子 → 共通型
            default:
                if (!ltype->is_numeric() || !rtype->is_numeric()) {
                    error(Span{}, "Arithmetic operators require numeric operands");
                    return ast::make_error();
                }
                return common_type(ltype, rtype);
        }
    }

    // 単項演算
    ast::TypePtr infer_unary(ast::UnaryExpr& unary) {
        auto otype = infer_type(*unary.operand);
        if (!otype)
            return ast::make_error();

        switch (unary.op) {
            case ast::UnaryOp::Neg:
                if (!otype->is_numeric()) {
                    error(Span{}, "Negation requires numeric operand");
                }
                return otype;
            case ast::UnaryOp::Not:
                if (otype->kind != ast::TypeKind::Bool) {
                    error(Span{}, "Logical not requires bool operand");
                }
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);
            case ast::UnaryOp::BitNot:
                if (!otype->is_integer()) {
                    error(Span{}, "Bitwise not requires integer operand");
                }
                return otype;
            case ast::UnaryOp::Deref:
                if (otype->kind != ast::TypeKind::Pointer) {
                    error(Span{}, "Cannot dereference non-pointer");
                    return ast::make_error();
                }
                return otype->element_type;
            case ast::UnaryOp::AddrOf:
                return ast::make_pointer(otype);
            case ast::UnaryOp::PreInc:
            case ast::UnaryOp::PreDec:
            case ast::UnaryOp::PostInc:
            case ast::UnaryOp::PostDec:
                if (!otype->is_numeric()) {
                    error(Span{}, "Increment/decrement requires numeric operand");
                }
                return otype;
        }
        return ast::make_error();
    }

    // 関数呼び出し
    ast::TypePtr infer_call(ast::CallExpr& call) {
        // 呼び出し先の型を取得
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            // 組み込み関数の特別処理
            if (ident->name == "println" || ident->name == "print") {
                // println/print は特別扱い（可変長引数をサポート）

                // 引数の型チェック（すべての型を受け入れる）
                for (auto& arg : call.args) {
                    infer_type(*arg);
                }

                // printlnは改行あり、printは改行なし
                return ast::make_void();
            }

            // 通常の関数はシンボルテーブルから検索
            auto sym = scopes_.current().lookup(ident->name);
            if (!sym || !sym->is_function) {
                error(Span{}, "'" + ident->name + "' is not a function");
                return ast::make_error();
            }

            // 通常の関数の引数チェック
            if (call.args.size() != sym->param_types.size()) {
                error(Span{}, "Function '" + ident->name + "' expects " +
                                  std::to_string(sym->param_types.size()) + " arguments, got " +
                                  std::to_string(call.args.size()));
            } else {
                for (size_t i = 0; i < call.args.size(); ++i) {
                    auto arg_type = infer_type(*call.args[i]);
                    if (!types_compatible(sym->param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*sym->param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(Span{}, "Argument type mismatch in call to '" + ident->name +
                                          "': expected " + expected + ", got " + actual);
                    }
                }
            }

            return sym->return_type;
        }

        return ast::make_error();
    }

    // メンバアクセス / メソッド呼び出し
    ast::TypePtr infer_member(ast::MemberExpr& member) {
        auto obj_type = infer_type(*member.object);
        if (!obj_type) {
            return ast::make_error();
        }

        // 型名を取得（構造体・プリミティブ両対応）
        std::string type_name = ast::type_to_string(*obj_type);

        // メソッド呼び出しの場合（全ての型で共通処理）
        if (member.is_method_call) {
            auto it = type_methods_.find(type_name);
            if (it != type_methods_.end()) {
                auto method_it = it->second.find(member.member);
                if (method_it != it->second.end()) {
                    const auto& method_info = method_it->second;

                    // privateメソッドのアクセスチェック
                    if (method_info.visibility == ast::Visibility::Private) {
                        // impl内（current_impl_target_type_が設定されている）からの呼び出しのみ許可
                        if (current_impl_target_type_.empty() ||
                            current_impl_target_type_ != type_name) {
                            error(Span{}, "Cannot call private method '" + member.member +
                                              "' from outside impl block of '" + type_name + "'");
                            return ast::make_error();
                        }
                    }

                    // 引数の型チェック
                    if (member.args.size() != method_info.param_types.size()) {
                        error(Span{}, "Method '" + member.member + "' expects " +
                                          std::to_string(method_info.param_types.size()) +
                                          " arguments, got " + std::to_string(member.args.size()));
                    } else {
                        for (size_t i = 0; i < member.args.size(); ++i) {
                            auto arg_type = infer_type(*member.args[i]);
                            if (!types_compatible(method_info.param_types[i], arg_type)) {
                                std::string expected =
                                    ast::type_to_string(*method_info.param_types[i]);
                                std::string actual = ast::type_to_string(*arg_type);
                                error(Span{}, "Argument type mismatch in method call '" +
                                                  member.member + "': expected " + expected +
                                                  ", got " + actual);
                            }
                        }
                    }

                    debug::tc::log(debug::tc::Id::Resolved,
                                   type_name + "." + member.member +
                                       "() : " + ast::type_to_string(*method_info.return_type),
                                   debug::Level::Debug);
                    return method_info.return_type;
                }
            }
            error(Span{}, "Unknown method '" + member.member + "' for type '" + type_name + "'");
            return ast::make_error();
        }

        // フィールドアクセスの場合（構造体のみ）
        if (obj_type->kind == ast::TypeKind::Struct) {
            const ast::StructDecl* struct_decl = get_struct(type_name);
            if (struct_decl) {
                for (const auto& field : struct_decl->fields) {
                    if (field.name == member.member) {
                        debug::tc::log(debug::tc::Id::Resolved,
                                       type_name + "." + member.member + " : " +
                                           ast::type_to_string(*field.type),
                                       debug::Level::Trace);
                        return field.type;
                    }
                }
                error(Span{},
                      "Unknown field '" + member.member + "' in struct '" + type_name + "'");
            } else {
                error(Span{}, "Unknown struct type '" + type_name + "'");
            }
        } else {
            error(Span{}, "Field access on non-struct type '" + type_name + "'");
        }

        return ast::make_error();
    }

    // 型互換性チェック
    bool types_compatible(ast::TypePtr a, ast::TypePtr b) {
        if (!a || !b)
            return false;
        if (a->kind == ast::TypeKind::Error || b->kind == ast::TypeKind::Error)
            return true;

        // 同じ型
        if (a->kind == b->kind) {
            if (a->kind == ast::TypeKind::Struct) {
                return a->name == b->name;
            }
            return true;
        }

        // 数値型間の暗黙変換
        if (a->is_numeric() && b->is_numeric()) {
            return true;
        }

        // 構造体のdefaultメンバとの互換性チェック
        // a（左辺）が構造体でb（右辺）がプリミティブの場合：w = 20
        if (a->kind == ast::TypeKind::Struct) {
            auto default_type = get_default_member_type(a->name);
            if (default_type && types_compatible(default_type, b)) {
                return true;
            }
        }
        // a（左辺）がプリミティブでb（右辺）が構造体の場合：int x = w
        if (b->kind == ast::TypeKind::Struct) {
            auto default_type = get_default_member_type(b->name);
            if (default_type && types_compatible(a, default_type)) {
                return true;
            }
        }

        return false;
    }

    // 共通型の計算
    ast::TypePtr common_type(ast::TypePtr a, ast::TypePtr b) {
        if (a->kind == b->kind)
            return a;

        // float > int
        if (a->is_floating() || b->is_floating()) {
            return a->kind == ast::TypeKind::Double || b->kind == ast::TypeKind::Double
                       ? ast::make_double()
                       : ast::make_float();
        }

        // より大きい整数型
        auto a_info = a->info();
        auto b_info = b->info();
        return a_info.size >= b_info.size ? a : b;
    }

    void error(Span span, const std::string& msg) {
        debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Error);
        diagnostics_.emplace_back(DiagKind::Error, span, msg);
    }

    ScopeStack scopes_;
    ast::TypePtr current_return_type_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;
};

}  // namespace cm
