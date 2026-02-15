#pragma once

#include "../../common/debug.hpp"
#include "../../frontend/ast/typedef.hpp"
#include "../../hir/nodes.hpp"
#include "../nodes.hpp"

#include <algorithm>
#include <optional>
#include <stack>
#include <unordered_map>
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
    std::vector<std::vector<const hir::HirStmt*>> defer_stacks;

    // デストラクタを持つ変数の追跡（スコープごと）
    std::vector<std::vector<std::pair<LocalId, std::string>>> destructor_vars;

    // デストラクタを持つ型のセット
    std::unordered_set<std::string> types_with_destructor;

    // enum値のキャッシュ - 親クラスから参照
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>* enum_defs =
        nullptr;

    // typedef定義へのポインタ - 親クラスから参照
    const std::unordered_map<std::string, hir::TypePtr>* typedef_defs = nullptr;

    // 構造体定義へのポインタ - 親クラスから参照
    const std::unordered_map<std::string, const hir::HirStruct*>* struct_defs = nullptr;

    // インターフェース名のセット - 親クラスから参照
    const std::unordered_set<std::string>* interface_names = nullptr;

    // Tagged Union名のセット - 親クラスから参照
    const std::unordered_set<std::string>* tagged_union_names = nullptr;

    // グローバルconst変数の値 - 親クラスから参照
    const std::unordered_map<std::string, MirConstant>* global_const_values = nullptr;

    // must{}ブロック内かどうか（最適化禁止フラグ）
    bool in_must_block = false;

    // const変数の値のキャッシュ
    std::unordered_map<std::string, MirConstant> const_values;

    // enumペイロードキャッシュ（Tagged Union用）
    std::optional<LocalId> last_enum_payload_local;

    // ジェネリック型パラメータのマッピング（T → 具体型）
    std::unordered_map<std::string, hir::TypePtr> type_param_map;

    // コンストラクタ
    explicit LoweringContext(MirFunction* f) : func(f) { push_scope(); }

    // ブロック管理
    BlockId new_block();
    void switch_to_block(BlockId block) { current_block = block; }
    BasicBlock* get_current_block() { return func->get_block(current_block); }

    // ローカル変数管理
    LocalId new_local(const std::string& name, hir::TypePtr type, bool is_mutable = true,
                      bool is_user = true, bool is_static = false, bool is_global = false);
    LocalId new_temp(hir::TypePtr type);

    // 文・ターミネータ管理
    void push_statement(MirStatementPtr stmt);
    void set_terminator(MirTerminatorPtr term);

    // ループコンテキスト管理
    void push_loop(BlockId header, BlockId exit) { loop_stack.emplace(header, exit); }
    void push_loop(BlockId header, BlockId exit, BlockId continue_target);
    void pop_loop();
    LoopContext* current_loop() { return loop_stack.empty() ? nullptr : &loop_stack.top(); }

    // enum値を取得
    std::optional<int64_t> get_enum_value(const std::string& enum_name,
                                          const std::string& member_name);

    // スコープ管理
    void push_scope();
    void pop_scope();

    // defer文管理
    void add_defer(const hir::HirStmt* stmt);
    std::vector<const hir::HirStmt*> get_defer_stmts();

    // デストラクタ変数管理
    void register_destructor_var(LocalId id, const std::string& type_name);
    std::vector<std::pair<LocalId, std::string>> get_all_destructor_vars();
    std::vector<std::pair<LocalId, std::string>> get_current_scope_destructor_vars();

    // デストラクタ型管理
    bool has_destructor(const std::string& type_name) const;
    void register_type_with_destructor(const std::string& type_name) {
        types_with_destructor.insert(type_name);
    }
    const std::unordered_set<std::string>& get_types_with_destructor() const {
        return types_with_destructor;
    }

    // 型パラメータ解決
    hir::TypePtr resolve_type_param(const std::string& param_name) const;
    int64_t calculate_type_size(const hir::TypePtr& type) const;

    // 変数管理
    void register_variable(const std::string& name, LocalId id);
    std::optional<LocalId> resolve_variable(const std::string& name);

    // const変数管理
    void register_const_value(const std::string& name, const MirConstant& value);
    std::optional<MirConstant> get_const_value(const std::string& name);

    // 構造体フィールド管理
    std::optional<FieldId> get_field_index(const std::string& struct_name,
                                           const std::string& field_name);

   private:
    // typedefとenumを解決（必要に応じて再帰的に）
    hir::TypePtr resolve_typedef(const hir::TypePtr& type);
};

}  // namespace cm::mir