// ============================================================
// HIR lowering - メンバアクセス式（メソッド呼び出し・フィールド参照）
// ============================================================

#include "internal/base/mangle.hpp"
#include "internal/hir/lowering/fwd.hpp"
#include "internal/hir/slice_dispatch.hpp"
#include "internal/syntax/ast/typekey.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::hir {

namespace {

// 検索ビルトイン（値比較系: indexOf/includes/contains）の要素型サフィックス（Z1）。
// 従来は一律_i32でstringがポインタ切り詰め比較・short/tiny/longがstride誤り・float/doubleが不正IRになっていた。
// 未対応の要素型（構造体・ユニオン等）はnulloptを返し、型検査が診断済みである
std::optional<std::string> array_search_suffix(const ast::TypePtr& elem) {
    if (!elem) {
        return std::nullopt;
    }
    switch (elem->kind) {
        case ast::TypeKind::Bool:
        case ast::TypeKind::Char:
        case ast::TypeKind::Tiny:
        case ast::TypeKind::UTiny:
            return std::string("i8");
        case ast::TypeKind::Short:
        case ast::TypeKind::UShort:
            return std::string("i16");
        case ast::TypeKind::Int:
        case ast::TypeKind::UInt:
            return std::string("i32");
        case ast::TypeKind::Long:
        case ast::TypeKind::ULong:
        case ast::TypeKind::ISize:
        case ast::TypeKind::USize:
            return std::string("i64");
        case ast::TypeKind::Float:
        case ast::TypeKind::UFloat:
            return std::string("f32");
        case ast::TypeKind::Double:
        case ast::TypeKind::UDouble:
            return std::string("f64");
        case ast::TypeKind::String:
            return std::string("str");
        default:
            return std::nullopt;
    }
}

// 配列HOF（map/filter/reduce/forEach/some/every/findIndex/first/last/find/sortBy）の要素幅サフィックス（局所処理調査E系）。
// 正準のslice_scalar_info（slice_dispatch.hpp）から導出し、各サイトの手書きi32/i64二択を廃止する。
// 集約・文字列など非スカラ要素はnullopt（js/ts以外は型検査が診断済みで、呼び出し側のi32フォールバックには到達しない。js/tsは構造的loweringのため幅は無関係）
std::optional<std::string> array_hof_suffix(const ast::TypePtr& elem) {
    if (!elem) {
        return std::nullopt;
    }
    if (auto info = slice_scalar_info(elem->kind)) {
        return std::string(info->width);
    }
    return std::nullopt;
}

// サフィックスを「_i32はレガシー基底名・それ以外は_<幅>付き」の実名へ整形する（map/filter/sortBy/sort/reverseの既存i32変種は無サフィックス名のため）
std::string hof_func_name(const std::string& base, const std::optional<std::string>& suffix,
                          bool i32_is_bare) {
    if (!suffix || (*suffix == "i32" && i32_is_bare)) {
        return i32_is_bare ? base : base + "_i32";
    }
    return base + "_" + *suffix;
}

}  // namespace

namespace {

// 固定長配列をcm_array_to_sliceでスライスへ変換するHIR呼び出しを構築する（局所処理調査E系）。
// 固定長配列のsort/reverseを、要素幅・符号・文字列・集約をヘッダのelem_sizeで正しく扱うスライス汎用ランタイムへ相乗りさせるために使う（従来のi32形状の__builtin_array_sort/reverseはdouble等で黙って壊れていた）。
// 要素サイズはHIRでは決めず0を置き、MIRのcm_array_to_slice引数loweringがlayout API（array_elem_stride）で再計算する
HirExprPtr make_array_to_slice(HirExprPtr obj_hir, const TypePtr& obj_type) {
    auto convert_call = std::make_unique<HirCall>();
    convert_call->func_name = "cm_array_to_slice";
    auto addr_op = std::make_unique<HirUnary>();
    addr_op->op = HirUnaryOp::AddrOf;
    addr_op->operand = std::move(obj_hir);
    auto ptr_type = ast::make_pointer(obj_type->element_type);
    convert_call->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
    auto size_lit = std::make_unique<HirLiteral>();
    size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
    convert_call->args.push_back(std::make_unique<HirExpr>(std::move(size_lit), ast::make_long()));
    auto elem_size_lit = std::make_unique<HirLiteral>();
    elem_size_lit->value = int64_t{0};
    convert_call->args.push_back(
        std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_long()));
    auto slice_type = ast::make_array(obj_type->element_type, std::nullopt);
    return std::make_unique<HirExpr>(std::move(convert_call), slice_type);
}

// sortの比較関数名（符号・浮動小数・文字列を区別）。正準のslice_scalar_sort_suffix（slice_dispatch.hpp）から導出し、手書きswitchの複製を廃止する
std::string slice_sort_func_name(const TypePtr& elem) {
    if (elem) {
        if (elem->kind == ast::TypeKind::String) {
            return "cm_slice_sort_str";
        }
        if (const char* s = slice_scalar_sort_suffix(elem->kind)) {
            return std::string("cm_slice_sort_") + s;
        }
    }
    return "cm_slice_sort_i32";
}

