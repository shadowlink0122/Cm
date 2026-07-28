#pragma once

#include "internal/base/debug.hpp"
#include "internal/hir/nodes.hpp"
#include "internal/mir/nodes.hpp"
#include "internal/syntax/ast/typedef.hpp"

#include <algorithm>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

// ============================================================
// ユニオン型のスライス要素サイズ
// codegenのtagged union表現 {tag: i32, payload: [N x i8]} と一致させる
// ============================================================
inline int64_t union_slice_elem_size(const hir::TypePtr& union_type) {
    int64_t payload = 8;  // デフォルト（int/long等の8バイトペイロード）
    if (union_type && !union_type->type_args.empty()) {
        payload = 0;
        for (const auto& variant : union_type->type_args) {
            if (!variant) {
                continue;
            }
            int64_t size = 8;
            switch (variant->kind) {
                case hir::TypeKind::Bool:
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                case hir::TypeKind::Char:
                    size = 1;
                    break;
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                    size = 2;
                    break;
                case hir::TypeKind::Int:
                case hir::TypeKind::UInt:
                case hir::TypeKind::Float:
                case hir::TypeKind::UFloat:
                    size = 4;
                    break;
                default:
                    size = 8;  // long/double/ポインタ/string等
                    break;
            }
            payload = std::max(payload, size);
        }
        payload = std::max<int64_t>(payload, 8);
    }
    return 4 + payload;  // tag(i32) + ペイロード
}

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
    // HIR関数定義（デフォルト引数補完用）
    const std::unordered_map<std::string, const hir::HirFunction*>* hir_func_defs = nullptr;

    // Tagged Union名のセット - 親クラスから参照
    const std::unordered_set<std::string>* tagged_union_names = nullptr;

    // グローバルconst変数の値 - 親クラスから参照
    const std::unordered_map<std::string, MirConstant>* global_const_values = nullptr;

    // must{}ブロック内かどうか（最適化禁止フラグ）
    bool in_must_block = false;

    // ============================================================
    // 文単位一時オブジェクトのトラッキング（C12 dropパス）
    // コンパイラが生成する無名文字列一時（concat・型変換の結果）を文ごとに記録し、
    // 文の評価完了後にエスケープしなかったものをcm_string_freeで解放する
    // ============================================================
    struct StmtTempScope {
        bool active = false;      // 単純文（let/assign/式文）のlowering中のみtrue
        BlockId start_block = 0;  // 文開始時のカレントブロック（スキャン起点）
        size_t start_stmt_index = 0;  // 文開始時のstart_block内statement数
        size_t start_block_count = 0;  // 文開始時のブロック総数（以降のブロックが対象）
        std::vector<LocalId> string_temps;  // 文中で新規確保された文字列一時
        std::vector<LocalId> slice_temps;  // 文中で新規確保されたスライス一時（map/filter結果）
    };
    StmtTempScope stmt_temp_scope;

    // 条件付き実行の腕（三項演算子・短絡評価の右辺）の内側では文スコープへ一時を登録しない。
    // 文末尾の解放地点で未初期化ポインタをfreeする危険があるため、深度>0の間は文スコープ登録を抑止する
    int conditional_expr_depth = 0;

    // 条件腕の一時スコープ（C12）。
    // 腕内で確保され腕内で完結する一時は、腕ブロック内（mergeへの分岐前）で解放する。
    // 腕の結果値は result = copy(値) のUseエスケープとして自然に保護される
    struct ArmTempScope {
        BlockId start_block = 0;
        size_t start_stmt_index = 0;
        size_t start_block_count = 0;
        std::vector<LocalId> string_temps;
        std::vector<LocalId> slice_temps;
    };
    std::vector<ArmTempScope> arm_temp_scopes;

    // 文字列一時を現在のスコープへ登録する（腕スコープ内なら最内の腕、そうでなければ文スコープ）
    void note_string_temp(LocalId id) {
        if (!stmt_temp_scope.active) {
            return;
        }
        if (!arm_temp_scopes.empty()) {
            arm_temp_scopes.back().string_temps.push_back(id);
            return;
        }
        if (conditional_expr_depth == 0) {
            stmt_temp_scope.string_temps.push_back(id);
        }
    }

    // スライス一時（データ所有権を持つ新規確保スライス）を現在のスコープへ登録する
    void note_slice_temp(LocalId id) {
        if (!stmt_temp_scope.active) {
            return;
        }
        if (!arm_temp_scopes.empty()) {
            arm_temp_scopes.back().slice_temps.push_back(id);
            return;
        }
        if (conditional_expr_depth == 0) {
            stmt_temp_scope.slice_temps.push_back(id);
        }
    }

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
    // move済み変数のデストラクタ登録を全スコープから解除する（moved-outの二重解放防止）
    void unregister_destructor_var(LocalId id);
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

    // typedefとenumを解決（必要に応じて再帰的に）
    hir::TypePtr resolve_typedef(const hir::TypePtr& type);

    // LLVMのDataLayout（自然アライメント）と一致する型サイズ/アライメントを計算する
    // スライスのblob要素サイズ算出用（calculate_type_sizeは見積もりでありレイアウト非互換）
    int64_t layout_size(const hir::TypePtr& type) const;
    int64_t layout_align(const hir::TypePtr& type) const;
};

// 腕の値の所有権判定結果（C12三項結果一時）。
// 腕の値がその腕で登録されたfresh一時で、唯一のエスケープ先が結果ローカルの場合に所有権が結果へ移動する
enum class ArmValueOwnership { None, String, Slice };

// 条件腕の一時スコープの開始/終了（C12。実装はstmt/temp_drop.cpp）。
// beginは文スコープがアクティブなときだけ腕スコープを積み、積んだかどうかを返す。
// endはbeginの戻り値を受け取り、積んだ場合のみ腕範囲をエスケープ解析して非エスケープ一時を腕内で解放する。
// 腕の終端（mergeへの分岐）を設定する前に呼ぶこと
bool begin_arm_temp_scope(LoweringContext& ctx);
void end_arm_temp_scope(LoweringContext& ctx, bool pushed);

// 所有権判定付きの腕スコープ終了（三項演算子用）。
// arm_valueが腕内登録のfresh一時で、腕内での使用が読み取り・非保持呼び出しと
// result_localへのUseコピー1回だけの場合、所有権がresultへ移動したとみなし種別を返す
ArmValueOwnership end_arm_temp_scope(LoweringContext& ctx, bool pushed, LocalId arm_value,
                                     LocalId result_local);

}  // namespace cm::mir