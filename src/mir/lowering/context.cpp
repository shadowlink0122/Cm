// MIR Lowering コンテキスト (context.hpp) の実装

#include "context.hpp"

namespace cm::mir {

// 新しいブロックを作成
BlockId LoweringContext::new_block() {
    BlockId id = func->add_block();
    if (func->name == "main") {
        debug_msg("mir_new_block", "[MIR] Created new block " + std::to_string(id) + " in main");
    }
    return id;
}

// 新しいローカル変数を作成（typedefを解決）
LocalId LoweringContext::new_local(const std::string& name, hir::TypePtr type, bool is_mutable,
                                   bool is_user, bool is_static, bool is_global) {
    auto resolved_type = resolve_typedef(type);
    return func->add_local(name, resolved_type, is_mutable, is_user, is_static, is_global);
}

// 新しい一時変数を作成（typedefを解決）
LocalId LoweringContext::new_temp(hir::TypePtr type) {
    std::string name = "_t" + std::to_string(next_temp_id++);
    auto resolved_type = resolve_typedef(type);
    return func->add_local(name, resolved_type, true, true, false);
}

// 文を現在のブロックに追加
void LoweringContext::push_statement(MirStatementPtr stmt) {
    auto* block = get_current_block();
    if (block) {
        // must{}ブロック内の文は最適化禁止
        if (in_must_block) {
            stmt->no_opt = true;
        }
        // デバッグ: ステートメント追加前の状態
        if (current_block == 0 && stmt->kind == MirStatement::Assign) {
            auto& assign = std::get<MirStatement::AssignData>(stmt->data);
            debug_msg("mir_bb0_stmt",
                      "[MIR] Adding to bb0: assign to local " + std::to_string(assign.place.local) +
                          ", bb0 currently has " + std::to_string(block->statements.size()) +
                          " statements" +
                          ", block ptr: " + std::to_string(reinterpret_cast<uintptr_t>(block)));
        }
        block->add_statement(std::move(stmt));
    }
}

// ターミネータを設定
void LoweringContext::set_terminator(MirTerminatorPtr term) {
    auto* block = get_current_block();
    if (block && !block->terminator) {
        block->set_terminator(std::move(term));
    }
}

// ループコンテキストをプッシュ（forループ用、continueターゲット指定）
void LoweringContext::push_loop(BlockId header, BlockId exit, BlockId continue_target) {
    loop_stack.emplace(header, exit, continue_target);
}

// ループコンテキストをポップ
void LoweringContext::pop_loop() {
    if (!loop_stack.empty()) {
        loop_stack.pop();
    }
}

// enum値を取得
std::optional<int64_t> LoweringContext::get_enum_value(const std::string& enum_name,
                                                       const std::string& member_name) {
    if (!enum_defs)
        return std::nullopt;

    auto enum_it = enum_defs->find(enum_name);
    if (enum_it == enum_defs->end())
        return std::nullopt;

    auto member_it = enum_it->second.find(member_name);
    if (member_it == enum_it->second.end())
        return std::nullopt;

    return member_it->second;
}

// スコープ管理
void LoweringContext::push_scope() {
    scopes.emplace_back();
    defer_stacks.emplace_back();
    destructor_vars.emplace_back();
}

void LoweringContext::pop_scope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
    if (!defer_stacks.empty()) {
        defer_stacks.pop_back();
    }
    if (!destructor_vars.empty()) {
        destructor_vars.pop_back();
    }
}

// 現在のスコープにdefer文を追加
void LoweringContext::add_defer(const hir::HirStmt* stmt) {
    if (!defer_stacks.empty()) {
        defer_stacks.back().push_back(stmt);
    }
}

// 現在のスコープのdefer文を取得（逆順）
std::vector<const hir::HirStmt*> LoweringContext::get_defer_stmts() {
    if (!defer_stacks.empty()) {
        auto stmts = defer_stacks.back();
        std::reverse(stmts.begin(), stmts.end());  // 逆順にする
        return stmts;
    }
    return {};
}

