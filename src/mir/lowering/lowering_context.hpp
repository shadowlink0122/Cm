#pragma once

#include "../../hir/hir_nodes.hpp"
#include "../mir_nodes.hpp"

#include <algorithm>
#include <optional>
#include <stack>
#include <unordered_set>

namespace cm::mir {

// ============================================================
// ループコンテキスト
// break/continue先のブロックを管理
// ============================================================
struct LoopContext {
    BlockId header;      // ループヘッダ（whileループのcontinue先）
    BlockId exit;        // ループ出口（break先）
    BlockId update;      // forループの更新ブロック（forループのcontinue先）
    LocalId update_var;  // for文の更新変数（オプション）

    // whileループ用コンストラクタ（continueはヘッダへ）
    LoopContext(BlockId h, BlockId e)
        : header(h), exit(e), update(h), update_var(static_cast<LocalId>(-1)) {}

    // forループ用コンストラクタ（continueは更新ブロックへ）
    LoopContext(BlockId h, BlockId e, BlockId upd)
        : header(h), exit(e), update(upd), update_var(static_cast<LocalId>(-1)) {}
};

// ============================================================
// Lowering コンテキスト
// 関数のlowering中の状態を管理
// ============================================================
class LoweringContext {
   public:
    MirFunction* func;                   // 現在lowering中の関数
    BlockId current_block = 0;           // 現在のブロック
    LocalId next_temp_id = 1000;         // 次の一時変数ID
    std::stack<LoopContext> loop_stack;  // ループコンテキストのスタック

    // 変数スコープ管理（変数名 → LocalId）
    std::vector<std::unordered_map<std::string, LocalId>> scopes;

    // defer文管理（スコープごとのdefer文のスタック）
    // raw pointerを使用（HirStmtは関数のlowering中は生きているため）
    std::vector<std::vector<const hir::HirStmt*>> defer_stacks;

    // デストラクタを持つ変数の追跡（スコープごと）
    // {LocalId, 型名} のペアを格納
    std::vector<std::vector<std::pair<LocalId, std::string>>> destructor_vars;

    // デストラクタを持つ型のセット
    std::unordered_set<std::string> types_with_destructor;

    // enum値のキャッシュ (EnumName::MemberName -> value) - 親クラスから参照
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>* enum_defs =
        nullptr;

    // typedef定義へのポインタ - 親クラスから参照
    const std::unordered_map<std::string, hir::TypePtr>* typedef_defs = nullptr;

    // 構造体定義へのポインタ - 親クラスから参照
    const std::unordered_map<std::string, const hir::HirStruct*>* struct_defs = nullptr;

    explicit LoweringContext(MirFunction* f) : func(f) {
        // 初期スコープを作成
        push_scope();
    }

    // 新しいブロックを作成
    BlockId new_block() { return func->add_block(); }

    // 現在のブロックを切り替え
    void switch_to_block(BlockId block) { current_block = block; }

    // 現在のブロックを取得
    BasicBlock* get_current_block() { return func->get_block(current_block); }

    // 新しいローカル変数を作成
    LocalId new_local(const std::string& name, hir::TypePtr type, bool is_mutable = true) {
        return func->add_local(name, type, is_mutable, false);
    }

    // 新しい一時変数を作成
    LocalId new_temp(hir::TypePtr type) {
        std::string name = "_t" + std::to_string(next_temp_id++);
        return func->add_local(name, type, true, true);
    }

    // 文を現在のブロックに追加
    void push_statement(MirStatementPtr stmt) {
        auto* block = get_current_block();
        if (block) {
            block->add_statement(std::move(stmt));
        }
    }

    // ターミネータを設定
    void set_terminator(MirTerminatorPtr term) {
        auto* block = get_current_block();
        if (block && !block->terminator) {
            block->set_terminator(std::move(term));
        }
    }

    // ループコンテキストをプッシュ（whileループ用）
    void push_loop(BlockId header, BlockId exit) { loop_stack.emplace(header, exit); }

    // ループコンテキストをプッシュ（forループ用、continueターゲット指定）
    void push_loop(BlockId header, BlockId exit, BlockId continue_target) {
        loop_stack.emplace(header, exit, continue_target);
    }

    // ループコンテキストをポップ
    void pop_loop() {
        if (!loop_stack.empty()) {
            loop_stack.pop();
        }
    }

    // 現在のループコンテキストを取得
    LoopContext* current_loop() { return loop_stack.empty() ? nullptr : &loop_stack.top(); }

    // enum値を取得
    std::optional<int64_t> get_enum_value(const std::string& enum_name,
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
    void push_scope() {
        scopes.emplace_back();
        defer_stacks.emplace_back();
        destructor_vars.emplace_back();
    }

    void pop_scope() {
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
    void add_defer(const hir::HirStmt* stmt) {
        if (!defer_stacks.empty()) {
            defer_stacks.back().push_back(stmt);
        }
    }

    // 現在のスコープのdefer文を取得（逆順）
    std::vector<const hir::HirStmt*> get_defer_stmts() {
        if (!defer_stacks.empty()) {
            auto stmts = defer_stacks.back();
            std::reverse(stmts.begin(), stmts.end());  // 逆順にする
            return stmts;
        }
        return {};
    }

    // デストラクタを持つ変数を登録
    void register_destructor_var(LocalId id, const std::string& type_name) {
        if (!destructor_vars.empty()) {
            destructor_vars.back().push_back({id, type_name});
        }
    }

    // 全スコープのデストラクタ変数を取得（内側から外側へ、逆順）
    std::vector<std::pair<LocalId, std::string>> get_all_destructor_vars() {
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
    std::vector<std::pair<LocalId, std::string>> get_current_scope_destructor_vars() {
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
    bool has_destructor(const std::string& type_name) const {
        return types_with_destructor.count(type_name) > 0;
    }

    // デストラクタを持つ型として登録
    void register_type_with_destructor(const std::string& type_name) {
        types_with_destructor.insert(type_name);
    }

    // 変数を現在のスコープに登録
    void register_variable(const std::string& name, LocalId id) {
        if (!scopes.empty()) {
            scopes.back()[name] = id;
        }
    }

    // 変数名からLocalIdを解決（スコープチェーンを遡る）
    std::optional<LocalId> resolve_variable(const std::string& name) {
        // 内側のスコープから外側へ向かって検索
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto var_it = it->find(name);
            if (var_it != it->end()) {
                return var_it->second;
            }
        }
        return std::nullopt;
    }

    // 構造体のフィールドインデックスを取得
    std::optional<FieldId> get_field_index(const std::string& struct_name,
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

   private:
    // typedefを解決（必要に応じて再帰的に）
    hir::TypePtr resolve_typedef(const hir::TypePtr& type) {
        if (!type || !typedef_defs) {
            return type;
        }

        // Structタイプの場合、typedef定義を確認
        if (type->kind == hir::TypeKind::Struct) {
            if (auto it = typedef_defs->find(type->name); it != typedef_defs->end()) {
                // 再帰的に解決
                return resolve_typedef(it->second);
            }
        }

        return type;
    }
};

}  // namespace cm::mir