// 配列HOF（map/filter/reduce等）共通のデータ引数・サイズ引数を構築する。
// 固定長配列: 配列アドレス(&arr) + 静的サイズ。
// スライス: CmSlice*値そのもの + サイズ-1（ランタイムが負サイズをCmSlice*として展開する。
// 従来はvalue_or(0)でサイズ0が埋め込まれ、スライスへのHOFがサイレントに空結果を返していた）
void push_array_hof_args(HirCall& call, HirExprPtr obj_hir, const TypePtr& obj_type) {
    const bool is_slice = !obj_type->array_size.has_value();
    if (is_slice) {
        call.args.push_back(std::move(obj_hir));
    } else {
        auto addr_op = std::make_unique<HirUnary>();
        addr_op->op = HirUnaryOp::AddrOf;
        addr_op->operand = std::move(obj_hir);
        auto ptr_type = ast::make_pointer(obj_type->element_type);
        call.args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
    }

    auto size_lit = std::make_unique<HirLiteral>();
    size_lit->value =
        is_slice ? int64_t{-1} : static_cast<int64_t>(obj_type->array_size.value_or(0));
    call.args.push_back(std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
}

}  // namespace

// lower_memberの腕: 組み込みResult<T,E>/Option<T>メソッドの脱糖（非該当ならnullptrを返しobj_hirは消費しない）
HirExprPtr HirLowering::lower_member_sum_type(ast::MemberExpr& mem, const TypePtr& obj_type,
                                              const HirExprPtr& obj_hir) {
    // 組み込みResult<T,E>/Option<T>のメソッドをタグ比較・ペイロード取り出しへ脱糖する（is_ok/is_err/is_some/is_none/unwrap/unwrap_or/unwrap_err/expect）
    // 補間ミニパイプライン経由ではMIRローカル型名（__TaggedUnion_Option等）で渡るため、基底enum名へ正規化してから判定する
    std::string enum_base = obj_type ? obj_type->name : "";
    {
        static const std::string kTaggedPrefix = "__TaggedUnion_";
        if (enum_base.rfind(kTaggedPrefix, 0) == 0) {
            enum_base = enum_base.substr(kTaggedPrefix.size());
        }
        // モノモーフィズド名（Result__int__string・Result$2$...等）は正準関数で基底名へ切り詰める
        // （checker側のstrip_spec_suffixと同じ意味論をtypekeyの共有実装で担い、逐語複製を解消。checker-to-hir-resolution-handoff）
        enum_base = ast::typekey::spec_base_name(enum_base);
    }
    // enum_defs_はミニパイプライン（補間式）ではprelude宣言が無く空になるため、ビルトイン登録・seedで必ず入るenum_values_のタグキーで判定する（ユーザーが同名structを定義した場合はタグキーが無いので誤脱糖しない）
    const bool is_builtin_sum_type =
        (enum_base == "Result" && enum_values_.count("Result::Ok") > 0) ||
        (enum_base == "Option" && enum_values_.count("Option::Some") > 0);
    if (obj_type && obj_type->kind == ast::TypeKind::Struct && is_builtin_sum_type) {
        const std::string& en = enum_base;
        const bool is_result = (en == "Result");
        const std::string ok_variant = is_result ? "Ok" : "Some";
        const std::string err_variant = is_result ? "Err" : "None";
        auto tag_of = [&](const std::string& v) -> int64_t {
            auto it = enum_values_.find(en + "::" + v);
            return (it != enum_values_.end()) ? it->second : 0;
        };
        TypePtr ok_type = (!obj_type->type_args.empty() && obj_type->type_args[0])
                              ? obj_type->type_args[0]
                              : ast::make_int();
        TypePtr err_type = (is_result && obj_type->type_args.size() > 1 && obj_type->type_args[1])
                               ? obj_type->type_args[1]
                               : ast::make_int();
        auto make_tag_cmp = [&](int64_t tag_value) -> HirExprPtr {
            auto tag_access = std::make_unique<HirMember>();
            tag_access->object = clone_hir_expr(obj_hir);
            tag_access->member = "__tag";
            auto tag_expr = std::make_unique<HirExpr>(std::move(tag_access), ast::make_int());
            auto lit = std::make_unique<HirLiteral>();
            lit->value = tag_value;
            auto lit_expr = std::make_unique<HirExpr>(std::move(lit), ast::make_int());
            auto cmp = std::make_unique<HirBinary>();
            cmp->op = HirBinaryOp::Eq;
            cmp->lhs = std::move(tag_expr);
            cmp->rhs = std::move(lit_expr);
            return std::make_unique<HirExpr>(std::move(cmp), ast::make_bool());
        };
        auto make_payload = [&](const std::string& variant,
                                const TypePtr& payload_type) -> HirExprPtr {
            auto payload = std::make_unique<HirEnumPayload>();
            payload->scrutinee = clone_hir_expr(obj_hir);
            payload->variant_name = en + "::" + variant;
            payload->payload_type = payload_type;
            return std::make_unique<HirExpr>(std::move(payload), payload_type);
        };
        auto make_panic = [&](const std::string& msg, const TypePtr& t) -> HirExprPtr {
            auto call = std::make_unique<HirCall>();
            call->func_name = "panic";
            auto msg_lit = std::make_unique<HirLiteral>();
            msg_lit->value = msg;
            call->args.push_back(std::make_unique<HirExpr>(std::move(msg_lit), ast::make_string()));
            return std::make_unique<HirExpr>(std::move(call), t);
        };
        auto make_guarded = [&](int64_t good_tag, HirExprPtr value, HirExprPtr fallback,
                                const TypePtr& t) -> HirExprPtr {
            auto tern = std::make_unique<HirTernary>();
            tern->condition = make_tag_cmp(good_tag);
            tern->then_expr = std::move(value);
            tern->else_expr = std::move(fallback);
            return std::make_unique<HirExpr>(std::move(tern), t);
        };

        if (mem.member == "is_ok" || mem.member == "is_some") {
            return make_tag_cmp(tag_of(ok_variant));
        }
        if (mem.member == "is_err" || mem.member == "is_none") {
            return make_tag_cmp(tag_of(err_variant));
        }
        if (mem.member == "unwrap") {
            std::string msg =
                is_result ? "called unwrap on an Err value" : "called unwrap on a None value";
            return make_guarded(tag_of(ok_variant), make_payload(ok_variant, ok_type),
                                make_panic(msg, ok_type), ok_type);
        }
        if (mem.member == "expect" && !mem.args.empty()) {
            // メッセージ引数（文字列リテラル）をそのままpanicへ渡す
            auto msg_expr = lower_expr(*mem.args[0]);
            auto call = std::make_unique<HirCall>();
            call->func_name = "panic";
            call->args.push_back(std::move(msg_expr));
            auto panic_expr = std::make_unique<HirExpr>(std::move(call), ok_type);
            return make_guarded(tag_of(ok_variant), make_payload(ok_variant, ok_type),
                                std::move(panic_expr), ok_type);
        }
        if (mem.member == "unwrap_or" && !mem.args.empty()) {
            auto fallback = lower_expr(*mem.args[0]);
            // 型引数が失われている場合（補間ミニパイプライン等）はフォールバック引数の型からペイロード型を復元する
            if (obj_type->type_args.empty() && fallback->type) {
                ok_type = fallback->type;
            }
            return make_guarded(tag_of(ok_variant), make_payload(ok_variant, ok_type),
                                std::move(fallback), ok_type);
        }
        if (mem.member == "unwrap_err" && is_result) {
            return make_guarded(tag_of(err_variant), make_payload(err_variant, err_type),
                                make_panic("called unwrap_err on an Ok value", err_type), err_type);
        }
    }
    return nullptr;
}