// デストラクタを持つ変数を登録
void LoweringContext::register_destructor_var(LocalId id, const std::string& type_name) {
    if (!destructor_vars.empty()) {
        destructor_vars.back().push_back({id, type_name});
    }
}

// 全スコープのデストラクタ変数を取得（内側から外側へ、逆順）
std::vector<std::pair<LocalId, std::string>> LoweringContext::get_all_destructor_vars() {
    std::vector<std::pair<LocalId, std::string>> result;
    // 内側のスコープから外側へ
    for (auto it = destructor_vars.rbegin(); it != destructor_vars.rend(); ++it) {
        // 各スコープ内では逆順（後から宣言された変数が先にデストラクト）
        for (auto var_it = it->rbegin(); var_it != it->rend(); ++var_it) {
            result.push_back(*var_it);
        }
    }
    return result;
}

// 現在のスコープのデストラクタ変数を取得（逆順）
std::vector<std::pair<LocalId, std::string>> LoweringContext::get_current_scope_destructor_vars() {
    std::vector<std::pair<LocalId, std::string>> result;
    if (!destructor_vars.empty()) {
        auto& current = destructor_vars.back();
        for (auto it = current.rbegin(); it != current.rend(); ++it) {
            result.push_back(*it);
        }
    }
    return result;
}

// 型がデストラクタを持つか確認
bool LoweringContext::has_destructor(const std::string& type_name) const {
    // 直接登録されている場合
    if (types_with_destructor.count(type_name) > 0) {
        return true;
    }

    // ジェネリック型の場合（Vector__TrackedObject等）、元テンプレート名を抽出してチェック
    auto underscore_pos = type_name.find("__");
    if (underscore_pos != std::string::npos) {
        std::string base_template = type_name.substr(0, underscore_pos);
        // Vector<T> の形式で登録されているかチェック
        std::string generic_name = base_template + "<T>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        // Vector<K, V> の形式もチェック
        generic_name = base_template + "<K, V>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        // 単なるベース名でもチェック
        if (types_with_destructor.count(base_template) > 0) {
            return true;
        }
    }

    // ベース名で渡された場合（例：Vector）、ジェネリックテンプレートをチェック
    if (type_name.find('<') == std::string::npos && type_name.find("__") == std::string::npos) {
        std::string generic_name = type_name + "<T>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
        generic_name = type_name + "<K, V>";
        if (types_with_destructor.count(generic_name) > 0) {
            return true;
        }
    }

    return false;
}

// ジェネリック型パラメータを解決（sizeof_for_T用）
hir::TypePtr LoweringContext::resolve_type_param(const std::string& param_name) const {
    auto it = type_param_map.find(param_name);
    if (it != type_param_map.end()) {
        return it->second;
    }
    return nullptr;
}

// 型サイズを計算（sizeof_for_Tマーカー処理用）
int64_t LoweringContext::calculate_type_size(const hir::TypePtr& type) const {
    if (!type)
        return 8;  // デフォルトはポインタサイズ

    switch (type->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return 8;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
        case hir::TypeKind::String:
            return 8;
        case hir::TypeKind::Struct: {
            // 構造体定義を探してサイズを計算
            if (struct_defs && struct_defs->count(type->name)) {
                const auto* st = struct_defs->at(type->name);
                // 各フィールドをポインタサイズで見積もり
                int64_t size = static_cast<int64_t>(st->fields.size()) * 8;
                return size > 0 ? size : 8;
            }
            // マングリング名の場合、ベース名で検索
            if (type->name.find("__") != std::string::npos) {
                std::string base = type->name.substr(0, type->name.find("__"));
                if (struct_defs && struct_defs->count(base)) {
                    const auto* st = struct_defs->at(base);
                    int64_t size = static_cast<int64_t>(st->fields.size()) * 8;
                    return size > 0 ? size : 8;
                }
            }
            return 8;
        }
        case hir::TypeKind::Array:
            if (type->element_type && type->array_size.has_value()) {
                return calculate_type_size(type->element_type) * type->array_size.value();
            }
            return 8;
        default:
            return 8;
    }
}

// 変数を現在のスコープに登録
void LoweringContext::register_variable(const std::string& name, LocalId id) {
    if (!scopes.empty()) {
        scopes.back()[name] = id;
    }
}

