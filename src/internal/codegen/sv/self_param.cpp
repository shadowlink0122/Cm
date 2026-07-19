// SVターゲット向けself値渡し化の実装
// 変換内容:
//   1. 構造体ポインタ引数（*T self）の型を T へ変更し、本体の (*self).field アクセスから先頭のDeref投影を除去する
//   2. 呼び出し側の「_t = &place; call f(copy(_t))」を「call f(copy(place))」へ書き換え、Ref代入をNop化する
//   3. 値渡しで意味が変わるケースは診断エラー: selfへの書き込み（SV010）・動的ディスパッチ（SV011）・ポインタ値の逃避（SV012）

#include "self_param.hpp"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cm::codegen::sv {

namespace {

using namespace cm::mir;

// 構造体を指すポインタ型か（変換対象の判定）
bool is_struct_pointer(const hir::TypePtr& type) {
    return type && type->kind == hir::TypeKind::Pointer && type->element_type &&
           type->element_type->kind == hir::TypeKind::Struct;
}

// 変換対象関数の情報（引数インデックスと引数ローカルID）
struct TransformInfo {
    std::vector<size_t> arg_indices;
    MirFunction* func = nullptr;
};

// オペランドがPlace参照なら、変換対象パラメータ起点の先頭Derefを除去する。
// 投影なしの裸のポインタ使用は呼び出し引数以外では許可しない（エラーは呼び出し側で判定するためここではフラグだけ返す）
void strip_deref_in_place(MirPlace& place, const std::unordered_set<LocalId>& params,
                          bool& bare_use) {
    if (!params.count(place.local)) {
        return;
    }
    if (place.projections.empty()) {
        bare_use = true;
        return;
    }
    if (place.projections[0].kind == ProjectionKind::Deref) {
        place.projections.erase(place.projections.begin());
    }
}

void strip_deref_in_operand(MirOperandPtr& op, const std::unordered_set<LocalId>& params,
                            bool& bare_use) {
    if (!op) {
        return;
    }
    if (op->kind == MirOperand::Move || op->kind == MirOperand::Copy) {
        strip_deref_in_place(std::get<MirPlace>(op->data), params, bare_use);
    }
}

}  // namespace