// lower_memberの腕: 配列のビルトインメソッド（非該当ならnullptrを返しobj_hirは消費しない）
HirExprPtr HirLowering::lower_member_array_builtin(ast::MemberExpr& mem, const TypePtr& obj_type,
                                                   HirExprPtr& obj_hir) {
    // 配列のビルトインメソッド処理
    if (obj_type && obj_type->kind == ast::TypeKind::Array) {
        // dim() - 配列の次元数を返す
        if (mem.member == "dim") {
            // 次元数を計算
            int dim = 1;
            TypePtr t = obj_type->element_type;
            while (t && t->kind == ast::TypeKind::Array) {
                dim++;
                t = t->element_type;
            }
            auto lit = std::make_unique<HirLiteral>();
            lit->value = static_cast<int64_t>(dim);
            debug::hir::log(debug::hir::Id::MethodCallLower,
                            "Array builtin dim() = " + std::to_string(dim), debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
        }

        // get(i): チェック付き要素アクセス（Rustのslice::get相当）。
        // 範囲内なら Option::Some(arr[i])、範囲外なら Option::None へ脱糖する。
        // arr[i] の範囲外アクセス（固定長=未定義値・スライス=0）をOption型で安全にハンドリングするためのAPI
        if (mem.member == "get" && mem.args.size() == 1) {
            TypePtr elem_type = obj_type->element_type ? obj_type->element_type : ast::make_int();
            auto idx_hir = lower_expr(*mem.args[0]);

            // 長さ式: 固定長は定数リテラル、スライスは__builtin_slice_len
            HirExprPtr len_expr;
            if (obj_type->array_size.has_value()) {
                auto len_lit = std::make_unique<HirLiteral>();
                len_lit->value = static_cast<int64_t>(obj_type->array_size.value());
                len_expr = std::make_unique<HirExpr>(std::move(len_lit), ast::make_int());
            } else {
                auto len_call = std::make_unique<HirCall>();
                len_call->func_name = "__builtin_slice_len";
                len_call->args.push_back(clone_hir_expr(obj_hir));
                len_expr = std::make_unique<HirExpr>(std::move(len_call), ast::make_int());
            }

            // 条件: (i >= 0) && (i < len)
            auto make_int_lit = [](int64_t v) {
                auto lit = std::make_unique<HirLiteral>();
                lit->value = v;
                return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
            };
            auto ge_zero = std::make_unique<HirBinary>();
            ge_zero->op = HirBinaryOp::Ge;
            ge_zero->lhs = clone_hir_expr(idx_hir);
            ge_zero->rhs = make_int_lit(0);
            auto ge_expr = std::make_unique<HirExpr>(std::move(ge_zero), ast::make_bool());
            auto lt_len = std::make_unique<HirBinary>();
            lt_len->op = HirBinaryOp::Lt;
            lt_len->lhs = clone_hir_expr(idx_hir);
            lt_len->rhs = std::move(len_expr);
            auto lt_expr = std::make_unique<HirExpr>(std::move(lt_len), ast::make_bool());
            auto both = std::make_unique<HirBinary>();
            both->op = HirBinaryOp::And;
            both->lhs = std::move(ge_expr);
            both->rhs = std::move(lt_expr);
            auto cond = std::make_unique<HirExpr>(std::move(both), ast::make_bool());

            // Option<elem>型
            auto opt_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            opt_type->name = "Option";
            opt_type->type_args.push_back(elem_type);

            // Some(arr[i])
            auto index_access = std::make_unique<HirIndex>();
            index_access->object = clone_hir_expr(obj_hir);
            index_access->index = std::move(idx_hir);
            auto elem_expr = std::make_unique<HirExpr>(std::move(index_access), elem_type);
            auto some_construct = std::make_unique<HirEnumConstruct>();
            some_construct->enum_name = "Option";
            some_construct->variant_name = "Some";
            some_construct->tag_value = 0;
            some_construct->payload = std::move(elem_expr);
            auto some_expr = std::make_unique<HirExpr>(std::move(some_construct), opt_type);

            // None
            auto none_construct = std::make_unique<HirEnumConstruct>();
            none_construct->enum_name = "Option";
            none_construct->variant_name = "None";
            none_construct->tag_value = 1;
            auto none_expr = std::make_unique<HirExpr>(std::move(none_construct), opt_type);

            auto tern = std::make_unique<HirTernary>();
            tern->condition = std::move(cond);
            tern->then_expr = std::move(some_expr);
            tern->else_expr = std::move(none_expr);
            return std::make_unique<HirExpr>(std::move(tern), opt_type);
        }

        // 動的配列（スライス）の場合はlenを関数呼び出しで処理
        if (!obj_type->array_size.has_value()) {
            if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_len";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin len()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
            }
        }
        // 静的配列の場合はサイズを定数リテラルとして返す
        else if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = static_cast<int64_t>(obj_type->array_size.value());
            debug::hir::log(
                debug::hir::Id::MethodCallLower,
                "Array builtin size() = " + std::to_string(obj_type->array_size.value_or(0)),
                debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
        }

        if (mem.member == "forEach") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でtiny/short/float/doubleがstride誤りだった）
            hir->func_name = hof_func_name("__builtin_array_forEach",
                                           array_hof_suffix(obj_type->element_type), false);
            hir->args.push_back(std::move(obj_hir));
            auto size_lit = std::make_unique<HirLiteral>();
            // スライスはサイズ-1（ランタイムが負サイズをCmSlice*として展開する）
            size_lit->value = obj_type->array_size.has_value()
                                  ? static_cast<int64_t>(*obj_type->array_size)
                                  : int64_t{-1};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin forEach()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
        }

        if (mem.member == "reduce") {
            auto hir = std::make_unique<HirCall>();
            // コールバックと初期値を先にlowerし、アキュムレータ型を取り出す
            std::vector<HirExprPtr> lowered_args;
            for (auto& arg : mem.args) {
                lowered_args.push_back(lower_expr(*arg));
            }
            hir::TypePtr acc_type = nullptr;
            if (!lowered_args.empty() && lowered_args[0] && lowered_args[0]->type &&
                lowered_args[0]->type->kind == ast::TypeKind::Function &&
                !lowered_args[0]->type->param_types.empty()) {
                acc_type = lowered_args[0]->type->param_types[0];
            }
            const bool acc64 =
                acc_type &&
                (acc_type->kind == ast::TypeKind::Long || acc_type->kind == ast::TypeKind::ULong ||
                 acc_type->kind == ast::TypeKind::ISize || acc_type->kind == ast::TypeKind::USize);
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でtiny/short/float/doubleが壊れていた）。
            // アキュムレータが64bit整数で要素が32bit以下の整数の場合は混合幅版を選ぶ（要素型のみで選ぶとlong acc×int要素のコールバックが(i32,i32)シグネチャで呼ばれ、wasmはcall_indirectの型検査でトラップする）
            auto elem_suffix = array_hof_suffix(obj_type->element_type);
            std::string suffix = "_" + elem_suffix.value_or("i32");
            if (acc64 && suffix == "_i32") {
                suffix = "_i32_acc64";
            }
            hir->func_name = "__builtin_array_reduce" + suffix;
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            // コールバック関数と初期値
            for (auto& larg : lowered_args) {
                hir->args.push_back(std::move(larg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reduce()",
                            debug::Level::Debug);
            // 結果型はアキュムレータ型（不明なら従来どおり要素型）
            hir::TypePtr result_type = acc_type ? acc_type : obj_type->element_type;
            return std::make_unique<HirExpr>(std::move(hir), result_type);
        }

        if (mem.member == "some") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32固定）
            hir->func_name = hof_func_name("__builtin_array_some",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin some()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }

        if (mem.member == "every") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32固定）
            hir->func_name = hof_func_name("__builtin_array_every",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin every()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }

        if (mem.member == "findIndex") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32固定）
            hir->func_name = hof_func_name("__builtin_array_findIndex",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin findIndex()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
        }

        if (mem.member == "indexOf") {
            auto hir = std::make_unique<HirCall>();
            // 要素型でランタイム変種を選択する（Z1。未対応型は型検査が診断済みのためi32へフォールバック）
            auto idx_suffix = array_search_suffix(obj_type->element_type);
            hir->func_name =
                "__builtin_array_indexOf_" + (idx_suffix ? *idx_suffix : std::string("i32"));
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin indexOf()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
        }

        if (mem.member == "includes" || mem.member == "contains") {
            auto hir = std::make_unique<HirCall>();
            // 要素型でランタイム変種を選択する（Z1。未対応型は型検査が診断済みのためi32へフォールバック）
            auto inc_suffix = array_search_suffix(obj_type->element_type);
            hir->func_name =
                "__builtin_array_includes_" + (inc_suffix ? *inc_suffix : std::string("i32"));
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin includes()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }

        if (mem.member == "map") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でtiny/short/float/doubleがstride不一致で黙って壊れていた。i32はレガシーの無サフィックス基底名）
            hir->func_name = hof_func_name("__builtin_array_map",
                                           array_hof_suffix(obj_type->element_type), true);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            // コールバック関数
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin map()",
                            debug::Level::Debug);
            // 結果の型は常に動的配列/スライス
            TypePtr result_type = ast::make_array(obj_type->element_type, std::nullopt);
            return std::make_unique<HirExpr>(std::move(hir), result_type);
        }

        if (mem.member == "filter") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（mapと同様。i32はレガシーの無サフィックス基底名）
            hir->func_name = hof_func_name("__builtin_array_filter",
                                           array_hof_suffix(obj_type->element_type), true);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            // コールバック関数
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin filter()",
                            debug::Level::Debug);
            // 結果の型（動的配列/スライス）
            TypePtr result_type = ast::make_array(obj_type->element_type, std::nullopt);
            return std::make_unique<HirExpr>(std::move(hir), result_type);
        }

        if (mem.member == "reverse" && obj_type->array_size.has_value()) {
            // 固定長配列はスライスへ変換し、ヘッダのelem_sizeで全要素型を正しく扱うスライス汎用reverseへ相乗りする（従来のi32形状__builtin_array_reverseはdouble/構造体で黙って壊れていた）
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "cm_slice_reverse";
            hir->args.push_back(make_array_to_slice(std::move(obj_hir), obj_type));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reverse()",
                            debug::Level::Debug);
            // 動的配列（スライス）を返す
            auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
            return std::make_unique<HirExpr>(std::move(hir), return_type);
        }

        if (mem.member == "sort" && obj_type->array_size.has_value()) {
            // 固定長配列はスライスへ変換し、符号・浮動小数・文字列を正しく比較するスライス汎用sortへ相乗りする（従来のi32形状__builtin_array_sortはdouble等で黙って壊れていた）
            auto hir = std::make_unique<HirCall>();
            hir->func_name = slice_sort_func_name(obj_type->element_type);
            hir->args.push_back(make_array_to_slice(std::move(obj_hir), obj_type));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin sort()",
                            debug::Level::Debug);
            // 動的配列（スライス）を返す
            auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
            return std::make_unique<HirExpr>(std::move(hir), return_type);
        }

        // sortByは固定長配列・スライス共通（スライスはサイズ-1番兵でランタイム展開）
        if (mem.member == "sortBy") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32形状固定でlong/double要素の比較関数が効かなかった。i32はレガシーの無サフィックス基底名）
            hir->func_name = hof_func_name("__builtin_array_sortBy",
                                           array_hof_suffix(obj_type->element_type), true);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            // コンパレータ関数
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin sortBy()",
                            debug::Level::Debug);
            // 動的配列（スライス）を返す
            auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
            return std::make_unique<HirExpr>(std::move(hir), return_type);
        }

        // 固定サイズ配列のfirst（動的配列は後で処理）
        if (mem.member == "first" && obj_type->array_size.has_value()) {
            // 要素型が配列（多次元配列）の場合は、インデックスアクセスに変換
            if (obj_type->element_type && obj_type->element_type->kind == ast::TypeKind::Array) {
                // arr.first() -> arr[0]
                auto idx_lit = std::make_unique<HirLiteral>();
                idx_lit->value = int64_t{0};
                auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                auto index_op = std::make_unique<HirIndex>();
                index_op->object = std::move(obj_hir);
                index_op->index = std::move(idx_expr);
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin first() - multidim",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
            }

            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でdouble等が壊れていた）
            hir->func_name = hof_func_name("__builtin_array_first",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin first()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
        }

        // 固定サイズ配列のlast（動的配列は後で処理）
        if (mem.member == "last" && obj_type->array_size.has_value()) {
            // 要素型が配列（多次元配列）の場合は、インデックスアクセスに変換
            if (obj_type->element_type && obj_type->element_type->kind == ast::TypeKind::Array) {
                // arr.last() -> arr[size-1]
                auto idx_lit = std::make_unique<HirLiteral>();
                idx_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(1) - 1);
                auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                auto index_op = std::make_unique<HirIndex>();
                index_op->object = std::move(obj_hir);
                index_op->index = std::move(idx_expr);
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin last() - multidim",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
            }

            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でdouble等が壊れていた）
            hir->func_name = hof_func_name("__builtin_array_last",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin last()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
        }

        if (mem.member == "find") {
            auto hir = std::make_unique<HirCall>();
            // 要素幅サフィックスは正準ヘルパで全幅選択（従来はi32/i64二択でdouble等が壊れていた）
            hir->func_name = hof_func_name("__builtin_array_find",
                                           array_hof_suffix(obj_type->element_type), false);
            // データ引数とサイズ引数（固定長配列/スライス共通）
            push_array_hof_args(*hir, std::move(obj_hir), obj_type);
            // コールバック関数
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin find()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
        }
    }
    return nullptr;
}

