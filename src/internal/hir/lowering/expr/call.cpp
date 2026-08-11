// ============================================================
// HIR lowering - 呼び出し式（関数呼び出し・リダクション演算子）
// ============================================================

#include "internal/base/mangle.hpp"
#include "internal/hir/lowering/expr/internal.hpp"
#include "internal/hir/lowering/fwd.hpp"

#include <memory>
#include <set>
#include <string>
#include <utility>

namespace cm::hir {

// 関数呼び出し
HirExprPtr HirLowering::lower_reduction(ast::CallExpr& call, const std::string& name) {
    // 被演算子の型からビット幅を求める（bit[N]=N・単一bit=1・整数型=バイト幅*8）
    ast::TypePtr operand_type = call.args[0]->type;
    int width = 0;
    if (operand_type) {
        if (operand_type->kind == ast::TypeKind::Array && operand_type->element_type &&
            operand_type->element_type->kind == ast::TypeKind::Bit) {
            width = static_cast<int>(operand_type->array_size.value_or(0));
        } else if (operand_type->kind == ast::TypeKind::Bit) {
            width = 1;
        } else if (operand_type->is_integer()) {
            width = static_cast<int>(operand_type->info().size) * 8;
        }
    }
    if (width <= 0) {
        width = 32;  // 型が取れない場合の安全側フォールバック
    }

    // SVターゲット: native リダクション演算子（&x / |x / ^x / ~&x / ~|x / ~^x）の出力用に
    // ビルトイン呼び出しを残す（SVコード生成が __builtin_reduce_* を写像する）
    if (sv_target_) {
        auto hir = std::make_unique<HirCall>();
        hir->func_name = "__builtin_" + name;
        hir->args.push_back(lower_expr(*call.args[0]));
        return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
    }

    // 非SV: 幅ぶんの算術へ脱糖する（LLVM/JSが既に扱える純粋な整数演算のみを生成する）
    const int64_t mask = (width >= 64) ? int64_t{-1} : ((int64_t{1} << width) - 1);

    // (operand & mask) <cmp> rhs 形式のbool式を作る（and/or/nand/nor用）
    auto masked_cmp = [&](HirBinaryOp cmp, int64_t rhs) -> HirExprPtr {
        auto band = std::make_unique<HirBinary>();
        band->op = HirBinaryOp::BitAnd;
        band->lhs = lower_expr(*call.args[0]);
        band->rhs = make_int_lit(mask, ast::make_ulong());
        auto band_expr = std::make_unique<HirExpr>(std::move(band), ast::make_ulong());
        auto cmp_bin = std::make_unique<HirBinary>();
        cmp_bin->op = cmp;
        cmp_bin->lhs = std::move(band_expr);
        cmp_bin->rhs = make_int_lit(rhs, ast::make_ulong());
        return std::make_unique<HirExpr>(std::move(cmp_bin), ast::make_bool());
    };

    if (name == "reduce_and") {
        return masked_cmp(HirBinaryOp::Eq, mask);  // 全ビット1
    }
    if (name == "reduce_or") {
        return masked_cmp(HirBinaryOp::Ne, 0);  // 1ビットでも1
    }
    if (name == "reduce_nand") {
        return masked_cmp(HirBinaryOp::Ne, mask);  // 全ビット1でない
    }
    if (name == "reduce_nor") {
        return masked_cmp(HirBinaryOp::Eq, 0);  // 全ビット0
    }

    // reduce_xor / reduce_xnor: パリティ = XOR_{k=0..width-1} ((operand >> k) & 1)。
    // 各ビットを個別に取り出してXOR畳み込みする（被演算子を幅ぶん評価するため、
    // reduce_xor/xnor の被演算子には副作用のない式を渡すこと）
    HirExprPtr parity;
    for (int k = 0; k < width; ++k) {
        HirExprPtr shifted;
        if (k == 0) {
            shifted = lower_expr(*call.args[0]);
        } else {
            auto shr = std::make_unique<HirBinary>();
            shr->op = HirBinaryOp::Shr;
            shr->lhs = lower_expr(*call.args[0]);
            shr->rhs = make_int_lit(k, ast::make_int());
            shifted = std::make_unique<HirExpr>(std::move(shr), ast::make_ulong());
        }
        auto bit = std::make_unique<HirBinary>();
        bit->op = HirBinaryOp::BitAnd;
        bit->lhs = std::move(shifted);
        bit->rhs = make_int_lit(1, ast::make_ulong());
        auto bit_expr = std::make_unique<HirExpr>(std::move(bit), ast::make_ulong());
        if (!parity) {
            parity = std::move(bit_expr);
        } else {
            auto x = std::make_unique<HirBinary>();
            x->op = HirBinaryOp::BitXor;
            x->lhs = std::move(parity);
            x->rhs = std::move(bit_expr);
            parity = std::make_unique<HirExpr>(std::move(x), ast::make_ulong());
        }
    }
    // parity は 0/1。reduce_xor は parity!=0、reduce_xnor は parity==0
    auto cmp_bin = std::make_unique<HirBinary>();
    cmp_bin->op = (name == "reduce_xor") ? HirBinaryOp::Ne : HirBinaryOp::Eq;
    cmp_bin->lhs = std::move(parity);
    cmp_bin->rhs = make_int_lit(0, ast::make_ulong());
    return std::make_unique<HirExpr>(std::move(cmp_bin), ast::make_bool());
}

HirExprPtr HirLowering::lower_call(ast::CallExpr& call, TypePtr type) {
    debug::hir::log(debug::hir::Id::CallExprLower, "", debug::Level::Debug);

    // デバッグ: calleeの種類を確認
    if (call.callee) {
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            debug::hir::log(debug::hir::Id::CallTarget, "callee is IdentExpr: " + ident->name,
                            debug::Level::Debug);
        } else if (auto* member = call.callee->as<ast::MemberExpr>()) {
            if (auto* obj_ident = member->object->as<ast::IdentExpr>()) {
                debug::hir::log(debug::hir::Id::CallTarget,
                                "callee is MemberExpr: " + obj_ident->name + "::" + member->member,
                                debug::Level::Debug);
            } else {
                debug::hir::log(debug::hir::Id::CallTarget,
                                "callee is MemberExpr with non-IdentExpr object: " + member->member,
                                debug::Level::Debug);
            }
        } else {
            debug::hir::log(debug::hir::Id::CallTarget, "callee is unknown type",
                            debug::Level::Debug);
        }
    }

