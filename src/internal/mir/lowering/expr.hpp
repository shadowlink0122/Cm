#pragma once

#include "base.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

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

    // 補間プレースホルダの内容を式としてパースしMIRへ降下する（{a + b} 等、変数名パターンで扱えない一般式のフォールバック。
    //  パースに失敗した場合は std::nullopt を返す）
    std::optional<LocalId> lower_interp_expression(const std::string& content,
                                                   LoweringContext& ctx);

    // 補間プレースホルダを値ローカルへ解決（識別子直接参照 or 式パーサ経由）
    LocalId resolve_interp_placeholder(const std::string& content, LoweringContext& ctx);

   protected:
    // HIR二項演算子をMIRに変換
    MirBinaryOp convert_binary_op(hir::HirBinaryOp op);

    // HIR単項演算子をMIRに変換
    MirUnaryOp convert_unary_op(hir::HirUnaryOp op);

    // 値を文字列に変換するヘルパー
    LocalId convert_to_string(LocalId value, const hir::TypePtr& type, LoweringContext& ctx);
};

}  // namespace cm::mir