// lower_memberの腕: 動的配列（スライス）のビルトインメソッド（非該当ならnullptrを返しobj_hirは消費しない）
HirExprPtr HirLowering::lower_member_slice_builtin(ast::MemberExpr& mem, const TypePtr& obj_type,
                                                   HirExprPtr& obj_hir, const TypePtr& type) {
    // 動的配列（スライス）のビルトインメソッド処理
    // 動的配列は array_size が設定されていない
    if (obj_type && obj_type->kind == ast::TypeKind::Array && !obj_type->array_size.has_value()) {
        if (mem.member == "len" || mem.member == "size" || mem.member == "length") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_len";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin len()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
        }

        if (mem.member == "cap" || mem.member == "capacity") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_cap";
            hir->args.push_back(std::move(obj_hir));
            return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
        }

        if (mem.member == "push") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_push";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin push()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
        }

        if (mem.member == "pop") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_pop";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin pop()",
                            debug::Level::Debug);
            TypePtr elem_type = obj_type->element_type ? obj_type->element_type : ast::make_int();
            return std::make_unique<HirExpr>(std::move(hir), elem_type);
        }

        if (mem.member == "remove" || mem.member == "delete") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_delete";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin remove()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
        }

        if (mem.member == "clear") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_slice_clear";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin clear()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
        }

        if (mem.member == "first") {
            // 要素型が配列（多次元スライス）の場合は、インデックスアクセスに変換
            if (obj_type->element_type && obj_type->element_type->kind == ast::TypeKind::Array) {
                // slice.first() -> slice[0]
                auto idx_lit = std::make_unique<HirLiteral>();
                idx_lit->value = int64_t{0};
                auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                auto index_op = std::make_unique<HirIndex>();
                index_op->object = std::move(obj_hir);
                index_op->index = std::move(idx_expr);
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin first() - multidim",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
            }

            auto hir = std::make_unique<HirCall>();
            // スライスの場合はcm_slice_first_*を使用。要素幅サフィックスは正準ヘルパで全幅選択し、ポインタ・文字列要素は_ptr変種を使う（従来はi32/i64二択でdouble/stringが壊れていた）
            std::string suffix = "_i32";
            if (auto w = array_hof_suffix(obj_type->element_type)) {
                suffix = "_" + *w;
            } else if (obj_type->element_type &&
                       (obj_type->element_type->kind == ast::TypeKind::String ||
                        obj_type->element_type->kind == ast::TypeKind::Pointer)) {
                suffix = "_ptr";
            }
            hir->func_name = "cm_slice_first" + suffix;
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin first()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
        }

        if (mem.member == "last") {
            // 要素型が配列（多次元スライス）の場合はcm_slice_last_ptrを使用
            if (obj_type->element_type && obj_type->element_type->kind == ast::TypeKind::Array) {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "cm_slice_last_ptr";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin last() - multidim",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            auto hir = std::make_unique<HirCall>();
            // スライスの場合はcm_slice_last_*を使用。要素幅サフィックスは正準ヘルパで全幅選択し、ポインタ・文字列要素は_ptr変種を使う（firstと同様）
            std::string suffix = "_i32";
            if (auto w = array_hof_suffix(obj_type->element_type)) {
                suffix = "_" + *w;
            } else if (obj_type->element_type &&
                       (obj_type->element_type->kind == ast::TypeKind::String ||
                        obj_type->element_type->kind == ast::TypeKind::Pointer)) {
                suffix = "_ptr";
            }
            hir->func_name = "cm_slice_last" + suffix;
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin last()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
        }

        if (mem.member == "reverse") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "cm_slice_reverse";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin reverse()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), type);
        }

        if (mem.member == "sort") {
            auto hir = std::make_unique<HirCall>();
            // 要素型に応じたソート関数の選択は正準ヘルパへ集約（符号・浮動小数・文字列を正しく比較。C5の手書きswitchをslice_dispatchの正準表から導出する形へ置換）
            hir->func_name = slice_sort_func_name(obj_type->element_type);
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin sort()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), type);
        }
    }
    return nullptr;
}

