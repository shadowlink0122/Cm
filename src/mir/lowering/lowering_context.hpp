#pragma once

#include "../../hir/hir_nodes.hpp"
#include "../mir_nodes.hpp"

#include <algorithm>
#include <optional>
#include <stack>

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

    // enum値のキャッシュ (EnumName::MemberName -> value) - 親クラスから参照
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>* enum_defs =
        nullptr;

    // typedef定義へのポインタ - 親クラスから参照
    const std::unordered_map<std::string, hir::TypePtr>* typedef_defs = nullptr;

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
        return func->add_local(name, resolve_typedef(type), is_mutable, false);
    }

    // 新しい一時変数を作成
    LocalId new_temp(hir::TypePtr type) {
        std::string name = "_t" + std::to_string(next_temp_id++);
        return func->add_local(name, resolve_typedef(type), true, true);
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

    // typedefを解決
    hir::TypePtr resolve_typedef(hir::TypePtr type) {
        if (!type || !typedef_defs)
            return type;

        // TypeAlias型の場合、typedefかチェック
        if (type->kind == hir::TypeKind::TypeAlias || type->kind == hir::TypeKind::Struct) {
            auto it = typedef_defs->find(type->name);
            if (it != typedef_defs->end()) {
                // 再帰的に解決
                return resolve_typedef(it->second);
            }
        }

        return type;
    }

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
    }

    void pop_scope() {
        if (!scopes.empty()) {
            scopes.pop_back();
        }
        if (!defer_stacks.empty()) {
            defer_stacks.pop_back();
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
};

}  // namespace cm::mir