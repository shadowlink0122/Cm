#pragma once

#include "../../common/debug/tc.hpp"
#include "../ast/decl.hpp"
#include "../ast/module.hpp"
#include "../parser/parser.hpp"
#include "generic_context.hpp"
#include "scope.hpp"

#include <functional>
#include <regex>
#include <set>

namespace cm {

// ============================================================
// 型チェッカー
// ============================================================
class TypeChecker {
   public:
    TypeChecker() {
        // 組み込み関数は import 時に登録される
        // 組み込みインターフェースを登録
        register_builtin_interfaces();
    }

    // 構造体定義を登録
    void register_struct(const std::string& name, const ast::StructDecl& decl) {
        struct_defs_[name] = &decl;
    }

    // 構造体定義を取得
    const ast::StructDecl* get_struct(const std::string& name) const {
        // まず直接検索
        auto it = struct_defs_.find(name);
        if (it != struct_defs_.end()) {
            return it->second;
        }

        // typedefをチェック
        auto td_it = typedef_defs_.find(name);
        if (td_it != typedef_defs_.end() && td_it->second) {
            // typedefの実際の型名で再検索
            std::string actual_name = td_it->second->name;
            auto struct_it = struct_defs_.find(actual_name);
            if (struct_it != struct_defs_.end()) {
                return struct_it->second;
            }
        }

        return nullptr;
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

    // インターフェース実装情報 (型名 → 実装しているインターフェース名のセット)
    std::unordered_map<std::string, std::unordered_set<std::string>> impl_interfaces_;

    // インターフェース名のセット
    std::unordered_set<std::string> interface_names_;

    // インターフェースのメソッド情報 (インターフェース名 → メソッド名 → メソッド情報)
    std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>> interface_methods_;

    // enum値のキャッシュ (EnumName::MemberName -> value)
    std::unordered_map<std::string, int64_t> enum_values_;

    // enum名のセット
    std::unordered_set<std::string> enum_names_;

    // typedef定義のキャッシュ (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, ast::TypePtr> typedef_defs_;

    // ネストしたnamespaceを再帰的に登録
    void register_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
        std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
        std::string full_namespace =
            parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

        debug::tc::log(debug::tc::Id::Resolved, "Processing namespace: " + full_namespace,
                       debug::Level::Debug);

        // namespace内の各宣言を処理
        for (auto& inner_decl : mod.declarations) {
            if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
                register_namespace(*nested_mod, full_namespace);
            } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
                std::string original_name = func->name;
                func->name = full_namespace + "::" + original_name;
                register_declaration(*inner_decl);
                func->name = original_name;
            } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
                std::string original_name = st->name;
                st->name = full_namespace + "::" + original_name;
                register_declaration(*inner_decl);
                st->name = original_name;
            } else {
                register_declaration(*inner_decl);
            }
        }
    }

    // ネストしたnamespaceを再帰的にチェック
    void check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
        std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
        std::string full_namespace =
            parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

        debug::tc::log(debug::tc::Id::Resolved, "Checking namespace: " + full_namespace,
                       debug::Level::Debug);

        // namespace内の各宣言をチェック
        for (auto& inner_decl : mod.declarations) {
            if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
                check_namespace(*nested_mod, full_namespace);
            } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
                std::string original_name = func->name;
                func->name = full_namespace + "::" + original_name;
                check_declaration(*inner_decl);
                func->name = original_name;
            } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
                std::string original_name = st->name;
                st->name = full_namespace + "::" + original_name;
                check_declaration(*inner_decl);
                st->name = original_name;
            } else {
                check_declaration(*inner_decl);
            }
        }
    }

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
        // ModuleDecl/Namespaceの処理
        if (auto* mod = decl.as<ast::ModuleDecl>()) {
            register_namespace(*mod, "");
            return;
        }

        if (auto* func = decl.as<ast::FunctionDecl>()) {
            // ジェネリックパラメータがある場合は記録
            if (!func->generic_params.empty()) {
                generic_functions_[func->name] = func->generic_params;
                // 制約情報も保存
                generic_function_constraints_[func->name] = func->generic_params_v2;
                debug::tc::log(debug::tc::Id::Resolved,
                               "Generic function: " + func->name + " with " +
                                   std::to_string(func->generic_params.size()) + " type params",
                               debug::Level::Debug);
            }

            std::vector<ast::TypePtr> param_types;
            size_t required_params = 0;
            for (const auto& p : func->params) {
                param_types.push_back(p.type);
                if (!p.default_value) {
                    required_params++;
                }
            }
            scopes_.global().define_function(func->name, std::move(param_types), func->return_type,
                                             required_params);
        } else if (auto* st = decl.as<ast::StructDecl>()) {
            // ジェネリックパラメータがある場合は記録
            if (!st->generic_params.empty()) {
                generic_structs_[st->name] = st->generic_params;
                debug::tc::log(debug::tc::Id::Resolved,
                               "Generic struct: " + st->name + " with " +
                                   std::to_string(st->generic_params.size()) + " type params",
                               debug::Level::Debug);
            }

            // 構造体を型として登録
            scopes_.global().define(st->name, ast::make_named(st->name));
            // 構造体定義を保存（フィールドアクセス用）
            register_struct(st->name, *st);

            // with キーワードで指定された自動実装を登録
            for (const auto& iface_name : st->auto_impls) {
                register_auto_impl(*st, iface_name);
            }
        } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
            // インターフェースを登録
            interface_names_.insert(iface->name);
            scopes_.global().define(iface->name, ast::make_named(iface->name));

            // インターフェースのメソッド情報を保存
            for (const auto& method : iface->methods) {
                MethodInfo info;
                info.return_type = method.return_type;
                for (const auto& param : method.params) {
                    info.param_types.push_back(param.type);
                }
                interface_methods_[iface->name][method.name] = info;
            }

            debug::tc::log(debug::tc::Id::Resolved,
                           "Registering interface: " + iface->name + " with " +
                               std::to_string(iface->methods.size()) + " methods",
                           debug::Level::Debug);
        } else if (auto* en = decl.as<ast::EnumDecl>()) {
            // enum定義を登録
            register_enum(*en);
        } else if (auto* td = decl.as<ast::TypedefDecl>()) {
            // typedef定義を登録
            register_typedef(*td);
        } else if (auto* impl = decl.as<ast::ImplDecl>()) {
            // impl定義からメソッド情報を登録
            register_impl(*impl);
        } else if (auto* extern_block = decl.as<ast::ExternBlockDecl>()) {
            // extern "C" ブロック内の関数を登録
            for (const auto& func : extern_block->declarations) {
                std::vector<ast::TypePtr> param_types;
                for (const auto& p : func->params) {
                    param_types.push_back(p.type);
                }
                scopes_.global().define_function(func->name, std::move(param_types),
                                                 func->return_type);
            }
        }
    }

    // 宣言のチェック（パス2）
    void check_declaration(ast::Decl& decl) {
        debug::tc::log(debug::tc::Id::CheckDecl, "", debug::Level::Trace);

        // ModuleDecl/Namespaceの処理
        if (auto* mod = decl.as<ast::ModuleDecl>()) {
            check_namespace(*mod, "");
            return;
        }

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

        // インターフェース実装を記録
        if (!impl.interface_name.empty()) {
            // impl上書き禁止: 同じ型への同じインターフェース実装を検出
            if (impl_interfaces_[type_name].count(impl.interface_name) > 0) {
                throw std::runtime_error("Duplicate impl: " + type_name + " already implements " +
                                         impl.interface_name);
            }
            impl_interfaces_[type_name].insert(impl.interface_name);
            debug::tc::log(debug::tc::Id::Resolved,
                           type_name + " implements " + impl.interface_name, debug::Level::Debug);
        }

        // メソッド実装の場合
        for (const auto& method : impl.methods) {
            // メソッド名重複禁止: 異なるインターフェースでも同名メソッドを検出
            if (type_methods_[type_name].count(method->name) > 0) {
                throw std::runtime_error("Duplicate method: " + type_name +
                                         " already has method '" + method->name + "'");
            }

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

    // enum定義の登録
    void register_enum(ast::EnumDecl& en) {
        debug::tc::log(debug::tc::Id::Resolved, "Registering enum: " + en.name,
                       debug::Level::Debug);

        // enum名を記録
        enum_names_.insert(en.name);

        // enum型自体をグローバルスコープに登録（int型として扱う）
        scopes_.global().define(en.name, ast::make_int());

        // enum値を登録（EnumName::MemberName -> 整数値）
        for (const auto& member : en.members) {
            std::string full_name = en.name + "::" + member.name;
            int64_t value = member.value.value_or(0);
            enum_values_[full_name] = value;

            // enum値を変数としてグローバルスコープに登録（int型として）
            scopes_.global().define(full_name, ast::make_int());

            debug::tc::log(debug::tc::Id::Resolved,
                           "  " + full_name + " = " + std::to_string(value), debug::Level::Debug);
        }
    }

    // typedef定義の登録
    void register_typedef(ast::TypedefDecl& td) {
        debug::tc::log(debug::tc::Id::Resolved, "Registering typedef: " + td.name,
                       debug::Level::Debug);

        // typedefをグローバルスコープに型として登録
        scopes_.global().define(td.name, td.type);

        // typedefマップに保存（型解決用）
        typedef_defs_[td.name] = td.type;
    }

    // withキーワードで指定された自動実装を登録
    void register_auto_impl(const ast::StructDecl& st, const std::string& iface_name) {
        // 組み込みインターフェースか確認
        if (interface_names_.find(iface_name) == interface_names_.end()) {
            error(Span{}, "Unknown interface '" + iface_name + "' in 'with' clause");
            return;
        }

        std::string struct_name = st.name;

        // インターフェース実装を記録
        impl_interfaces_[struct_name].insert(iface_name);

        debug::tc::log(debug::tc::Id::Resolved,
                       "Auto-implementing " + iface_name + " for " + struct_name,
                       debug::Level::Debug);

        // Eq: 全フィールドの == 比較
        if (iface_name == "Eq") {
            register_auto_eq_impl(st);
        }
        // Ord: 全フィールドの < 比較（辞書順）
        else if (iface_name == "Ord") {
            register_auto_ord_impl(st);
        }
        // Copy: マーカー（特に処理なし）
        else if (iface_name == "Copy") {
            // マーカーインターフェース、コード生成なし
        }
        // Clone: clone() メソッド
        else if (iface_name == "Clone") {
            register_auto_clone_impl(st);
        }
        // Hash: hash() メソッド
        else if (iface_name == "Hash") {
            register_auto_hash_impl(st);
        }
    }

    // Eq自動実装: operator bool ==(StructType other)
    void register_auto_eq_impl(const ast::StructDecl& st) {
        std::string struct_name = st.name;
        auto struct_type = ast::make_named(struct_name);

        // 演算子情報を登録
        MethodInfo eq_op;
        eq_op.name = "==";
        eq_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        eq_op.param_types.push_back(struct_type);
        type_methods_[struct_name]["operator=="] = eq_op;

        // 自動導出される != も登録
        MethodInfo ne_op;
        ne_op.name = "!=";
        ne_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        ne_op.param_types.push_back(struct_type);
        type_methods_[struct_name]["operator!="] = ne_op;

        // 実装情報を保存（HIR/MIR生成で使用）
        auto_impl_info_[struct_name]["Eq"] = true;

        debug::tc::log(debug::tc::Id::Resolved,
                       "  Generated operator== and operator!= for " + struct_name,
                       debug::Level::Debug);
    }

    // Ord自動実装: operator bool <(StructType other)
    void register_auto_ord_impl(const ast::StructDecl& st) {
        std::string struct_name = st.name;
        auto struct_type = ast::make_named(struct_name);

        // < 演算子を登録
        MethodInfo lt_op;
        lt_op.name = "<";
        lt_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        lt_op.param_types.push_back(struct_type);
        type_methods_[struct_name]["operator<"] = lt_op;

        // 自動導出される >, <=, >= も登録
        for (const char* op_name : {"operator>", "operator<=", "operator>="}) {
            MethodInfo derived_op;
            derived_op.name = op_name;
            derived_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            derived_op.param_types.push_back(struct_type);
            type_methods_[struct_name][op_name] = derived_op;
        }

        auto_impl_info_[struct_name]["Ord"] = true;

        debug::tc::log(debug::tc::Id::Resolved,
                       "  Generated operator<, >, <=, >= for " + struct_name, debug::Level::Debug);
    }

    // Clone自動実装: T clone()
    void register_auto_clone_impl(const ast::StructDecl& st) {
        std::string struct_name = st.name;
        auto struct_type = ast::make_named(struct_name);

        MethodInfo clone_method;
        clone_method.name = "clone";
        clone_method.return_type = struct_type;
        type_methods_[struct_name]["clone"] = clone_method;

        // グローバル関数としても登録
        std::string mangled_name = struct_name + "__clone";
        std::vector<ast::TypePtr> param_types;
        param_types.push_back(struct_type);  // self
        scopes_.global().define_function(mangled_name, std::move(param_types), struct_type);

        auto_impl_info_[struct_name]["Clone"] = true;

        debug::tc::log(debug::tc::Id::Resolved, "  Generated clone() for " + struct_name,
                       debug::Level::Debug);
    }

    // Hash自動実装: int hash()
    void register_auto_hash_impl(const ast::StructDecl& st) {
        std::string struct_name = st.name;
        auto struct_type = ast::make_named(struct_name);

        MethodInfo hash_method;
        hash_method.name = "hash";
        hash_method.return_type = ast::make_int();
        type_methods_[struct_name]["hash"] = hash_method;

        // グローバル関数としても登録
        std::string mangled_name = struct_name + "__hash";
        std::vector<ast::TypePtr> param_types;
        param_types.push_back(struct_type);  // self
        scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_int());

        auto_impl_info_[struct_name]["Hash"] = true;

        debug::tc::log(debug::tc::Id::Resolved, "  Generated hash() for " + struct_name,
                       debug::Level::Debug);
    }

    // impl本体のチェック
    void check_impl(ast::ImplDecl& impl) {
        if (!impl.target_type)
            return;

        std::string type_name = ast::type_to_string(*impl.target_type);

        // インターフェース実装の場合、実装情報を記録
        if (!impl.interface_name.empty()) {
            impl_interfaces_[type_name].insert(impl.interface_name);
            debug::tc::log(debug::tc::Id::Resolved,
                           type_name + " implements " + impl.interface_name, debug::Level::Debug);
        }

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

        // ジェネリックコンテキストのセットアップ
        generic_context_.clear();
        if (!func.generic_params.empty()) {
            for (const auto& param : func.generic_params) {
                // TODO: 型制約の解析も必要（例: T: Ord）
                generic_context_.add_type_param(param);
                // 型パラメータを現在のスコープに型として登録
                scopes_.current().define(param, ast::make_named(param));
                debug::tc::log(debug::tc::Id::Resolved, "Added generic type param: " + param,
                               debug::Level::Trace);
            }
        }

        // 戻り値型のtypedef解決（ジェネリック型パラメータも考慮）
        current_return_type_ = resolve_typedef(func.return_type);
        if (generic_context_.has_type_param(ast::type_to_string(*func.return_type))) {
            // 戻り値がジェネリック型パラメータの場合、そのまま使用
            current_return_type_ = func.return_type;
        }

        // パラメータをスコープに追加（typedefとジェネリック型を解決）
        for (const auto& param : func.params) {
            auto resolved_type = resolve_typedef(param.type);
            // ジェネリック型パラメータの場合はそのまま使用
            if (generic_context_.has_type_param(ast::type_to_string(*param.type))) {
                resolved_type = param.type;
            }
            scopes_.current().define(param.name, resolved_type, param.qualifiers.is_const);
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
        } else if (auto* for_in = stmt.as<ast::ForInStmt>()) {
            check_for_in(*for_in);
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
            // 明示的型あり - typedefを解決
            auto resolved_type = resolve_typedef(let.type);
            if (init_type && !types_compatible(resolved_type, init_type)) {
                error(Span{}, "Type mismatch in variable declaration '" + let.name + "'");
            }
            // 解決後の型を設定
            let.type = resolved_type;
            scopes_.current().define(let.name, resolved_type, let.is_const);
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

    // for-in文
    void check_for_in(ast::ForInStmt& for_in) {
        scopes_.push();

        // イテラブルの型をチェック
        auto iterable_type = infer_type(*for_in.iterable);
        if (!iterable_type) {
            error(Span{}, "Cannot infer type of iterable expression");
            scopes_.pop();
            return;
        }

        // イテラブルの要素型を取得
        ast::TypePtr element_type;
        if (iterable_type->kind == ast::TypeKind::Array) {
            element_type = iterable_type->element_type;
        } else {
            error(Span{}, "For-in requires an iterable type (array), got " +
                              ast::type_to_string(*iterable_type));
            scopes_.pop();
            return;
        }

        // ループ変数の型チェック
        if (for_in.var_type) {
            auto resolved_type = resolve_typedef(for_in.var_type);
            if (!types_compatible(resolved_type, element_type)) {
                error(Span{}, "For-in variable type mismatch: expected " +
                                  ast::type_to_string(*element_type) + ", got " +
                                  ast::type_to_string(*resolved_type));
            }
            for_in.var_type = resolved_type;
        } else {
            // 型推論
            for_in.var_type = element_type;
        }

        // ループ変数をスコープに追加
        scopes_.current().define(for_in.var_name, for_in.var_type, false);

        // ボディをチェック
        for (auto& s : for_in.body) {
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
        } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
            inferred_type = infer_ternary(*ternary);
        } else if (auto* idx = expr.as<ast::IndexExpr>()) {
            inferred_type = infer_index(*idx);
        } else if (auto* slice = expr.as<ast::SliceExpr>()) {
            inferred_type = infer_slice(*slice);
        } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
            inferred_type = infer_match(*match_expr);
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
            case ast::BinaryOp::DivAssign: {
                // const変数への代入をチェック
                if (auto* ident = binary.left->as<ast::IdentExpr>()) {
                    auto sym = scopes_.current().lookup(ident->name);
                    if (sym && sym->is_const) {
                        error(binary.left->span,
                              "Cannot assign to const variable '" + ident->name + "'");
                        return ast::make_error();
                    }
                }
                if (!types_compatible(ltype, rtype)) {
                    error(binary.left->span, "Assignment type mismatch");
                }
                return ltype;
            }

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
                // 関数のアドレスを取得する場合、そのまま関数ポインタ型を返す
                if (otype->kind == ast::TypeKind::Function) {
                    return otype;
                }
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

                // 引数の数チェック（最低1個、最大1個）
                if (call.args.empty()) {
                    error(Span{}, "'" + ident->name + "' requires at least 1 argument");
                    return ast::make_error();
                }
                if (call.args.size() > 1) {
                    error(Span{}, "'" + ident->name + "' takes only 1 argument, got " +
                                      std::to_string(call.args.size()));
                    return ast::make_error();
                }

                // 引数の型チェック（すべての型を受け入れる）
                for (auto& arg : call.args) {
                    infer_type(*arg);
                }

                // printlnは改行あり、printは改行なし
                return ast::make_void();
            }

            // ジェネリック関数かチェック
            auto gen_it = generic_functions_.find(ident->name);
            if (gen_it != generic_functions_.end()) {
                // ジェネリック関数の型推論
                return infer_generic_call(call, ident->name, gen_it->second);
            }

            // 構造体のコンストラクタ呼び出しかチェック
            if (get_struct(ident->name) != nullptr) {
                // コンストラクタの引数チェック（将来的にはコンストラクタシグネチャと照合）
                for (auto& arg : call.args) {
                    infer_type(*arg);
                }
                // コンストラクタは構造体型を返す
                return ast::make_named(ident->name);
            }

            // 通常の関数はシンボルテーブルから検索
            auto sym = scopes_.current().lookup(ident->name);
            if (!sym) {
                error(Span{}, "'" + ident->name + "' is not a function");
                return ast::make_error();
            }

            // 関数ポインタ型の変数からの呼び出しをチェック
            if (!sym->is_function && sym->type && sym->type->kind == ast::TypeKind::Function) {
                // 関数ポインタ型の変数
                auto fn_type = sym->type;

                // 引数チェック
                size_t arg_count = call.args.size();
                size_t param_count = fn_type->param_types.size();

                if (arg_count != param_count) {
                    error(Span{}, "Function pointer '" + ident->name + "' expects " +
                                      std::to_string(param_count) + " arguments, got " +
                                      std::to_string(arg_count));
                } else {
                    for (size_t i = 0; i < arg_count; ++i) {
                        auto arg_type = infer_type(*call.args[i]);
                        if (!types_compatible(fn_type->param_types[i], arg_type)) {
                            std::string expected = ast::type_to_string(*fn_type->param_types[i]);
                            std::string actual = ast::type_to_string(*arg_type);
                            error(Span{}, "Argument type mismatch in call to function pointer '" +
                                              ident->name + "': expected " + expected + ", got " +
                                              actual);
                        }
                    }
                }

                return fn_type->return_type ? fn_type->return_type : ast::make_void();
            }

            if (!sym->is_function) {
                error(Span{}, "'" + ident->name + "' is not a function");
                return ast::make_error();
            }

            // 通常の関数の引数チェック（デフォルト引数を考慮）
            size_t arg_count = call.args.size();
            size_t param_count = sym->param_types.size();
            size_t required_count = sym->required_params;

            if (arg_count < required_count || arg_count > param_count) {
                if (required_count == param_count) {
                    error(Span{}, "Function '" + ident->name + "' expects " +
                                      std::to_string(param_count) + " arguments, got " +
                                      std::to_string(arg_count));
                } else {
                    error(Span{}, "Function '" + ident->name + "' expects " +
                                      std::to_string(required_count) + " to " +
                                      std::to_string(param_count) + " arguments, got " +
                                      std::to_string(arg_count));
                }
            } else {
                for (size_t i = 0; i < arg_count; ++i) {
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
            // 配列型のビルトインメソッド
            if (obj_type->kind == ast::TypeKind::Array) {
                if (member.member == "size" || member.member == "len" ||
                    member.member == "length") {
                    if (!member.args.empty()) {
                        error(Span{}, "Array " + member.member + "() takes no arguments");
                    }
                    debug::tc::log(
                        debug::tc::Id::Resolved,
                        "Array builtin: " + type_name + "." + member.member + "() : uint",
                        debug::Level::Debug);
                    return ast::make_uint();
                }
                // indexOf: 要素の位置を検索
                if (member.member == "indexOf") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array indexOf() takes 1 argument");
                    }
                    // 引数の型をチェック（要素型と互換性があること）
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_int();
                }
                // includes/contains: 要素が含まれているか
                if (member.member == "includes" || member.member == "contains") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array " + member.member + "() takes 1 argument");
                    }
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_bool();
                }
                // some: いずれかの要素が条件を満たすか
                if (member.member == "some") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array some() takes 1 predicate function");
                    }
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_bool();
                }
                // every: すべての要素が条件を満たすか
                if (member.member == "every") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array every() takes 1 predicate function");
                    }
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_bool();
                }
                // findIndex: 条件を満たす最初の要素のインデックス
                if (member.member == "findIndex") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array findIndex() takes 1 predicate function");
                    }
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_int();
                }
                // reduce: 畳み込み
                if (member.member == "reduce") {
                    if (member.args.size() < 1 || member.args.size() > 2) {
                        error(Span{}, "Array reduce() takes 1-2 arguments (callback, [initial])");
                    }
                    for (auto& arg : member.args) {
                        infer_type(*arg);
                    }
                    // 戻り値の型は初期値の型（指定がなければint）
                    return ast::make_int();
                }
                // forEach: 各要素に対して関数を実行
                if (member.member == "forEach") {
                    if (member.args.size() != 1) {
                        error(Span{}, "Array forEach() takes 1 callback function");
                    }
                    if (!member.args.empty()) {
                        infer_type(*member.args[0]);
                    }
                    return ast::make_void();
                }
                error(Span{}, "Unknown array method '" + member.member + "'");
                return ast::make_error();
            }

            // 文字列型のビルトインメソッド
            if (obj_type->kind == ast::TypeKind::String) {
                if (member.member == "len" || member.member == "size" ||
                    member.member == "length") {
                    if (!member.args.empty()) {
                        error(Span{}, "String " + member.member + "() takes no arguments");
                    }
                    debug::tc::log(
                        debug::tc::Id::Resolved,
                        "String builtin: " + type_name + "." + member.member + "() : uint",
                        debug::Level::Debug);
                    return ast::make_uint();
                }
                if (member.member == "charAt" || member.member == "at") {
                    if (member.args.size() != 1) {
                        error(Span{}, "String " + member.member + "() takes 1 argument");
                    } else {
                        auto arg_type = infer_type(*member.args[0]);
                        if (!arg_type->is_integer()) {
                            error(Span{}, "String " + member.member + "() index must be integer");
                        }
                    }
                    return ast::make_char();
                }
                if (member.member == "substring" || member.member == "slice") {
                    if (member.args.size() < 1 || member.args.size() > 2) {
                        error(Span{}, "String " + member.member + "() takes 1-2 arguments");
                    } else {
                        for (auto& arg : member.args) {
                            auto arg_type = infer_type(*arg);
                            if (!arg_type->is_integer()) {
                                error(Span{},
                                      "String " + member.member + "() arguments must be integers");
                            }
                        }
                    }
                    return ast::make_string();
                }
                if (member.member == "indexOf") {
                    if (member.args.size() != 1) {
                        error(Span{}, "String indexOf() takes 1 argument");
                    } else {
                        auto arg_type = infer_type(*member.args[0]);
                        if (arg_type->kind != ast::TypeKind::String) {
                            error(Span{}, "String indexOf() argument must be string");
                        }
                    }
                    return ast::make_int();
                }
                if (member.member == "toUpperCase" || member.member == "toLowerCase" ||
                    member.member == "trim") {
                    if (!member.args.empty()) {
                        error(Span{}, "String " + member.member + "() takes no arguments");
                    }
                    return ast::make_string();
                }
                if (member.member == "startsWith" || member.member == "endsWith" ||
                    member.member == "includes" || member.member == "contains") {
                    if (member.args.size() != 1) {
                        error(Span{}, "String " + member.member + "() takes 1 argument");
                    } else {
                        auto arg_type = infer_type(*member.args[0]);
                        if (arg_type->kind != ast::TypeKind::String) {
                            error(Span{}, "String " + member.member + "() argument must be string");
                        }
                    }
                    return ast::make_bool();
                }
                if (member.member == "repeat") {
                    if (member.args.size() != 1) {
                        error(Span{}, "String repeat() takes 1 argument");
                    } else {
                        auto arg_type = infer_type(*member.args[0]);
                        if (!arg_type->is_integer()) {
                            error(Span{}, "String repeat() count must be integer");
                        }
                    }
                    return ast::make_string();
                }
                if (member.member == "replace") {
                    if (member.args.size() != 2) {
                        error(Span{}, "String replace() takes 2 arguments");
                    } else {
                        for (auto& arg : member.args) {
                            auto arg_type = infer_type(*arg);
                            if (arg_type->kind != ast::TypeKind::String) {
                                error(Span{}, "String replace() arguments must be strings");
                            }
                        }
                    }
                    return ast::make_string();
                }
            }

            // ポインタ型はビルトインメソッドを持たない
            if (obj_type->kind == ast::TypeKind::Pointer) {
                error(Span{},
                      "Pointer type does not support method calls. Use (*ptr).method() instead.");
                return ast::make_error();
            }

            // メソッド検索用の型名リストを作成（名前空間付きと名前空間なし）
            std::vector<std::string> type_names_to_search = {type_name};
            // 名前空間付きの場合、名前空間を除去した型名も検索
            size_t last_colon = type_name.rfind("::");
            if (last_colon != std::string::npos) {
                type_names_to_search.push_back(type_name.substr(last_colon + 2));
            }

            // 通常のメソッドを探す（名前空間付き/なし両方を試行）
            for (const auto& search_type : type_names_to_search) {
                auto it = type_methods_.find(search_type);
                if (it != type_methods_.end()) {
                    auto method_it = it->second.find(member.member);
                    if (method_it != it->second.end()) {
                        const auto& method_info = method_it->second;

                        // privateメソッドのアクセスチェック
                        if (method_info.visibility == ast::Visibility::Private) {
                            // impl内（current_impl_target_type_が設定されている）からの呼び出しのみ許可
                            if (current_impl_target_type_.empty() ||
                                (current_impl_target_type_ != type_name &&
                                 current_impl_target_type_ != search_type)) {
                                error(Span{}, "Cannot call private method '" + member.member +
                                                  "' from outside impl block of '" + type_name +
                                                  "'");
                                return ast::make_error();
                            }
                        }

                        // 引数の型チェック
                        if (member.args.size() != method_info.param_types.size()) {
                            error(Span{}, "Method '" + member.member + "' expects " +
                                              std::to_string(method_info.param_types.size()) +
                                              " arguments, got " +
                                              std::to_string(member.args.size()));
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
            }

            // ジェネリック構造体の場合、ジェネリック版のメソッドを探す
            // 例: Container<int> -> Container<T>
            if (obj_type->kind == ast::TypeKind::Struct && !obj_type->type_args.empty()) {
                // ジェネリック構造体名を構築 (Container<T>)
                std::string generic_type_name = obj_type->name + "<";
                auto gen_it = generic_structs_.find(obj_type->name);
                if (gen_it != generic_structs_.end()) {
                    for (size_t i = 0; i < gen_it->second.size(); ++i) {
                        if (i > 0)
                            generic_type_name += ", ";
                        generic_type_name += gen_it->second[i];
                    }
                    generic_type_name += ">";

                    auto git = type_methods_.find(generic_type_name);
                    if (git != type_methods_.end()) {
                        auto method_it = git->second.find(member.member);
                        if (method_it != git->second.end()) {
                            const auto& method_info = method_it->second;

                            // 戻り値型のジェネリックパラメータを具体型に置換
                            ast::TypePtr return_type = method_info.return_type;
                            if (return_type && gen_it != generic_structs_.end()) {
                                return_type = substitute_generic_type(return_type, gen_it->second,
                                                                      obj_type->type_args);
                            }

                            debug::tc::log(debug::tc::Id::Resolved,
                                           "Generic method: " + type_name + "." + member.member +
                                               "() : " + ast::type_to_string(*return_type),
                                           debug::Level::Debug);
                            return return_type;
                        }
                    }
                }
            }

            // インターフェース型の場合、インターフェースのメソッドを探す
            auto iface_it = interface_methods_.find(type_name);
            if (iface_it != interface_methods_.end()) {
                auto method_it = iface_it->second.find(member.member);
                if (method_it != iface_it->second.end()) {
                    const auto& method_info = method_it->second;

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
                                   "Interface " + type_name + "." + member.member +
                                       "() : " + ast::type_to_string(*method_info.return_type),
                                   debug::Level::Debug);
                    return method_info.return_type;
                }
            }

            // ジェネリック型パラメータの場合、メソッド呼び出しを許可（制約チェックは将来実装）
            if (generic_context_.has_type_param(type_name)) {
                debug::tc::log(debug::tc::Id::Resolved,
                               "Generic type param " + type_name + "." + member.member +
                                   "() - assuming valid (constraint check deferred)",
                               debug::Level::Debug);
                // 戻り値の型は不明なので、void を返す（モノモーフィゼーション時に解決）
                return ast::make_void();
            }

            error(Span{}, "Unknown method '" + member.member + "' for type '" + type_name + "'");
            return ast::make_error();
        }

        // フィールドアクセスの場合（構造体のみ）
        if (obj_type->kind == ast::TypeKind::Struct) {
            // ジェネリック型の場合、基底名を抽出
            std::string base_type_name = obj_type->name;
            const ast::StructDecl* struct_decl = get_struct(base_type_name);
            if (struct_decl) {
                for (const auto& field : struct_decl->fields) {
                    if (field.name == member.member) {
                        auto resolved_field_type = resolve_typedef(field.type);

                        // ジェネリック型の場合、フィールドの型を具体型に置換
                        if (!obj_type->type_args.empty() && !struct_decl->generic_params.empty()) {
                            resolved_field_type = substitute_generic_type(
                                resolved_field_type, struct_decl->generic_params,
                                obj_type->type_args);
                        }

                        debug::tc::log(debug::tc::Id::Resolved,
                                       type_name + "." + member.member + " : " +
                                           ast::type_to_string(*resolved_field_type),
                                       debug::Level::Trace);
                        return resolved_field_type;
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

    // 三項演算子
    ast::TypePtr infer_ternary(ast::TernaryExpr& ternary) {
        // 条件式の型チェック
        auto cond_type = infer_type(*ternary.condition);
        if (!cond_type ||
            (cond_type->kind != ast::TypeKind::Bool && cond_type->kind != ast::TypeKind::Int)) {
            error(Span{}, "Ternary condition must be bool or int");
        }

        // then部とelse部の型を推論
        auto then_type = infer_type(*ternary.then_expr);
        auto else_type = infer_type(*ternary.else_expr);

        // 型が互換性があるか確認
        if (!types_compatible(then_type, else_type)) {
            error(Span{}, "Ternary branches have incompatible types");
        }

        // then部の型を返す（両方が互換性がある場合）
        return then_type;
    }

    // match式
    ast::TypePtr infer_match(ast::MatchExpr& match) {
        // scrutinee（マッチ対象）の型を推論
        auto scrutinee_type = infer_type(*match.scrutinee);
        if (!scrutinee_type) {
            error(Span{}, "Cannot infer type of match scrutinee");
            return ast::make_error();
        }

        // 各アームの型をチェック
        ast::TypePtr result_type = nullptr;
        for (auto& arm : match.arms) {
            // パターンの型チェック
            check_match_pattern(arm.pattern.get(), scrutinee_type);

            // ガード条件がある場合、bool型であることを確認
            if (arm.guard) {
                auto guard_type = infer_type(*arm.guard);
                if (!guard_type || guard_type->kind != ast::TypeKind::Bool) {
                    error(Span{}, "Match guard must be a boolean expression");
                }
            }

            // アームの本体の型を推論
            auto body_type = infer_type(*arm.body);

            // 最初のアームの型を結果型とする
            if (!result_type) {
                result_type = body_type;
            } else if (!types_compatible(result_type, body_type)) {
                error(Span{}, "Match arms have incompatible types");
            }
        }

        if (!result_type) {
            error(Span{}, "Match expression has no arms");
            return ast::make_error();
        }

        // 網羅性チェック
        check_match_exhaustiveness(match, scrutinee_type);

        return result_type;
    }

    // match式の網羅性チェック
    void check_match_exhaustiveness(ast::MatchExpr& match, ast::TypePtr scrutinee_type) {
        if (!scrutinee_type)
            return;

        bool has_wildcard = false;
        bool has_variable_binding = false;
        std::set<std::string> covered_values;
        std::string detected_enum_name;  // enum型パターンから検出されたenum名

        for (const auto& arm : match.arms) {
            if (!arm.pattern)
                continue;

            switch (arm.pattern->kind) {
                case ast::MatchPatternKind::Wildcard:
                    has_wildcard = true;
                    break;
                case ast::MatchPatternKind::Variable:
                    // 変数束縛パターンは全ての値にマッチ（ガードがない場合）
                    if (!arm.guard) {
                        has_variable_binding = true;
                    }
                    break;
                case ast::MatchPatternKind::Literal:
                    if (arm.pattern->value) {
                        if (auto* lit = arm.pattern->value->as<ast::LiteralExpr>()) {
                            if (lit->is_int()) {
                                covered_values.insert(
                                    std::to_string(std::get<int64_t>(lit->value)));
                            } else if (lit->is_bool()) {
                                covered_values.insert(std::get<bool>(lit->value) ? "true"
                                                                                 : "false");
                            }
                        }
                    }
                    break;
                case ast::MatchPatternKind::EnumVariant:
                    if (arm.pattern->value) {
                        if (auto* ident = arm.pattern->value->as<ast::IdentExpr>()) {
                            covered_values.insert(ident->name);
                            // EnumName::Member から EnumName を抽出
                            auto pos = ident->name.find("::");
                            if (pos != std::string::npos) {
                                std::string enum_name = ident->name.substr(0, pos);
                                if (enum_names_.count(enum_name)) {
                                    detected_enum_name = enum_name;
                                }
                            }
                        }
                    }
                    break;
            }
        }

        // ワイルドカードまたは変数束縛があれば網羅的
        if (has_wildcard || has_variable_binding) {
            return;
        }

        // bool型の場合
        if (scrutinee_type->kind == ast::TypeKind::Bool) {
            if (!covered_values.count("true") || !covered_values.count("false")) {
                error(Span{},
                      "Non-exhaustive match: missing 'true' or 'false' pattern (or add '_' "
                      "wildcard)");
            }
            return;
        }

        // enum型パターンが検出された場合
        if (!detected_enum_name.empty()) {
            // enum値を取得
            std::set<std::string> all_variants;
            for (const auto& [key, value] : enum_values_) {
                if (key.find(detected_enum_name + "::") == 0) {
                    all_variants.insert(key);
                }
            }

            // 全てのバリアントがカバーされているか確認
            for (const auto& variant : all_variants) {
                if (!covered_values.count(variant)) {
                    error(Span{}, "Non-exhaustive match: missing pattern for '" + variant +
                                      "' (or add '_' wildcard)");
                    return;
                }
            }
            return;
        }

        // scrutinee_typeがenum名として登録されている場合
        std::string type_name = ast::type_to_string(*scrutinee_type);
        if (enum_names_.count(type_name)) {
            // enum値を取得
            std::set<std::string> all_variants;
            for (const auto& [key, value] : enum_values_) {
                if (key.find(type_name + "::") == 0) {
                    all_variants.insert(key);
                }
            }

            // 全てのバリアントがカバーされているか確認
            for (const auto& variant : all_variants) {
                if (!covered_values.count(variant)) {
                    error(Span{}, "Non-exhaustive match: missing pattern for '" + variant +
                                      "' (or add '_' wildcard)");
                    return;
                }
            }
            return;
        }

        // 整数型などの場合、ワイルドカードが必要
        if (scrutinee_type->is_integer()) {
            error(Span{}, "Non-exhaustive match: integer patterns require a '_' wildcard pattern");
        }
    }

    // matchパターンの型チェック
    void check_match_pattern(ast::MatchPattern* pattern, ast::TypePtr expected_type) {
        if (!pattern)
            return;

        switch (pattern->kind) {
            case ast::MatchPatternKind::Literal:
                if (pattern->value) {
                    auto lit_type = infer_type(*pattern->value);
                    if (!types_compatible(lit_type, expected_type)) {
                        error(Span{}, "Pattern type does not match scrutinee type");
                    }
                }
                break;

            case ast::MatchPatternKind::Variable:
                // 変数束縛パターン: 変数にscrutineeの型を設定
                if (!pattern->var_name.empty()) {
                    scopes_.current().define(pattern->var_name, expected_type);
                }
                break;

            case ast::MatchPatternKind::EnumVariant:
                if (pattern->value) {
                    auto enum_type = infer_type(*pattern->value);
                    if (!types_compatible(enum_type, expected_type)) {
                        error(Span{}, "Enum pattern type does not match scrutinee type");
                    }
                }
                break;

            case ast::MatchPatternKind::Wildcard:
                // ワイルドカードは任意の型にマッチ
                break;
        }
    }

    // 配列インデックスアクセス
    ast::TypePtr infer_index(ast::IndexExpr& idx) {
        // オブジェクトの型を推論
        auto obj_type = infer_type(*idx.object);

        // インデックスの型を推論（整数であること）
        auto index_type = infer_type(*idx.index);
        if (!index_type || !index_type->is_integer()) {
            error(Span{}, "Array index must be an integer type");
        }

        if (!obj_type) {
            return ast::make_error();
        }

        // 配列型の場合、要素型を返す
        if (obj_type->kind == ast::TypeKind::Array) {
            return obj_type->element_type;
        }

        // ポインタ型の場合、参照先の型を返す
        if (obj_type->kind == ast::TypeKind::Pointer) {
            return obj_type->element_type;
        }

        // 文字列型の場合、charを返す
        if (obj_type->kind == ast::TypeKind::String) {
            return ast::make_char();
        }

        error(Span{}, "Index access on non-array type");
        return ast::make_error();
    }

    // スライス式: arr[start:end:step]
    ast::TypePtr infer_slice(ast::SliceExpr& slice) {
        // オブジェクトの型を推論
        auto obj_type = infer_type(*slice.object);

        // インデックスの型をチェック（整数であること）
        if (slice.start) {
            auto start_type = infer_type(*slice.start);
            if (!start_type || !start_type->is_integer()) {
                error(Span{}, "Slice start index must be an integer type");
            }
        }
        if (slice.end) {
            auto end_type = infer_type(*slice.end);
            if (!end_type || !end_type->is_integer()) {
                error(Span{}, "Slice end index must be an integer type");
            }
        }
        if (slice.step) {
            auto step_type = infer_type(*slice.step);
            if (!step_type || !step_type->is_integer()) {
                error(Span{}, "Slice step must be an integer type");
            }
        }

        if (!obj_type) {
            return ast::make_error();
        }

        // 配列型の場合、動的配列型を返す（スライスは新しい配列を作成）
        if (obj_type->kind == ast::TypeKind::Array) {
            // スライスの結果は動的配列型
            return ast::make_array(obj_type->element_type, std::nullopt);
        }

        // 文字列型の場合、文字列を返す
        if (obj_type->kind == ast::TypeKind::String) {
            return ast::make_string();
        }

        error(Span{}, "Slice access on non-array/string type");
        return ast::make_error();
    }

    // 型互換性チェック
    bool types_compatible(ast::TypePtr a, ast::TypePtr b) {
        if (!a || !b)
            return false;
        if (a->kind == ast::TypeKind::Error || b->kind == ast::TypeKind::Error)
            return true;

        // ジェネリック型パラメータのチェック
        std::string a_name = ast::type_to_string(*a);
        std::string b_name = ast::type_to_string(*b);
        if (generic_context_.has_type_param(a_name) || generic_context_.has_type_param(b_name)) {
            // ジェネリック型パラメータの場合、同じ名前なら互換
            // TODO: より洗練された型推論が必要
            return a_name == b_name;
        }

        // typedefを展開（名前付き型の場合）
        a = resolve_typedef(a);
        b = resolve_typedef(b);

        // インターフェース互換性チェック（同じ型チェックより先に行う）
        // a（期待される型）がインターフェース名で、b（実際の型）がそれを実装した構造体の場合
        // 注: パーサーは全ての名前付き型をStructとして作成するため、名前でインターフェースか判定
        if (a->kind == ast::TypeKind::Struct && interface_names_.count(a->name)) {
            // aはインターフェース名
            if (b->kind == ast::TypeKind::Struct && !interface_names_.count(b->name)) {
                // bは構造体（インターフェースでない）
                auto it = impl_interfaces_.find(b->name);
                if (it != impl_interfaces_.end()) {
                    if (it->second.count(a->name)) {
                        return true;  // bはaインターフェースを実装している
                    }
                }
            }
        }

        // 同じ型
        if (a->kind == b->kind) {
            if (a->kind == ast::TypeKind::Struct) {
                return a->name == b->name;
            }
            if (a->kind == ast::TypeKind::Interface) {
                return a->name == b->name;
            }
            // 関数ポインタ型の互換性チェック
            if (a->kind == ast::TypeKind::Function) {
                // 戻り値型のチェック
                if (!types_compatible(a->return_type, b->return_type)) {
                    return false;
                }
                // パラメータ数のチェック
                if (a->param_types.size() != b->param_types.size()) {
                    return false;
                }
                // 各パラメータ型のチェック
                for (size_t i = 0; i < a->param_types.size(); ++i) {
                    if (!types_compatible(a->param_types[i], b->param_types[i])) {
                        return false;
                    }
                }
                return true;
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

        // 配列→ポインタ暗黙変換 (array decay)
        // int[N] → int* への変換を許可
        if (a->kind == ast::TypeKind::Pointer && b->kind == ast::TypeKind::Array) {
            if (a->element_type && b->element_type) {
                return types_compatible(a->element_type, b->element_type);
            }
        }

        return false;
    }

    // 型が指定されたインターフェースを実装しているかチェック
    bool type_implements_interface(const std::string& type_name,
                                   const std::string& interface_name) {
        // プリミティブ型の組み込みインターフェース
        // Ord: 比較可能な型（数値型、文字型）
        if (interface_name == "Ord") {
            if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
                type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
                type_name == "long" || type_name == "ulong" || type_name == "float" ||
                type_name == "double" || type_name == "char") {
                return true;
            }
        }

        // Eq: 等価比較可能な型
        if (interface_name == "Eq") {
            if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
                type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
                type_name == "long" || type_name == "ulong" || type_name == "float" ||
                type_name == "double" || type_name == "char" || type_name == "bool" ||
                type_name == "string") {
                return true;
            }
        }

        // Clone: コピー可能な型（今のところすべてのプリミティブ型）
        if (interface_name == "Clone") {
            if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
                type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
                type_name == "long" || type_name == "ulong" || type_name == "float" ||
                type_name == "double" || type_name == "char" || type_name == "bool" ||
                type_name == "string") {
                return true;
            }
        }

        // 明示的なimpl実装をチェック
        auto it = impl_interfaces_.find(type_name);
        if (it != impl_interfaces_.end()) {
            if (it->second.count(interface_name)) {
                return true;
            }
        }

        return false;
    }

    // 型制約をチェック（ジェネリック関数呼び出し時）
    bool check_type_constraints(const std::string& type_name,
                                const std::vector<std::string>& constraints) {
        for (const auto& constraint : constraints) {
            if (!type_implements_interface(type_name, constraint)) {
                return false;
            }
        }
        return true;
    }

    // ジェネリック型パラメータを具体型に置換
    ast::TypePtr substitute_generic_type(ast::TypePtr type,
                                         const std::vector<std::string>& generic_params,
                                         const std::vector<ast::TypePtr>& type_args) {
        if (!type)
            return type;

        // 型パラメータ名に一致するか確認
        std::string type_name = ast::type_to_string(*type);
        for (size_t i = 0; i < generic_params.size() && i < type_args.size(); ++i) {
            if (type_name == generic_params[i]) {
                return type_args[i];
            }
        }

        // 複合型の場合は再帰的に処理
        if (type->kind == ast::TypeKind::Pointer || type->kind == ast::TypeKind::Reference) {
            auto new_type = std::make_shared<ast::Type>(type->kind);
            new_type->element_type =
                substitute_generic_type(type->element_type, generic_params, type_args);
            return new_type;
        }

        if (type->kind == ast::TypeKind::Array) {
            auto new_type = std::make_shared<ast::Type>(ast::TypeKind::Array);
            new_type->element_type =
                substitute_generic_type(type->element_type, generic_params, type_args);
            new_type->array_size = type->array_size;
            return new_type;
        }

        // ジェネリック構造体の場合
        if (type->kind == ast::TypeKind::Struct && !type->type_args.empty()) {
            auto new_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            new_type->name = type->name;
            for (const auto& arg : type->type_args) {
                new_type->type_args.push_back(
                    substitute_generic_type(arg, generic_params, type_args));
            }
            return new_type;
        }

        return type;
    }

    // typedef解決（名前付き型→実際の型）
    ast::TypePtr resolve_typedef(ast::TypePtr type) {
        if (!type)
            return type;

        // 名前付き型（Struct/Interface/Generic）の場合
        if (type->kind == ast::TypeKind::Struct || type->kind == ast::TypeKind::Interface ||
            type->kind == ast::TypeKind::Generic) {
            // enum名の場合はint型として解決
            if (enum_names_.count(type->name)) {
                return ast::make_int();
            }

            // typedefに登録されていれば解決
            auto it = typedef_defs_.find(type->name);
            if (it != typedef_defs_.end()) {
                return it->second;
            }
        }

        return type;
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

    // ジェネリック関数呼び出しの型推論
    ast::TypePtr infer_generic_call(ast::CallExpr& call, const std::string& func_name,
                                    const std::vector<std::string>& type_params) {
        // 引数の型を推論
        std::vector<ast::TypePtr> arg_types;
        for (auto& arg : call.args) {
            arg_types.push_back(infer_type(*arg));
        }

        // シンボルテーブルから関数情報を取得
        auto sym = scopes_.current().lookup(func_name);
        if (!sym || !sym->is_function) {
            error(Span{}, "'" + func_name + "' is not a function");
            return ast::make_error();
        }

        // 引数の数チェック（デフォルト引数を考慮）
        size_t arg_count = call.args.size();
        size_t param_count = sym->param_types.size();
        size_t required_count = sym->required_params;

        if (arg_count < required_count || arg_count > param_count) {
            if (required_count == param_count) {
                error(Span{}, "Generic function '" + func_name + "' expects " +
                                  std::to_string(param_count) + " arguments, got " +
                                  std::to_string(arg_count));
            } else {
                error(Span{}, "Generic function '" + func_name + "' expects " +
                                  std::to_string(required_count) + " to " +
                                  std::to_string(param_count) + " arguments, got " +
                                  std::to_string(arg_count));
            }
            return ast::make_error();
        }

        // 型パラメータをセットに変換
        std::set<std::string> type_param_set(type_params.begin(), type_params.end());

        // 型パラメータの推論
        std::unordered_map<std::string, ast::TypePtr> inferred_types;
        for (size_t i = 0; i < arg_types.size(); ++i) {
            auto& param_type = sym->param_types[i];
            auto& arg_type = arg_types[i];

            if (param_type && arg_type) {
                std::string param_str = ast::type_to_string(*param_type);
                // パラメータ型が型パラメータの場合
                if (type_param_set.count(param_str)) {
                    auto it = inferred_types.find(param_str);
                    if (it == inferred_types.end()) {
                        inferred_types[param_str] = arg_type;
                        debug::tc::log(
                            debug::tc::Id::Resolved,
                            "Inferred " + param_str + " = " + ast::type_to_string(*arg_type),
                            debug::Level::Debug);
                    }
                }
                // パラメータがジェネリック構造体の場合（例：Box<T>）
                else if (param_type->kind == ast::TypeKind::Struct &&
                         !param_type->type_args.empty() &&
                         arg_type->kind == ast::TypeKind::Struct &&
                         param_type->name == arg_type->name) {
                    // 型引数を一致させて推論
                    for (size_t j = 0;
                         j < param_type->type_args.size() && j < arg_type->type_args.size(); ++j) {
                        std::string type_arg_str = ast::type_to_string(*param_type->type_args[j]);
                        if (type_param_set.count(type_arg_str)) {
                            auto it = inferred_types.find(type_arg_str);
                            if (it == inferred_types.end()) {
                                inferred_types[type_arg_str] = arg_type->type_args[j];
                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Inferred " + type_arg_str + " = " +
                                                   ast::type_to_string(*arg_type->type_args[j]),
                                               debug::Level::Debug);
                            }
                        }
                    }
                }
            }
        }

        // 推論された型情報を保存（後でモノモーフィゼーションで使用）
        if (!inferred_types.empty()) {
            call.inferred_type_args = inferred_types;

            // 順序付き型引数を設定
            for (const auto& param_name : type_params) {
                auto it = inferred_types.find(param_name);
                if (it != inferred_types.end()) {
                    call.ordered_type_args.push_back(it->second);
                }
            }
        }

        // 型制約チェック
        auto constraint_it = generic_function_constraints_.find(func_name);
        if (constraint_it != generic_function_constraints_.end()) {
            for (const auto& generic_param : constraint_it->second) {
                if (!generic_param.constraints.empty()) {
                    // この型パラメータに対する推論された型を取得
                    auto inferred_it = inferred_types.find(generic_param.name);
                    if (inferred_it != inferred_types.end()) {
                        std::string actual_type = ast::type_to_string(*inferred_it->second);
                        // 全ての制約をチェック
                        if (!check_type_constraints(actual_type, generic_param.constraints)) {
                            std::string constraints_str;
                            for (size_t i = 0; i < generic_param.constraints.size(); ++i) {
                                if (i > 0)
                                    constraints_str += " + ";
                                constraints_str += generic_param.constraints[i];
                            }
                            error(Span{}, "Type '" + actual_type +
                                              "' does not satisfy constraint '" + constraints_str +
                                              "' for type parameter '" + generic_param.name +
                                              "' in function '" + func_name + "'");
                        }
                    }
                }
            }
        }

        // 戻り値の型を置換
        std::string return_type_str = ast::type_to_string(*sym->return_type);
        auto it = inferred_types.find(return_type_str);
        if (it != inferred_types.end()) {
            // 戻り値型がジェネリック型パラメータの場合、推論された型を返す
            debug::tc::log(
                debug::tc::Id::Resolved,
                "Generic call " + func_name + " returns " + ast::type_to_string(*it->second),
                debug::Level::Debug);

            return it->second;
        }

        // 戻り値型がジェネリック構造体の場合（例：Option<T>）、型引数を置換
        if (sym->return_type && sym->return_type->kind == ast::TypeKind::Struct &&
            !sym->return_type->type_args.empty()) {
            bool needs_substitution = false;
            std::vector<ast::TypePtr> new_type_args;

            for (const auto& type_arg : sym->return_type->type_args) {
                if (type_arg) {
                    std::string arg_str = ast::type_to_string(*type_arg);
                    auto subst_it = inferred_types.find(arg_str);
                    if (subst_it != inferred_types.end()) {
                        new_type_args.push_back(subst_it->second);
                        needs_substitution = true;
                    } else {
                        new_type_args.push_back(type_arg);
                    }
                }
            }

            if (needs_substitution) {
                auto new_return_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
                new_return_type->name = sym->return_type->name;
                new_return_type->type_args = std::move(new_type_args);

                debug::tc::log(debug::tc::Id::Resolved,
                               "Generic call " + func_name + " returns " +
                                   ast::type_to_string(*new_return_type),
                               debug::Level::Debug);

                return new_return_type;
            }
        }

        // 戻り値型がジェネリックでない場合はそのまま
        return sym->return_type;
    }

    void error(Span span, const std::string& msg) {
        debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Error);
        diagnostics_.emplace_back(DiagKind::Error, span, msg);
    }

    ScopeStack scopes_;
    ast::TypePtr current_return_type_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;

    // ジェネリックコンテキスト（現在処理中のジェネリック関数/構造体用）
    GenericContext generic_context_;

    // ジェネリック関数の登録情報（関数名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_functions_;

    // ジェネリック関数の制約情報（関数名 → GenericParamリスト）
    std::unordered_map<std::string, std::vector<ast::GenericParam>> generic_function_constraints_;

    // ジェネリック構造体の登録情報（構造体名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_structs_;

    // 組み込みインターフェースを登録
    void register_builtin_interfaces() {
        // Eq<T> - 等価比較
        // interface Eq<T> { operator bool ==(T other); }
        {
            interface_names_.insert("Eq");
            builtin_interface_generic_params_["Eq"] = {"T"};

            MethodInfo eq_op;
            eq_op.name = "==";
            eq_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            eq_op.param_types.push_back(ast::make_generic_param("T"));
            interface_methods_["Eq"]["=="] = eq_op;

            // != は == から自動導出
            builtin_derived_operators_["Eq"]["!="] = "==";  // a != b => !(a == b)
        }

        // Ord<T> - 順序比較
        // interface Ord<T> { operator bool <(T other); }
        {
            interface_names_.insert("Ord");
            builtin_interface_generic_params_["Ord"] = {"T"};

            MethodInfo lt_op;
            lt_op.name = "<";
            lt_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            lt_op.param_types.push_back(ast::make_generic_param("T"));
            interface_methods_["Ord"]["<"] = lt_op;

            // >, <=, >= は < から自動導出
            builtin_derived_operators_["Ord"][">"] = "<";   // a > b => b < a
            builtin_derived_operators_["Ord"]["<="] = "<";  // a <= b => !(b < a)
            builtin_derived_operators_["Ord"][">="] = "<";  // a >= b => !(a < b)
        }

        // Copy - コピー可能（マーカーインターフェース）
        {
            interface_names_.insert("Copy");
            // マーカーインターフェース（メソッドなし）
        }

        // Clone<T> - 明示的クローン
        // interface Clone<T> { T clone(); }
        {
            interface_names_.insert("Clone");
            builtin_interface_generic_params_["Clone"] = {"T"};

            MethodInfo clone_method;
            clone_method.name = "clone";
            clone_method.return_type = ast::make_generic_param("T");
            interface_methods_["Clone"]["clone"] = clone_method;
        }

        // Hash - ハッシュ値計算
        // interface Hash { int hash(); }
        {
            interface_names_.insert("Hash");

            MethodInfo hash_method;
            hash_method.name = "hash";
            hash_method.return_type = ast::make_int();
            interface_methods_["Hash"]["hash"] = hash_method;
        }

        debug::tc::log(debug::tc::Id::Resolved,
                       "Registered builtin interfaces: Eq, Ord, Copy, Clone, Hash",
                       debug::Level::Debug);
    }

    // 組み込みインターフェースのジェネリックパラメータ
    std::unordered_map<std::string, std::vector<std::string>> builtin_interface_generic_params_;

    // 自動導出される演算子のマッピング (インターフェース名 → 導出演算子 → 基本演算子)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        builtin_derived_operators_;

    // 自動実装情報 (構造体名 → インターフェース名 → 実装済みフラグ)
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> auto_impl_info_;

   public:
    // 自動実装情報を取得（HIR/MIR生成で使用）
    bool has_auto_impl(const std::string& struct_name, const std::string& iface_name) const {
        auto it = auto_impl_info_.find(struct_name);
        if (it != auto_impl_info_.end()) {
            auto iface_it = it->second.find(iface_name);
            if (iface_it != it->second.end()) {
                return iface_it->second;
            }
        }
        return false;
    }

    // 構造体定義を取得（外部から使用）
    const std::unordered_map<std::string, const ast::StructDecl*>& get_struct_defs() const {
        return struct_defs_;
    }
};

}  // namespace cm