// lower_memberの腕: 文字列のビルトインメソッド（非該当ならnullptrを返しobj_hirは消費しない）
HirExprPtr HirLowering::lower_member_string_builtin(ast::MemberExpr& mem, const TypePtr& obj_type,
                                                    HirExprPtr& obj_hir) {
    // 文字列のビルトインメソッド処理
    if (obj_type && obj_type->kind == ast::TypeKind::String) {
        // len()はUTF-8コードポイント数、byte_len()はバイト数（H9第2段・第3段。
        // ASCIIのみの文字列では両者は一致する。添字・部分文字列は現時点ではバイト単位のまま）
        if (mem.member == "len" || mem.member == "size" || mem.member == "length") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_codepoint_len";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin len()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_uint());
        }
        if (mem.member == "byte_len") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_len";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin byte_len()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_uint());
        }
        if (mem.member == "chars") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_chars";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin chars()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_array(ast::make_uint()));
        }
        if (mem.member == "codepoint_at") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_codepoint_at";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin codepoint_at()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_uint());
        }
        if (mem.member == "byte_at") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_byte_at";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin byte_at()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
        }
        if (mem.member == "charAt" || mem.member == "at") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_charAt";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin charAt()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
        }
        if (mem.member == "substring" || mem.member == "slice") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_substring";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin substring()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "indexOf") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_indexOf";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin indexOf()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
        }
        if (mem.member == "toUpperCase") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_toUpperCase";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toUpperCase()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "toLowerCase") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_toLowerCase";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toLowerCase()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "trim") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_trim";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin trim()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "startsWith") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_startsWith";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin startsWith()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }
        if (mem.member == "endsWith") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_endsWith";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin endsWith()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }
        if (mem.member == "includes" || mem.member == "contains") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_includes";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin includes()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
        }
        if (mem.member == "repeat") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_repeat";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin repeat()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "replace") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_replace";
            hir->args.push_back(std::move(obj_hir));
            for (auto& arg : mem.args) {
                hir->args.push_back(lower_expr(*arg));
            }
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin replace()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
        }
        if (mem.member == "first") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_first";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin first()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
        }
        if (mem.member == "last") {
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "__builtin_string_last";
            hir->args.push_back(std::move(obj_hir));
            debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin last()",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
        }
    }
    return nullptr;
}

