#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================
// TypeChecker メインクラス定義
// ============================================================

#include "base.hpp"
#include "internal/base/messages/message_ids.hpp"

namespace cm {

class TypeChecker {
   public:
    TypeChecker();

    // プログラム全体をチェック
    bool check(ast::Program& program);

    // 構造体定義を登録
    void register_struct(const std::string& name, const ast::StructDecl& decl);

    // 構造体定義を取得
    const ast::StructDecl* get_struct(const std::string& name) const;

    // 構造体のdefaultメンバの型を取得（なければnullptr）
    ast::TypePtr get_default_member_type(const std::string& struct_name) const;

    // 構造体のdefaultメンバの名前を取得（なければ空文字列）
    std::string get_default_member_name(const std::string& struct_name) const;

    // 診断情報
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const;

    // 自動実装情報を取得（HIR/MIR生成で使用）
    bool has_auto_impl(const std::string& struct_name, const std::string& iface_name) const;

    // 構造体定義を取得（外部から使用）
    const std::unordered_map<std::string, const ast::StructDecl*>& get_struct_defs() const {
        return struct_defs_;
    }

    // Lint警告の有効/無効を設定
    void set_enable_lint_warnings(bool enable) { enable_lint_warnings_ = enable; }

    // M3段階3: 非constポインタへのconst基点&式の束縛を警告
    void warn_addr_of_const_into_mutable_ptr(const ast::TypePtr& dest_type, const ast::Expr* init);

    // 命名規則チェック（--strict）の有効/無効を設定
    void set_enable_naming_check(bool enable) { enable_naming_check_ = enable; }