std::vector<std::string> lower_self_pointer_params(MirProgram& program) {
    std::vector<std::string> errors;

    // 動的ディスパッチ（vtable経由のinterface呼び出し）はハードウェアへ合成できないためエラー
    for (auto& func : program.functions) {
        if (!func) {
            continue;
        }
        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call) {
                continue;
            }
            const auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
            if (call.is_virtual) {
                errors.push_back(
                    "error[SV011]: interface dynamic dispatch is not supported on the SV target "
                    "(function: " +
                    func->name +
                    "). Use a concrete struct type so the call can be resolved statically");
            }
        }
    }

    // 変換対象の収集: 構造体ポインタ引数を持つ関数
    std::unordered_map<std::string, TransformInfo> transform;
    for (auto& func : program.functions) {
        if (!func) {
            continue;
        }
        TransformInfo info;
        info.func = func.get();
        for (size_t i = 0; i < func->arg_locals.size(); ++i) {
            const auto& local = func->locals[func->arg_locals[i]];
            if (is_struct_pointer(local.type)) {
                info.arg_indices.push_back(i);
            }
        }
        if (!info.arg_indices.empty()) {
            transform.emplace(func->name, std::move(info));
        }
    }
    if (transform.empty()) {
        return errors;
    }

    // 本体の書き換え: パラメータ起点のDeref除去・selfへの書き込み検出
    for (auto& func : program.functions) {
        if (!func) {
            continue;
        }
        auto it = transform.find(func->name);
        if (it == transform.end()) {
            continue;
        }
        std::unordered_set<LocalId> params;
        for (size_t idx : it->second.arg_indices) {
            params.insert(func->arg_locals[idx]);
        }

        for (auto& block : func->basic_blocks) {
            if (!block) {
                continue;
            }
            for (auto& stmt : block->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign) {
                    continue;
                }
                auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                // selfへの書き込み: 値渡しでは呼び出し元に反映されないためエラー
                if (params.count(assign.place.local) && !assign.place.projections.empty() &&
                    assign.place.projections[0].kind == ProjectionKind::Deref) {
                    errors.push_back(
                        "error[SV010]: methods that write through 'self' are not supported on "
                        "the SV target (function: " +
                        func->name + ")");
                    continue;
                }
                bool bare_use = false;
                switch (assign.rvalue ? assign.rvalue->kind : MirRvalue::Use) {
                    case MirRvalue::Use:
                        strip_deref_in_operand(
                            std::get<MirRvalue::UseData>(assign.rvalue->data).operand, params,
                            bare_use);
                        break;
                    case MirRvalue::BinaryOp: {
                        auto& bin = std::get<MirRvalue::BinaryOpData>(assign.rvalue->data);
                        strip_deref_in_operand(bin.lhs, params, bare_use);
                        strip_deref_in_operand(bin.rhs, params, bare_use);
                        break;
                    }
                    case MirRvalue::UnaryOp:
                        strip_deref_in_operand(
                            std::get<MirRvalue::UnaryOpData>(assign.rvalue->data).operand, params,
                            bare_use);
                        break;
                    case MirRvalue::Cast:
                        strip_deref_in_operand(
                            std::get<MirRvalue::CastData>(assign.rvalue->data).operand, params,
                            bare_use);
                        break;
                    case MirRvalue::FormatConvert:
                        strip_deref_in_operand(
                            std::get<MirRvalue::FormatConvertData>(assign.rvalue->data).operand,
                            params, bare_use);
                        break;
                    case MirRvalue::Aggregate:
                        for (auto& op :
                             std::get<MirRvalue::AggregateData>(assign.rvalue->data).operands) {
                            strip_deref_in_operand(op, params, bare_use);
                        }
                        break;
                    case MirRvalue::Ref: {
                        // selfの再借用（&self.field 等）はポインタ値を再生成するため未対応
                        auto& ref = std::get<MirRvalue::RefData>(assign.rvalue->data);
                        if (params.count(ref.place.local)) {
                            bare_use = true;
                        }
                        break;
                    }
                }
                if (bare_use) {
                    errors.push_back(
                        "error[SV012]: the 'self' pointer value escapes outside a method call on "
                        "the SV target (function: " +
                        func->name + ")");
                }
            }
            // ターミネータ内のオペランド（呼び出し引数のself.field読み・SwitchInt判別値）もDerefを除去する
            if (block->terminator) {
                if (block->terminator->kind == MirTerminator::Call) {
                    auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
                    for (auto& arg : call.args) {
                        // 裸のself転送は呼び出し側書き換えで型を差し替えるためここでは許可する
                        bool ignored = false;
                        strip_deref_in_operand(arg, params, ignored);
                    }
                } else if (block->terminator->kind == MirTerminator::SwitchInt) {
                    auto& sw = std::get<MirTerminator::SwitchIntData>(block->terminator->data);
                    bool bare_use = false;
                    strip_deref_in_operand(sw.discriminant, params, bare_use);
                }
            }
        }

        // 引数型をポインタから構造体値へ変更する
        for (size_t idx : it->second.arg_indices) {
            auto& local = func->locals[func->arg_locals[idx]];
            local.type = local.type->element_type;
        }
    }

    // 呼び出し側の書き換え: 「_t = &place」+「call f(copy(_t))」→「call f(copy(place))」
    for (auto& func : program.functions) {
        if (!func) {
            continue;
        }
        // この関数自身の変換済みパラメータ（selfをそのまま転送するケースの判定用）
        std::unordered_set<LocalId> own_params;
        if (auto own = transform.find(func->name); own != transform.end()) {
            for (size_t idx : own->second.arg_indices) {
                own_params.insert(func->arg_locals[idx]);
            }
        }

        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call) {
                continue;
            }
            auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call.func || call.func->kind != MirOperand::FunctionRef) {
                continue;
            }
            auto callee_it = transform.find(std::get<std::string>(call.func->data));
            if (callee_it == transform.end()) {
                continue;
            }
            const auto& callee = *callee_it->second.func;
            for (size_t idx : callee_it->second.arg_indices) {
                if (idx >= call.args.size() || !call.args[idx]) {
                    continue;
                }
                auto& arg = call.args[idx];
                if (arg->kind != MirOperand::Move && arg->kind != MirOperand::Copy) {
                    errors.push_back(
                        "error[SV012]: unsupported 'self' argument form for method call on the SV "
                        "target (function: " +
                        func->name + " -> " + callee.name + ")");
                    continue;
                }
                auto& arg_place = std::get<MirPlace>(arg->data);
                const hir::TypePtr callee_param_type = callee.locals[callee.arg_locals[idx]].type;
                if (own_params.count(arg_place.local) && arg_place.projections.empty()) {
                    // selfの転送: パラメータは既に値型へ変更済みのため型情報のみ更新する
                    arg->type = callee_param_type;
                    continue;
                }
                // 同一ブロック内の「_t = &place」定義を探して直接値渡しへ書き換える
                bool rewritten = false;
                for (auto stmt_it = block->statements.rbegin(); stmt_it != block->statements.rend();
                     ++stmt_it) {
                    auto& stmt = *stmt_it;
                    if (!stmt || stmt->kind != MirStatement::Assign) {
                        continue;
                    }
                    auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                    if (assign.place.local != arg_place.local ||
                        !assign.place.projections.empty() || !assign.rvalue ||
                        assign.rvalue->kind != MirRvalue::Ref) {
                        continue;
                    }
                    auto& ref = std::get<MirRvalue::RefData>(assign.rvalue->data);
                    arg = MirOperand::copy(ref.place, callee_param_type);
                    stmt->kind = MirStatement::Nop;
                    stmt->data = std::monostate{};
                    rewritten = true;
                    break;
                }
                if (!rewritten) {
                    errors.push_back(
                        "error[SV012]: could not rewrite the 'self' argument to pass-by-value on "
                        "the SV target (function: " +
                        func->name + " -> " + callee.name + ")");
                }
            }
        }
    }

    return errors;
}

}  // namespace cm::codegen::sv