// lower_memberの腕: メソッド解決用型名の正規化（名前空間除去・値enum・ジェネリック正準化・固定長配列→スライス名。スライスimpl変換の要否はout引数で返す）
std::string HirLowering::resolve_method_type_name(const TypePtr& obj_type,
                                                  const std::string& type_name,
                                                  bool& needs_array_to_slice) {
    // 名前空間を除去した型名を取得（ネスト型のOuter::Innerは完全名で登録済みのため除去しない）
    std::string method_type_name = type_name;
    size_t last_colon = type_name.rfind("::");
    if (last_colon != std::string::npos && struct_defs_.count(type_name) == 0 &&
        enum_defs_.count(type_name) == 0) {
        method_type_name = type_name.substr(last_colon + 2);
    }

    // Q5: int正規化された値enum（checkerのresolve_typedefがnameへ元enum名を保持）は、enum名のinherent implメソッド（Color__name等）へディスパッチする
    if (obj_type && obj_type->kind == ast::TypeKind::Int && !obj_type->name.empty() &&
        enum_defs_.count(obj_type->name) > 0) {
        method_type_name = obj_type->name;
    }

    // ジェネリック型名（例：Vector<int>）を関数名ドメインの正準接頭辞（例：Vector__int、
    // ネスト特殊化はVector__Vector$1$3$int）へ変換する（移行計画①: 型ツリーからargkeyを組み、
    // type_to_string文字列の再パース＝表示形argの半マングル混入を廃止する）
    if (obj_type && obj_type->kind == ast::TypeKind::Struct && !obj_type->type_args.empty() &&
        struct_defs_.count(method_type_name) == 0) {
        std::string base_name = method_type_name.substr(0, method_type_name.find('<'));
        // stale名（旧フラット/エンコード名のままargs併存）は基底名へ再建する（C8: ユーザー定義完全名は上のcountで除外済み）
        base_name = ast::typekey::spec_base_name(base_name);
        method_type_name = ast::typekey::spec_fn_prefix(
            ast::typekey::struct_key_from_tree(base_name, obj_type->type_args));
        debug::hir::log(debug::hir::Id::MethodCallLower,
                        "Generic type name mangled: " + type_name + " -> " + method_type_name,
                        debug::Level::Debug);
    }
    // 型ツリーに引数の無い表示形名（Vector<int>等が名前へ埋め込まれた型）はそのまま残す
    // （monoのスキャンが表示形Base<args>__suffixを復号して照合する）

    // 固定長配列（T[N]）の場合、スライス型名（T[]）にマッピング
    // impl int[] for Interface のメソッドを int[5], int[10] 等からも呼び出し可能にする
    if (obj_type && obj_type->kind == ast::TypeKind::Array && obj_type->array_size.has_value()) {
        if (obj_type->element_type) {
            method_type_name = ast::type_to_string(*obj_type->element_type) + "[]";
            needs_array_to_slice = true;
            debug::hir::log(
                debug::hir::Id::MethodCallLower,
                "Fixed-size array -> slice impl: " + type_name + " -> " + method_type_name,
                debug::Level::Debug);
        }
    }
    return method_type_name;
}