   private:
    // ============================================================
    // 宣言の登録・チェック (decl.cpp)
    // ============================================================
    void register_declaration(ast::Decl& decl);
    void check_declaration(ast::Decl& decl);
    void register_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace);
    void check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace);
    void register_impl(ast::ImplDecl& impl);
    void check_impl(ast::ImplDecl& impl);
    void register_enum(ast::EnumDecl& en);
    void register_typedef(ast::TypedefDecl& td);
    void check_import(ast::ImportDecl& import);
    void check_function(ast::FunctionDecl& func);
    void register_println();
    void register_print();

    // ============================================================
    // 文のチェック (stmt.cpp)
    // ============================================================
    void check_statement(ast::Stmt& stmt);
    void check_let(ast::LetStmt& let);
    void check_return(ast::ReturnStmt& ret);
    void check_if(ast::IfStmt& if_stmt);
    void check_while(ast::WhileStmt& while_stmt);
    void check_for(ast::ForStmt& for_stmt);
    void check_for_in(ast::ForInStmt& for_in);

    // 配列リテラル初期化子の各要素を宣言要素型に対して検査する（ネストした配列リテラルは多次元要素型へ再帰）。var_nameは診断メッセージ用
    void check_array_literal_elements(ast::ArrayLiteralExpr& lit,
                                      const ast::TypePtr& expected_array,
                                      const std::string& var_name);

    // デフォルト引数式が同じ宣言のパラメータを参照していないか検査する（R8。関数・implメソッド・演算子で共用）
    void check_default_param_refs(const std::vector<ast::Param>& params, const Span& span);

    // 属性の検証レジストリ（R7）: 未知・タイポ属性と未実装属性の診断・#[target]名検証・#[deprecated]関数の収集
    void check_attributes(const ast::Program& program);
    void check_attribute_list(const std::vector<ast::AttributeNode>& attrs,
                              const Span& fallback_span);

    // ============================================================
    // 式の型推論 (expr.cpp)
    // ============================================================
    ast::TypePtr infer_type(ast::Expr& expr);

    // 期待型つき式推論の正式API（type-resolution-simplification 領域3）。
    // 値消費サイト（let初期化・代入右辺・return・関数引数・push引数・フィールド/要素）は期待型をここへ渡すだけにし、無名リテラルの型決定はpropagate_literal_expected_typeの1箇所へ集約する
    ast::TypePtr infer_type_expecting(ast::Expr& expr, const ast::TypePtr& expected);
    ast::TypePtr infer_literal(ast::LiteralExpr& lit);
    ast::TypePtr infer_array_literal(ast::ArrayLiteralExpr& lit);
    ast::TypePtr infer_struct_literal(ast::StructLiteralExpr& lit);
    ast::TypePtr infer_ident(ast::IdentExpr& ident);
    ast::TypePtr infer_binary(ast::BinaryExpr& binary);
    ast::TypePtr infer_unary(ast::UnaryExpr& unary);
    ast::TypePtr infer_ternary(ast::TernaryExpr& ternary);
    ast::TypePtr infer_index(ast::IndexExpr& idx);
    ast::TypePtr infer_slice(ast::SliceExpr& slice);
    ast::TypePtr infer_match(ast::MatchExpr& match_expr);
    ast::TypePtr infer_lambda(ast::LambdaExpr& lambda);

    // ============================================================
    // 関数/メソッド呼び出し (call.cpp)
    // ============================================================
    ast::TypePtr infer_call(ast::CallExpr& call);
    ast::TypePtr infer_member(ast::MemberExpr& member);
    ast::TypePtr infer_member_field(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_member_method(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_array_method(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_string_method(ast::MemberExpr& member, ast::TypePtr obj_type);

    // ============================================================
    // ジェネリクス処理 (generic.cpp)
    // ============================================================
    ast::TypePtr infer_generic_call(ast::CallExpr& call, const std::string& func_name,
                                    const std::vector<std::string>& type_params);
    ast::TypePtr substitute_generic_type(ast::TypePtr type,
                                         const std::vector<std::string>& type_params,
                                         const std::vector<ast::TypePtr>& type_args);
    bool check_constraint(const std::string& type_param, const ast::TypePtr& arg_type,
                          const ast::GenericParam& constraint);

    // ============================================================
    // 自動実装 (auto_impl.cpp)
    // ============================================================
    void register_auto_impl(const ast::StructDecl& st, const std::string& iface_name);
    bool validate_derive_field_types(const ast::StructDecl& st, const std::string& iface_name);
    // フィールド型がトレイトのderive対象として未対応なら理由文字列を返す（対応済みなら空。宣言時と特殊化時の検証で共有）
    std::string derive_field_unsupported_reason(const std::string& iface_name,
                                                const ast::TypePtr& t);
    // derive付きジェネリック構造体の特殊化（例: Box<int[]>）で置換後フィールド型を検証する（R21: 無言の誤値・リンク失敗を診断化）
    void validate_derive_instantiation(const ast::StructDecl& st, const ast::TypePtr& type);
    std::set<std::string> validated_derive_instantiations_;  // 特殊化検証の重複診断抑止

    // ============================================================
    // SVプラットフォーム検査（R16）
    // ============================================================
   public:
    // SVプラットフォーム（//! platform: sv）のとき有効化。入力ポートへの代入検査に使う
    void set_sv_platform(bool enabled) { sv_platform_ = enabled; }

    // js/ts系ターゲット（構造的配列lowering）のとき有効化（局所処理調査E系）。
    // 配列高階関数はネイティブ系では要素幅別ランタイムを使うため非スカラ要素を診断で止めるが、js/tsは要素型に依存しないJS配列メソッドへlowerされるため集約・文字列要素も許可する
    void set_structural_array_lowering(bool enabled) { structural_array_lowering_ = enabled; }

   private:
    bool sv_platform_ = false;
    bool structural_array_lowering_ = false;
    bool in_test_function_ = false;  // #[test]関数本体の検査中か（入力ポート駆動を許可）
    std::unordered_set<std::string> sv_input_ports_;  // #[input]属性つきグローバル（SVポート）
    void register_auto_eq_impl(const ast::StructDecl& st);
    void register_auto_ord_impl(const ast::StructDecl& st);
    void register_auto_clone_impl(const ast::StructDecl& st);
    void register_auto_hash_impl(const ast::StructDecl& st);
    void register_auto_debug_impl(const ast::StructDecl& st);
    void register_auto_display_impl(const ast::StructDecl& st);
    void register_auto_css_impl(const ast::StructDecl& st);
    void register_builtin_interfaces();
    void register_builtin_types();  // Result<T, E>, Option<T> 組み込み型

    // ============================================================
    // ユーティリティ (utils.cpp)
    // ============================================================
    ast::TypePtr resolve_typedef(ast::TypePtr type);
    // typeof(式)型（__typeof__）を被演算式の推論型へ解決する。ポインタ/参照/配列の要素側も再帰解決する。typeofでなければそのまま返す
    ast::TypePtr resolve_typeof(const ast::TypePtr& type);
    bool types_compatible(ast::TypePtr expected, ast::TypePtr actual);
    ast::TypePtr common_type(ast::TypePtr a, ast::TypePtr b);

    // 数値変換の意味論分類（Z5: 暗黙変換と明示キャストの設計整理。utils/conversion.cpp）。
    // 「暗黙可（拡大）／要as（縮小・符号解釈変化）」の判断をこの表1箇所から導く
    enum class NumericConversion {
        NotNumeric,  // どちらかが数値型でない（本表の対象外）
        Identity,    // 同一型
        Widening,    // 値を保存する拡大（暗黙可）
        Narrowing,   // 情報を失いうる縮小（要as）
        SignChange,  // 符号解釈が変わる変換（要as）
    };
    NumericConversion classify_numeric_conversion(const ast::TypePtr& target,
                                                  const ast::TypePtr& source);
    // 受理サイト（let初期化・代入・return）での縮小/符号変化の診断。
    // 宛先に適合するリテラルは対象外。通常は警告、--strict（check/lint）ではエラーへ昇格する（段階導入）
    void check_numeric_conversion_policy(const ast::TypePtr& target, const ast::TypePtr& source,
                                         const ast::Expr* value_expr, Span span);
    std::vector<std::string> extract_format_variables(const std::string& format_str);

    // 文字列リテラルの補間プレースホルダを一度だけ実ASTへ脱糖する（第4段b）。
    // 以後の推論・HIR/MIR loweringはテキスト再パースせずlit.interp_partsの式を消費する
    void desugar_interpolation_parts(ast::LiteralExpr& lit);
    // 集約型（配列/スライス・構造体・ユニオン・インターフェース）のprint/補間直接整形を診断で停止する（局所処理調査G3。bitベクタは整数として整形されるため対象外）
    void check_print_aggregate(const ast::TypePtr& type, const Span& span);

    // 名前空間内の非修飾型名を「現在の名前空間::名前」として解決する（内側から外側へ探索。解決できた場合は修飾名を返す）
    std::optional<std::string> resolve_in_namespace(const std::string& name) const;

    // 変数参照を検索する。名前空間内の非修飾参照は「現在の名前空間::名前」へフォールバックし、解決できた場合は参照名を修飾名へ書き換えて
    // HIR/コード生成が一貫した名前を見るようにする
    std::optional<Symbol> lookup_var_ident(ast::IdentExpr& ident);
    void error(Span span, const std::string& msg);
    void warning(Span span, const std::string& msg);
    bool type_implements_interface(const std::string& type_name, const std::string& interface_name);
    bool check_type_constraints(const std::string& type_name,
                                const std::vector<std::string>& constraints);
    bool is_valid_type(ast::TypePtr type);
    // メソッド表キーの正準計算（method-resolution-unification）。登録側はtype_to_string(impl.target_type)を正とし、
    // 参照側のジェネリック定義キー再構築（G<T, U>形）はこの1関数を共有する（バイト一致必須の複製を排除）
    std::string generic_def_method_key(const std::string& base_name) const;
    // 特殊化サフィックスの除去（Name<...>・Name__k → Name。enum/ジェネリックのベース名でメソッド表を引く参照側で共有）
    static std::string strip_spec_suffix(const std::string& name);

    // メソッド解決の統一入口（method-resolution-unification）。
    // レシーバ型からメソッド表の検索順（直接名/namespace剥ぎ/値enum名→ジェネリック定義キー→
    // interface表→型パラメータ境界→enum基底名）を1箇所へ畳み、発見時は置換に必要な情報ごと返す。
    // 呼び出しサイト（infer_member・静的呼び出し・for-in）は検索をここへ委譲し、引数検査・注釈だけを担う
    struct MethodResolution {
        const MethodInfo* info = nullptr;
        // 解決に使った表キー（診断・デバッグ用）
        std::string table_key;
        // 戻り値・引数型のジェネリック置換が必要な場合の定義パラメータ名と実引数（不要なら空）
        std::vector<std::string> generic_params;
        std::vector<ast::TypePtr> type_args;
        // どの検索段で解決したか（呼び出しサイトの検査差——private検査はDirectのみ・
        // GenericBoundのGeneric戻り値はレシーバ型へ置換——の分岐に使う）
        enum class Via { Direct, GenericDef, Interface, GenericBound, EnumBase };
        Via via = Via::Direct;
    };
    std::optional<MethodResolution> resolve_method(const ast::TypePtr& recv_type,
                                                   const std::string& method_name);
    // R10: constジェネリックパラメータ（<N: const int>）は実体化未実装のため宣言時に明示診断で拒否する
    void reject_const_generic_params(const std::vector<ast::GenericParam>& params, Span span);

    // リテラル型チェック（typedef HttpMethod = "GET" | "POST" など）
    // 代入先がLiteralUnion型の場合、代入する値が許容リテラルに含まれるかチェック
    bool check_literal_assignment(ast::TypePtr target_type, ast::Expr* init_expr, Span span);

    // コンパイル時定数評価（const強化）
    std::optional<int64_t> evaluate_const_expr(ast::Expr& expr);

    // 配列サイズのsize_param_name解決（const強化）。
    // best_effort=trueのときは、名前がconstとして解決できない場合でも診断を出さず記号名のまま残す
    // （構造体フィールドはbit[WIDTH]のように同struct内の#[sv::param]フィールドを幅に使い、SVバックエンドが[WIDTH-1:0]で出力するため）
    void resolve_array_size(ast::TypePtr& type, bool best_effort = false);

    // 変数変更追跡（const推奨警告用）
    void mark_variable_modified(const std::string& name);
    void check_const_recommendations();

    // 未使用変数チェック (W001)
    void check_unused_variables();

    // 命名規則チェック (L001 naming-convention。--strict時のみ)
    static bool is_snake_case(const std::string& name);
    static bool is_pascal_case(const std::string& name);
    static bool is_upper_snake_case(const std::string& name);
    void check_naming_conventions(ast::Program& program);
    void check_naming_decl(ast::Decl& decl, bool top_level);
    void check_naming_function(ast::FunctionDecl& func);
    void check_naming_stmts(std::vector<ast::StmtPtr>& stmts);
    void report_naming(Span span, i18n::MsgId decl_kind, const std::string& name,
                       i18n::MsgId expected);

    // match式のヘルパー
    void check_match_pattern(ast::MatchPattern* pattern, ast::TypePtr expected_type);
    void check_match_exhaustiveness(ast::MatchExpr& match, ast::TypePtr scrutinee_type);

    // ============================================================
    // メンバ変数
    // ============================================================
    ScopeStack scopes_;
    ast::TypePtr current_return_type_;

    // ループ本体のネスト深度（break/continueのループ外使用診断用。Z4穴3。
    // Cmのswitchは自動breakのため明示break/continueはループ専用）
    int loop_depth_ = 0;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;

    // Lint警告の有効/無効（デフォルト: false = 警告なし）
    bool enable_lint_warnings_ = false;

    // 命名規則チェックの有効/無効（check/lint --strict でのみ有効）
    bool enable_naming_check_ = false;

    // 現在チェック中の文/式のSpan（エラー表示用）
    Span current_span_;

    // 型ごとのメソッド情報 (型名 → メソッド名 → メソッド情報)
    std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>> type_methods_;

    // 現在チェック中のimplのターゲット型（privateメソッド呼び出しチェック用）
    std::string current_impl_target_type_;

    // 現在処理中の名前空間（namespace内の非修飾型名・関数名の解決用。空=トップレベル）
    std::string current_namespace_;

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
    // #[deprecated]が付いた関数名（修飾名含む。呼び出しサイトで警告する。R7）
    std::unordered_set<std::string> deprecated_functions_;

    // ジェネリックenumの登録情報（enum名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_enums_;

    // enum定義のキャッシュ（Tagged Union用）
    std::unordered_map<std::string, const ast::EnumDecl*> enum_defs_;

    // typedef定義のキャッシュ (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, ast::TypePtr> typedef_defs_;

    // ジェネリックコンテキスト（現在処理中のジェネリック関数/構造体用）
    GenericContext generic_context_;

    // ジェネリック関数の登録情報（関数名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_functions_;
    // 本体を持つ非ジェネリック関数の定義済みシグネチャ（重複定義検出用）
    std::unordered_map<std::string, std::string> defined_function_sigs_;

    // マングル名の単一シンボルテーブル（C16: メソッド・自由関数・ctor/dtor・モジュール修飾名の衝突検出）
    struct MangledSymbolInfo {
        std::string origin;  // 由来の表示名（"method Type.name" / "function name" 等）
        std::string sig;     // シグネチャ（同一定義の再登録許容の判定用）
        Span span;
    };
    std::unordered_map<std::string, MangledSymbolInfo> mangled_symbols_;

    // マングル名をシンボルテーブルへ登録し、別由来・別シグネチャの同名があればエラーを発行する
    void register_mangled_symbol(const std::string& name, const std::string& origin,
                                 const std::string& sig, Span span);

    // ジェネリック本体が要求する演算子能力と宣言境界の突き合わせ（L8。実装はgeneric/bounds.cpp）
    void check_generic_operator_bounds(ast::FunctionDecl& func);

    // ジェネリック関数の制約情報（関数名 → GenericParamリスト）
    std::unordered_map<std::string, std::vector<ast::GenericParam>> generic_function_constraints_;

    // ジェネリック構造体の登録情報（構造体名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_structs_;

    // 組み込みインターフェースのジェネリックパラメータ
    std::unordered_map<std::string, std::vector<std::string>> builtin_interface_generic_params_;

    // 自動導出される演算子のマッピング (インターフェース名 → 導出演算子 → 基本演算子)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        builtin_derived_operators_;

    // 自動実装情報 (構造体名 → インターフェース名 → 実装済みフラグ)
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> auto_impl_info_;

    // 変更されたことのある変数（const推奨警告用）
    std::unordered_set<std::string> modified_variables_;

    // キャプチャ付きクロージャを束縛したローカル変数名（関数単位でクリア）。
    // クロージャはラムダリフティング実装で値としては生の関数ポインタしか持たないため、
    // 関数引数・構造体フィールド・スライス要素へ渡すと環境が失われゴミ値になる（V5〜V7）。
    // 恒久対応（{fnptr, env}のファットポインタ化）までは該当箇所を診断付きで拒否する
    std::unordered_set<std::string> closure_vars_;

    // 式がキャプチャ付きクロージャ（ラムダ直書きまたはclosure_vars_の変数参照）か
    bool is_capturing_closure_expr(const ast::Expr& expr) const;

    // リテラル式へ期待型を再帰的に伝播する（W1/X3/X4）。
    // 無名構造体リテラルの型名補完と、配列リテラル（要素の無名リテラル含む）の型注釈を行う
    void propagate_literal_expected_type(ast::Expr& expr, const ast::TypePtr& expected);

    // 宣言された非const変数の情報（名前 → Span）
    std::unordered_map<std::string, Span> non_const_variable_spans_;

    // 初期化済み変数の追跡（初期化前使用チェック用）
    std::unordered_set<std::string> initialized_variables_;

    // 変数を初期化済みとしてマーク
    void mark_variable_initialized(const std::string& name);

    // 初期化前使用をチェック
    void check_uninitialized_use(const std::string& name, Span span);

    // ============================================================
    // Move Semantics - 移動済み変数の追跡（Scopeベース）
    // ============================================================
    // 変数を移動済みとしてマーク（Scope経由）
    void mark_variable_moved(const std::string& name);

    // 移動後の使用をチェック（Scope経由）
    void check_use_after_move(const std::string& name, Span span);
};

}  // namespace cm
