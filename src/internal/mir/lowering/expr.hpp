#pragma once

#include "base.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// ユニオン等値比較の正準脱糖（タグ一致＋アクティブ変種のペイロード比較のCFG構築）。
// HIR loweringのbinary Eq/Neと、モノモーフィゼーションの特殊化後正準化（総称derive本体の
// 生Eqがユニオンへ確定したときの書き換え）で共有する。実装はexpr/binary.cpp
LocalId cm_lower_union_equality(bool is_ne, LocalId lhs, LocalId rhs, const hir::TypePtr& lt,
                                const hir::TypePtr& rt, bool l_union, bool r_union,
                                LoweringContext& ctx);

// ============================================================
// 式のLowering
// HIRの各種式をMIRのローカル変数に変換
// ============================================================
class ExprLowering : public MirLoweringBase {
   public:
    // 式のlowering（結果を一時変数に格納して返す）
    LocalId lower_expression(const hir::HirExpr& expr, LoweringContext& ctx);

    // 各式タイプのlowering
    LocalId lower_literal(const hir::HirLiteral& lit, const hir::TypePtr& expr_type,
                          LoweringContext& ctx);
    LocalId lower_var_ref(const hir::HirVarRef& var, const hir::TypePtr& expr_type,
                          LoweringContext& ctx);
    LocalId lower_binary(const hir::HirBinary& bin, LoweringContext& ctx);
    LocalId lower_unary(const hir::HirUnary& un, LoweringContext& ctx);
    LocalId lower_call(const hir::HirCall& call, const hir::TypePtr& result_type,
                       LoweringContext& ctx);

    // builtin別のlowering（該当しなければnullopt）
    std::optional<LocalId> try_lower_println(const hir::HirCall& call,
                                             const hir::TypePtr& result_type, LoweringContext& ctx);
    std::optional<LocalId> try_lower_slice_builtin(const hir::HirCall& call,
                                                   const hir::TypePtr& result_type,
                                                   LoweringContext& ctx);
    LocalId lower_index(const hir::HirIndex& idx, LoweringContext& ctx);
    LocalId lower_member(const hir::HirMember& mem, LoweringContext& ctx);
    LocalId lower_ternary(const hir::HirTernary& ternary, const hir::TypePtr& expr_type,
                          LoweringContext& ctx);
    LocalId lower_struct_literal(const hir::HirStructLiteral& lit, const hir::TypePtr& expr_type,
                                 LoweringContext& ctx);
    LocalId lower_array_literal(const hir::HirArrayLiteral& lit, const hir::TypePtr& expected_type,
                                LoweringContext& ctx);
    LocalId lower_cast(const hir::HirCast& cast, LoweringContext& ctx);
    LocalId lower_enum_construct(const hir::HirEnumConstruct& ec, LoweringContext& ctx);
    LocalId lower_enum_payload(const hir::HirEnumPayload& ep, LoweringContext& ctx);

    // スライスリテラルの実体化（cm_slice_new確保+slice_elem_dispatch表引きの要素pushループ。実装はexpr/materialize.cpp）。
    // let初期化・構造体リテラルフィールドの配列リテラル→スライス経路が共有する。destを渡すとそこへ、省略時はslice_typeの一時へ格納する。
    // 空のelementsは容量0の空スライス確保になる（スライス型letの無初期化と同形）
    LocalId materialize_slice_literal(const std::vector<hir::HirExprPtr>& elements,
                                      const hir::TypePtr& slice_type, LoweringContext& ctx,
                                      std::optional<MirPlace> dest = std::nullopt);

    // 唯一の場所化API（type-resolution-simplification 領域2）。
    // 変数・メンバ・添字・デリファレンスの任意チェーンからMirPlaceを構築し、代入左辺値・メソッドレシーバ・参照取得のすべてで共用する。
    // スライスヘッダ降下（内側スライス要素はcm_slice_get_subslice_refで参照降下、それ以外の要素はcodegenがヘッダ経由で解決するIndex投影）とtypedef解決・ジェネリックフィールド型置換はここにのみ存在させる
    bool lower_place(const hir::HirExpr* expr, LoweringContext& ctx, MirPlace& out_place,
                     hir::TypePtr& out_type);