// lower_memberの腕: 関数型フィールドの呼び出し（obj.field(args)。非該当ならnullptrを返しobj_hirは消費しない）
HirExprPtr HirLowering::lower_member_field_fn_call(ast::MemberExpr& mem, const TypePtr& obj_type,
                                                   HirExprPtr& obj_hir) {
    // 関数型フィールドの呼び出し（obj.field(args)）: implメソッドではなく関数値を保持するフィールドを起動する
    // （JSオブジェクトのメソッド等。呼び出し先はメンバ式のまま保持し、JSバックエンドでのthis束縛を保てるようにする）
    if (obj_type && obj_type->kind == ast::TypeKind::Struct) {
        // 構造体フィールドの解決（struct_defs_）。ジェネリック特殊化名（Box<int*(int,int)>）は
        // 基底名（Box）でstruct_defsを引き、フィールド型を型引数で置換する（局所処理調査「その他」: 関数フィールド呼び出しの取りこぼし解消）
        std::vector<std::pair<std::string, TypePtr>> field_types;
        std::string field_struct_name = obj_type->name;
        {
            auto angle = field_struct_name.find('<');
            if (angle != std::string::npos) {
                field_struct_name = field_struct_name.substr(0, angle);
            }
        }
        auto struct_it = struct_defs_.find(field_struct_name);
        if (struct_it != struct_defs_.end() && struct_it->second) {
            const auto& sd = *struct_it->second;
            for (const auto& field : sd.fields) {
                TypePtr ft = field.type;
                // フィールド型が基底のジェネリックパラメータ（v: T）なら実型引数へ置換する
                if (ft && !obj_type->type_args.empty()) {
                    for (size_t i = 0;
                         i < sd.generic_params.size() && i < obj_type->type_args.size(); ++i) {
                        if (sd.generic_params[i] == ft->name && obj_type->type_args[i]) {
                            ft = obj_type->type_args[i];
                            break;
                        }
                    }
                }
                field_types.emplace_back(field.name, ft);
            }
        }
        {
            for (const auto& [field_name, raw_field_type] : field_types) {
                // 構造化束縛はラムダでキャプチャできないため通常変数へ写す
                const std::string& fname = field_name;
                const TypePtr& ftype = raw_field_type;
                struct FieldView {
                    const std::string& name;
                    const TypePtr& type;
                } field{fname, ftype};
                if (field.name != mem.member || !field.type ||
                    field.type->kind != ast::TypeKind::Function) {
                    continue;
                }
                auto field_call = std::make_unique<HirCall>();
                field_call->func_name = mem.member;
                field_call->is_indirect = true;
                auto member_access = std::make_unique<HirMember>();
                member_access->object = std::move(obj_hir);
                member_access->member = mem.member;
                field_call->indirect_callee =
                    std::make_unique<HirExpr>(std::move(member_access), field.type);
                for (auto& arg : mem.args) {
                    field_call->args.push_back(lower_expr(*arg));
                }
                TypePtr ret_type =
                    field.type->return_type ? field.type->return_type : ast::make_void();
                return std::make_unique<HirExpr>(std::move(field_call), ret_type);
            }
        }
    }
    return nullptr;
}