// 変数名からLocalIdを解決（スコープチェーンを遡る）
std::optional<LocalId> LoweringContext::resolve_variable(const std::string& name) {
    // 内側のスコープから外側へ向かって検索
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto var_it = it->find(name);
        if (var_it != it->end()) {
            return var_it->second;
        }
    }
    return std::nullopt;
}

// const変数の値を登録
void LoweringContext::register_const_value(const std::string& name, const MirConstant& value) {
    const_values[name] = value;
}

// const変数の値を取得（関数ローカルとグローバルの両方をチェック）
std::optional<MirConstant> LoweringContext::get_const_value(const std::string& name) {
    // まず関数ローカルのconst変数をチェック
    auto it = const_values.find(name);
    if (it != const_values.end()) {
        return it->second;
    }

    // 次にグローバルconst変数をチェック
    if (global_const_values) {
        auto global_it = global_const_values->find(name);
        if (global_it != global_const_values->end()) {
            return global_it->second;
        }
    }

    return std::nullopt;
}

// 構造体のフィールドインデックスを取得
std::optional<FieldId> LoweringContext::get_field_index(const std::string& struct_name,
                                                        const std::string& field_name) {
    if (!struct_defs || struct_defs->find(struct_name) == struct_defs->end()) {
        return std::nullopt;
    }

    const auto* struct_def = struct_defs->at(struct_name);
    for (size_t i = 0; i < struct_def->fields.size(); ++i) {
        if (struct_def->fields[i].name == field_name) {
            return static_cast<FieldId>(i);
        }
    }
    return std::nullopt;
}

// typedefとenumを解決（必要に応じて再帰的に）
hir::TypePtr LoweringContext::resolve_typedef(const hir::TypePtr& type) {
    if (!type) {
        return type;
    }

    // Structタイプの場合、typedef/enum定義を確認
    if (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::TypeAlias) {
        // まずtypedef定義を確認
        if (typedef_defs) {
            if (auto it = typedef_defs->find(type->name); it != typedef_defs->end()) {
                // 再帰的に解決
                return resolve_typedef(it->second);
            }
        }

        // enum定義を確認
        if (enum_defs) {
            if (auto it = enum_defs->find(type->name); it != enum_defs->end()) {
                // Tagged Union enum（ペイロード付き）は__TaggedUnion_構造体として扱う
                if (tagged_union_names && tagged_union_names->count(type->name)) {
                    auto tagged_union_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    tagged_union_type->name = "__TaggedUnion_" + type->name;
                    return tagged_union_type;
                }
                // 通常のenum（値のみ）はintとして扱う
                return hir::make_int();
            }
        }
    }

    // ポインタ型の場合、要素型を再帰的に解決
    if (type->kind == hir::TypeKind::Pointer || type->kind == hir::TypeKind::Reference) {
        auto resolved_elem = resolve_typedef(type->element_type);
        if (resolved_elem != type->element_type) {
            auto result = std::make_shared<hir::Type>(*type);
            result->element_type = resolved_elem;
            return result;
        }
    }

    // 配列型の場合、要素型を再帰的に解決
    if (type->kind == hir::TypeKind::Array) {
        auto resolved_elem = resolve_typedef(type->element_type);
        if (resolved_elem != type->element_type) {
            auto result = std::make_shared<hir::Type>(*type);
            result->element_type = resolved_elem;
            return result;
        }
    }

    // LiteralUnion型の場合、基底型（string/int/double）に変換
    if (type->kind == hir::TypeKind::LiteralUnion) {
        auto* lit_union = static_cast<ast::LiteralUnionType*>(type.get());
        if (lit_union && !lit_union->literals.empty()) {
            const auto& first = lit_union->literals[0].value;
            if (std::holds_alternative<std::string>(first)) {
                return hir::make_string();
            } else if (std::holds_alternative<int64_t>(first)) {
                return hir::make_int();
            } else if (std::holds_alternative<double>(first)) {
                return hir::make_double();
            }
        }
        return hir::make_int();  // フォールバック
    }

    return type;
}

}  // namespace cm::mir