    // メンバアクセスからMirPlaceを取得（lower_placeのHirMember枝。HirMember&しか持たない既存呼び出し元のための入口）
    bool get_member_place(const hir::HirMember& mem, LoweringContext& ctx, MirPlace& out_place,
                          hir::TypePtr& out_type);

    // メソッドレシーバの場所化（H10）。実体はlower_placeへの委譲
    bool resolve_receiver_place(const hir::HirExpr* expr, LoweringContext& ctx, MirPlace& out_place,
                                hir::TypePtr& out_type);

    // 文字列補間の処理
    LocalId process_string_interpolation(const std::string& format_str,
                                         const std::vector<LocalId>& args, LoweringContext& ctx);

    // println/print用の特別処理
    LocalId handle_println_interpolation(const hir::HirCall& call, LoweringContext& ctx);

    // フォーマット文字列から名前付きプレースホルダを抽出
    std::pair<std::vector<std::string>, std::string> extract_named_placeholders(
        const std::string& format_str, LoweringContext& ctx);

    // 補間プレースホルダを値ローカルへ解決（脱糖済み部分式が無い場合の識別子直接参照）
    LocalId resolve_interp_placeholder(const std::string& content, LoweringContext& ctx);

    // 脱糖済みの補間部分式（HirLiteral.interp_parts）から各プレースホルダを値ローカルへ解決する（第4段b）
    std::vector<LocalId> lower_interp_arg_values(const hir::HirLiteral& lit,
                                                 const std::vector<std::string>& var_names,
                                                 LoweringContext& ctx);

   protected:
    // HIR二項演算子をMIRに変換
    MirBinaryOp convert_binary_op(hir::HirBinaryOp op);

    // HIR単項演算子をMIRに変換
    MirUnaryOp convert_unary_op(hir::HirUnaryOp op);

    // 値を文字列に変換するヘルパー
    LocalId convert_to_string(LocalId value, const hir::TypePtr& type, LoweringContext& ctx);

    // lower_binaryの腕抽出ヘルパー: 文字列連結チェーンの平坦化（3要素以上のみ。非該当ならnullopt）
    std::optional<LocalId> try_lower_string_concat_chain(const hir::HirBinary& bin,
                                                         LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: 代入演算の処理
    LocalId lower_assign(const hir::HirBinary& bin, LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: 代入RHS配列リテラルの直接要素書き込み（非該当ならnullopt）
    std::optional<LocalId> try_lower_assign_array_literal_unroll(const hir::HirBinary& bin,
                                                                 LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: 代入左辺値のMirPlace構築（再帰）
    bool build_assign_lvalue_place(const hir::HirExpr* expr, MirPlace& place,
                                   hir::TypePtr& current_type, LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: AND/OR短絡評価
    LocalId lower_logical_and(const hir::HirBinary& bin, LoweringContext& ctx);
    LocalId lower_logical_or(const hir::HirBinary& bin, LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: 構造体演算子の自動実装呼び出し（非該当ならnullopt）
    std::optional<LocalId> try_lower_struct_eq_op(const hir::HirBinary& bin, LocalId lhs,
                                                  LocalId rhs, LoweringContext& ctx);
    std::optional<LocalId> try_lower_struct_ord_op(const hir::HirBinary& bin, LocalId lhs,
                                                   LocalId rhs, LoweringContext& ctx);
    std::optional<LocalId> try_lower_struct_arith_op(const hir::HirBinary& bin, LocalId lhs,
                                                     LocalId rhs, LoweringContext& ctx);
    // lower_binaryの腕抽出ヘルパー: 2オペランドの文字列連結（非該当ならnullopt）
    std::optional<LocalId> try_lower_string_concat_pair(const hir::HirBinary& bin, LocalId lhs,
                                                        LocalId rhs, LoweringContext& ctx);
};

}  // namespace cm::mir