// メンバアクセス / メソッド呼び出し
HirExprPtr HirLowering::lower_member(ast::MemberExpr& mem, TypePtr type) {
    // メソッド呼び出しの場合
    if (mem.is_method_call) {
        debug::hir::log(
            debug::hir::Id::MethodCallLower,
            "method: " + mem.member + " with " + std::to_string(mem.args.size()) + " args",
            debug::Level::Debug);

        auto obj_hir = lower_expr(*mem.object);
        std::string type_name;

        TypePtr obj_type = nullptr;
        if (obj_hir->type) {
            obj_type = obj_hir->type;
            // type_to_stringでジェネリック型引数を含む形式で取得（Vector<int>）
            // 後でマングリング形式（Vector__int）に変換される
            type_name = ast::type_to_string(*obj_hir->type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "obj_hir->type = " + type_name,
                            debug::Level::Info);
        } else if (mem.object->type) {
            obj_type = mem.object->type;
            type_name = ast::type_to_string(*mem.object->type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "mem.object->type = " + type_name,
                            debug::Level::Info);
        } else {
            debug::hir::log(
                debug::hir::Id::MethodCallLower,
                "WARNING: Both obj_hir->type and mem.object->type are null for method: " +
                    mem.member,
                debug::Level::Warn);
        }

        // obj_typeがnullの場合はデバッグログのみ（フォールバックは使用せず、型チェッカーの設定に依存）

        // 組み込みResult<T,E>/Option<T>のメソッド脱糖の腕
        if (auto sum_result = lower_member_sum_type(mem, obj_type, obj_hir)) {
            return sum_result;
        }

        // 配列のビルトインメソッドの腕
        if (auto array_result = lower_member_array_builtin(mem, obj_type, obj_hir)) {
            return array_result;
        }

        // 動的配列（スライス）のビルトインメソッドの腕
        if (auto slice_result = lower_member_slice_builtin(mem, obj_type, obj_hir, type)) {
            return slice_result;
        }

        // 文字列のビルトインメソッドの腕
        if (auto string_result = lower_member_string_builtin(mem, obj_type, obj_hir)) {
            return string_result;
        }

        // メソッド解決用の型名を正規化し、固定長配列→スライスimplへの変換要否を得る
        bool needs_array_to_slice = false;
        std::string method_type_name =
            resolve_method_type_name(obj_type, type_name, needs_array_to_slice);

        // 関数型フィールドの呼び出しの腕
        if (auto field_call_result = lower_member_field_fn_call(mem, obj_type, obj_hir)) {
            return field_call_result;
        }

        auto hir = std::make_unique<HirCall>();
        hir->func_name = mangle::method_name(method_type_name, mem.member);

        // 固定長配列→スライス変換が必要な場合、cm_array_to_sliceで変換
        if (needs_array_to_slice && obj_type->element_type) {
            // cm_array_to_slice(ptr, len, elem_size) を呼び出してスライスを作成
            auto convert_call = std::make_unique<HirCall>();
            convert_call->func_name = "cm_array_to_slice";

            // 配列のアドレスを取得
            auto addr_op = std::make_unique<HirUnary>();
            addr_op->op = HirUnaryOp::AddrOf;
            addr_op->operand = std::move(obj_hir);
            auto ptr_type = ast::make_pointer(obj_type->element_type);
            convert_call->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));

            // 配列サイズ（コンパイル時定数）
            auto size_lit = std::make_unique<HirLiteral>();
            size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
            convert_call->args.push_back(
                std::make_unique<HirExpr>(std::move(size_lit), ast::make_long()));

            // 要素サイズはHIRでは決めない（プレースホルダ0）。
            // MIRのcm_array_to_slice引数loweringがlayout API（array_elem_stride: typedef解決・集約レイアウト・ターゲット依存ポインタ幅）で再計算する（Z2: 手書きswitchはshort/tiny/構造体/wasmポインタ幅を誤っていた）
            auto elem_size_lit = std::make_unique<HirLiteral>();
            elem_size_lit->value = int64_t{0};
            convert_call->args.push_back(
                std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_long()));

            // 変換結果をself引数として渡す
            auto slice_type = ast::make_array(obj_type->element_type, std::nullopt);
            hir->args.push_back(std::make_unique<HirExpr>(std::move(convert_call), slice_type));
        } else {
            hir->args.push_back(std::move(obj_hir));
        }

        for (auto& arg : mem.args) {
            hir->args.push_back(lower_expr(*arg));
        }

        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 通常のフィールドアクセス
    debug::hir::log(debug::hir::Id::FieldAccessLower, "", debug::Level::Debug);
    auto hir = std::make_unique<HirMember>();
    hir->object = lower_expr(*mem.object);
    hir->member = mem.member;
    debug::hir::log(debug::hir::Id::FieldName, "field: " + mem.member, debug::Level::Trace);
    return std::make_unique<HirExpr>(std::move(hir), type);
}

}  // namespace cm::hir