    // リダクション演算子（SV-N2）: SVターゲットは native 出力用にビルトイン呼び出しへ残し、
    // 非SVは幅ぶんの算術（マスク比較・パリティ）へ脱糖する
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        if ((ident->name == "reduce_and" || ident->name == "reduce_or" ||
             ident->name == "reduce_xor" || ident->name == "reduce_nand" ||
             ident->name == "reduce_nor" || ident->name == "reduce_xnor") &&
            call.args.size() == 1) {
            return lower_reduction(call, ident->name);
        }
    }

    // enum variantコンストラクタ呼び出しのチェック
    // パターン1: IdentExpr (例：OptVal::HasVal(42) - パーサーが::を含む名前として解析)
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        auto enum_it = enum_values_.find(ident->name);
        if (enum_it != enum_values_.end()) {
            // Tagged Union: enum variantコンストラクタ呼び出し
            debug::hir::log(debug::hir::Id::CallTarget,
                            "enum variant constructor: " + ident->name + " = " +
                                std::to_string(enum_it->second),
                            debug::Level::Debug);

            // HirEnumConstructノードを生成（タグ+ペイロード）
            auto enum_construct = std::make_unique<HirEnumConstruct>();

            // enum名とバリアント名を分解（"EnumName::VariantName" 形式）
            std::string full_name = ident->name;
            auto sep = full_name.find("::");
            if (sep != std::string::npos) {
                enum_construct->enum_name = full_name.substr(0, sep);
                enum_construct->variant_name = full_name.substr(sep + 2);
            } else {
                enum_construct->enum_name = full_name;
                enum_construct->variant_name = full_name;
            }
            enum_construct->tag_value = enum_it->second;

            // 引数があればペイロードとして保存
            if (!call.args.empty()) {
                enum_construct->payload = lower_expr(*call.args[0]);
            }

            // Tagged Union型を作成
            // 結果型は__TaggedUnion_{enum_name}構造体
            auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            tagged_union_type->name = "__TaggedUnion_" + enum_construct->enum_name;

            return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
        }
    }

    // パターン2: MemberExpr (例：Result::Err - パーサーがResultをIdentでErrをメンバとして解析)
    if (auto* member = call.callee->as<ast::MemberExpr>()) {
        if (auto* obj_ident = member->object->as<ast::IdentExpr>()) {
            // EnumName::VariantName形式を構築
            std::string full_name = obj_ident->name + "::" + member->member;
            auto enum_it = enum_values_.find(full_name);
            if (enum_it != enum_values_.end()) {
                // Tagged Union: enum variantコンストラクタ呼び出し
                debug::hir::log(debug::hir::Id::CallTarget,
                                "enum variant constructor (MemberExpr): " + full_name + " = " +
                                    std::to_string(enum_it->second),
                                debug::Level::Debug);

                auto enum_construct = std::make_unique<HirEnumConstruct>();
                enum_construct->enum_name = obj_ident->name;
                enum_construct->variant_name = member->member;
                enum_construct->tag_value = enum_it->second;

                if (!call.args.empty()) {
                    enum_construct->payload = lower_expr(*call.args[0]);
                }

                auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
                tagged_union_type->name = "__TaggedUnion_" + enum_construct->enum_name;

                return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
            }
        }
    }

    auto hir = std::make_unique<HirCall>();

    std::string func_name;
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        func_name = ident->name;

        // インポートエイリアスをチェック
        auto alias_it = import_aliases_.find(func_name);
        if (alias_it != import_aliases_.end()) {
            func_name = alias_it->second;
            debug::hir::log(debug::hir::Id::CallTarget,
                            "resolved import alias: " + ident->name + " -> " + func_name,
                            debug::Level::Trace);
        } else if (func_name == "println") {
            // フォールバック: printlnは常に__println__にマップ
            func_name = "__println__";
        } else if (func_name == "print") {
            // フォールバック: printは常に__print__にマップ
            func_name = "__print__";
        }

        // 静的メソッド呼び出し(Type::method)をType__method形式に変換
        // モジュールパス(std::io::println)は変換しない
        // 判定: ::が1つのみで、左側が既知の構造体/enum名の場合のみ変換（大文字始まりだけで判定すると大文字の名前空間エイリアス
        // `import ./mod as M; M::f()` が誤変換されシンボル不一致になる）
        size_t first_colon = func_name.find("::");
        if (first_colon != std::string::npos) {
            size_t second_colon = func_name.find("::", first_colon + 2);
            // ::が1つだけ存在する場合
            if (second_colon == std::string::npos) {
                std::string type_part = func_name.substr(0, first_colon);
                std::string method_part = func_name.substr(first_colon + 2);
                if (!type_part.empty() && std::isupper(static_cast<unsigned char>(type_part[0]))) {
                    // ジェネリック特殊化の静的呼び出し（Box<int>::new）は表示形Base<args>__methodへ変換する
                    // （R22: 未変換のままだとcallが黙って消えゼロ値になっていた。型引数の文字列分割による
                    //  半マングルBase__Arg1__Arg2は廃止し、復号はmonoスキャンの表示形照合へ一元化する＝移行計画①）
                    size_t lt_pos = type_part.find('<');
                    if (lt_pos != std::string::npos && type_part.back() == '>') {
                        std::string base_name = type_part.substr(0, lt_pos);
                        if (struct_defs_.count(base_name) > 0 || enum_defs_.count(base_name) > 0) {
                            func_name = mangle::method_name(type_part, method_part);
                        }
                    } else if (struct_defs_.count(type_part) > 0 ||
                               enum_defs_.count(type_part) > 0) {
                        func_name.replace(first_colon, 2, "__");
                    }
                }
            }
        }

        hir->func_name = func_name;
        debug::hir::log(debug::hir::Id::CallTarget, "function: " + func_name, debug::Level::Trace);

        static const std::set<std::string> builtin_funcs = {
            "printf", "__println__",      "__print__",          "sprintf", "exit", "panic",
            "assert", "__builtin_concat", "__builtin_replicate"};

        bool is_builtin = builtin_funcs.find(func_name) != builtin_funcs.end();
        bool is_defined = func_defs_.find(func_name) != func_defs_.end();
        bool is_namespaced = func_name.find("::") != std::string::npos;

        if (!is_builtin && !is_defined && !is_namespaced) {
            hir->is_indirect = true;
            debug::hir::log(debug::hir::Id::CallTarget, "indirect call via variable: " + func_name,
                            debug::Level::Debug);
        }
    } else {
        // 複雑な式を呼び出し先にする間接呼び出し（fs[0](args)・getf()(args) 等。局所処理調査G4）。
        // callee式を評価して indirect_callee に載せることで、MIRが式の値（関数ポインタ）経由で呼び出す。
        // 従来は func_name="<indirect>" のまま indirect_callee を設定せず、MIRが変数名 <indirect> の解決に失敗して _<indirect> 未解決シンボルになっていた（一旦変数へ束ねれば動いていた）
        hir->func_name = "<indirect>";
        hir->is_indirect = true;
        hir->indirect_callee = lower_expr(*call.callee);
        debug::hir::log(debug::hir::Id::CallTarget, "indirect call via expression",
                        debug::Level::Trace);
    }

    debug::hir::log(debug::hir::Id::CallArgs, "count=" + std::to_string(call.args.size()),
                    debug::Level::Trace);
    for (size_t i = 0; i < call.args.size(); i++) {
        debug::hir::log(debug::hir::Id::CallArgEval, "arg[" + std::to_string(i) + "]",
                        debug::Level::Trace);
        hir->args.push_back(lower_expr(*call.args[i]));
    }

    // デフォルト引数を適用
    if (!func_name.empty() && !hir->is_indirect) {
        auto func_it = func_defs_.find(func_name);
        if (func_it != func_defs_.end()) {
            const auto* func_def = func_it->second;
            for (size_t i = call.args.size(); i < func_def->params.size(); ++i) {
                const auto& param = func_def->params[i];
                if (param.default_value) {
                    debug::hir::log(debug::hir::Id::CallArgEval,
                                    "default arg[" + std::to_string(i) + "] for " + param.name,
                                    debug::Level::Trace);
                    hir->args.push_back(lower_expr(*param.default_value));
                }
            }
        }
    }

    return std::make_unique<HirExpr>(std::move(hir), type);
}

}  // namespace cm::hir
