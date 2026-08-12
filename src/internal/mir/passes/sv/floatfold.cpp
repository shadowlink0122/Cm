// SVターゲット専用: 定数計算可能な浮動小数チェーンの整数畳み込み（仕様はヘッダを参照）

#include "floatfold.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cm::mir::opt {

namespace {

// 追跡中の定数値（整数と浮動小数の別を保持する）
struct ConstVal {
    bool is_float = false;
    double f = 0.0;
    int64_t i = 0;
};

double as_double(const ConstVal& v) {
    return v.is_float ? v.f : static_cast<double>(v.i);
}

bool is_float_kind(const hir::TypePtr& t) {
    return t && (t->kind == hir::TypeKind::Float || t->kind == hir::TypeKind::Double ||
                 t->kind == hir::TypeKind::UFloat || t->kind == hir::TypeKind::UDouble);
}

// オペランドから定数値を取り出す（定数リテラル・既知ローカル・グローバルconstの引き写し）
std::optional<ConstVal> operand_value(const MirOperandPtr& op, const MirFunction& func,
                                      const std::unordered_map<LocalId, ConstVal>& known,
                                      const std::unordered_map<std::string, ConstVal>& globals) {
    if (!op) {
        return std::nullopt;
    }
    if (op->kind == MirOperand::Constant) {
        const auto& c = std::get<MirConstant>(op->data);
        if (const auto* iv = std::get_if<int64_t>(&c.value)) {
            return ConstVal{false, 0.0, *iv};
        }
        if (const auto* dv = std::get_if<double>(&c.value)) {
            return ConstVal{true, *dv, 0};
        }
        return std::nullopt;
    }
    if (op->kind == MirOperand::Copy || op->kind == MirOperand::Move) {
        const auto& pl = std::get<MirPlace>(op->data);
        if (!pl.projections.empty() || pl.local >= func.locals.size()) {
            return std::nullopt;
        }
        if (auto it = known.find(pl.local); it != known.end()) {
            return it->second;
        }
        // モジュールconstの引き写しローカル（非mutable）は名前でグローバルconst表を引く
        if (!func.locals[pl.local].is_mutable) {
            if (auto git = globals.find(func.locals[pl.local].name); git != globals.end()) {
                return git->second;
            }
        }
    }
    return std::nullopt;
}

// オペランドが参照するローカルへ使用数を計上する（placeの添字投影ローカル含む）
void count_operand(const MirOperandPtr& op, std::vector<int>& uses) {
    if (!op || (op->kind != MirOperand::Copy && op->kind != MirOperand::Move)) {
        return;
    }
    const auto& pl = std::get<MirPlace>(op->data);
    if (pl.local < uses.size()) {
        uses[pl.local]++;
    }
    for (const auto& proj : pl.projections) {
        if (proj.kind == ProjectionKind::Index && proj.index_local < uses.size()) {
            uses[proj.index_local]++;
        }
    }
}

// 関数内の全読み取り使用を数える。未知の文/rvalue/ターミネータ種があればfalse（保守的に削除を断念）
bool count_uses(const MirFunction& func, std::vector<int>& uses) {
    uses.assign(func.locals.size(), 0);
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (!stmt) {
                continue;
            }
            switch (stmt->kind) {
                case MirStatement::Nop:
                    break;
                case MirStatement::StorageLive:
                case MirStatement::StorageDead: {
                    // 有効範囲注釈は参照として数える（対象ローカルの削除を防ぐ安全側）
                    const auto& sd = std::get<MirStatement::StorageData>(stmt->data);
                    if (sd.local < uses.size()) {
                        uses[sd.local]++;
                    }
                    break;
                }
                case MirStatement::Assign: {
                    const auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                    // 代入先の添字投影ローカルは読み取り
                    for (const auto& proj : ad.place.projections) {
                        if (proj.kind == ProjectionKind::Index && proj.index_local < uses.size()) {
                            uses[proj.index_local]++;
                        }
                    }
                    if (!ad.rvalue) {
                        break;
                    }
                    switch (ad.rvalue->kind) {
                        case MirRvalue::Use:
                            count_operand(std::get<MirRvalue::UseData>(ad.rvalue->data).operand,
                                          uses);
                            break;
                        case MirRvalue::BinaryOp: {
                            const auto& bd = std::get<MirRvalue::BinaryOpData>(ad.rvalue->data);
                            count_operand(bd.lhs, uses);
                            count_operand(bd.rhs, uses);
                            break;
                        }
                        case MirRvalue::UnaryOp:
                            count_operand(std::get<MirRvalue::UnaryOpData>(ad.rvalue->data).operand,
                                          uses);
                            break;
                        case MirRvalue::Cast:
                            count_operand(std::get<MirRvalue::CastData>(ad.rvalue->data).operand,
                                          uses);
                            break;
                        case MirRvalue::Ref: {
                            const auto& rd = std::get<MirRvalue::RefData>(ad.rvalue->data);
                            if (rd.place.local < uses.size()) {
                                uses[rd.place.local]++;
                            }
                            for (const auto& proj : rd.place.projections) {
                                if (proj.kind == ProjectionKind::Index &&
                                    proj.index_local < uses.size()) {
                                    uses[proj.index_local]++;
                                }
                            }
                            break;
                        }
                        case MirRvalue::Aggregate: {
                            const auto& ag = std::get<MirRvalue::AggregateData>(ad.rvalue->data);
                            for (const auto& o : ag.operands) {
                                count_operand(o, uses);
                            }
                            break;
                        }
                        default:
                            return false;
                    }
                    break;
                }
                default:
                    return false;
            }
        }
        if (!block->terminator) {
            continue;
        }
        switch (block->terminator->kind) {
            case MirTerminator::Goto:
            case MirTerminator::Return:
            case MirTerminator::Unreachable:
                break;
            case MirTerminator::SwitchInt:
                count_operand(
                    std::get<MirTerminator::SwitchIntData>(block->terminator->data).discriminant,
                    uses);
                break;
            case MirTerminator::Call: {
                const auto& cd = std::get<MirTerminator::CallData>(block->terminator->data);
                count_operand(cd.func, uses);
                for (const auto& a : cd.args) {
                    count_operand(a, uses);
                }
                if (cd.destination) {
                    for (const auto& proj : cd.destination->projections) {
                        if (proj.kind == ProjectionKind::Index && proj.index_local < uses.size()) {
                            uses[proj.index_local]++;
                        }
                    }
                }
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

}  // namespace

void sv_fold_constant_float_chains(MirProgram& program) {
    // グローバルconstの値表（名前→値。名前空間なし名でも引けるよう両形を登録）
    std::unordered_map<std::string, ConstVal> global_consts;
    auto register_global = [&](const std::string& name, const ConstVal& v) {
        global_consts[name] = v;
        auto pos = name.rfind("::");
        if (pos != std::string::npos) {
            global_consts[name.substr(pos + 2)] = v;
        }
    };
    for (const auto& gv : program.global_vars) {
        if (!gv || !gv->is_const || !gv->init_value) {
            continue;
        }
        if (const auto* iv = std::get_if<int64_t>(&gv->init_value->value)) {
            register_global(gv->name, ConstVal{false, 0.0, *iv});
        } else if (const auto* dv = std::get_if<double>(&gv->init_value->value)) {
            register_global(gv->name, ConstVal{true, *dv, 0});
        }
    }

    for (auto& func : program.functions) {
        if (!func) {
            continue;
        }
        // floatローカルが無い関数は対象外
        bool has_float_local = false;
        for (const auto& local : func->locals) {
            if (is_float_kind(local.type)) {
                has_float_local = true;
                break;
            }
        }
        if (!has_float_local) {
            continue;
        }

        // 代入回数を数え、単一代入のローカルだけ値を追跡する（分岐で値が分かれるmutableを誤畳みしない）
        std::vector<int> assign_counts(func->locals.size(), 0);
        for (const auto& block : func->basic_blocks) {
            if (!block) {
                continue;
            }
            for (const auto& stmt : block->statements) {
                if (stmt && stmt->kind == MirStatement::Assign) {
                    const auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                    if (ad.place.projections.empty() && ad.place.local < assign_counts.size()) {
                        assign_counts[ad.place.local]++;
                    }
                }
            }
        }

        // 走査: 定数値を伝播し、float→int縮小castを整数定数へ書き換える
        std::unordered_map<LocalId, ConstVal> known;
        bool folded_any = false;
        for (auto& block : func->basic_blocks) {
            if (!block) {
                continue;
            }
            for (auto& stmt : block->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign) {
                    continue;
                }
                auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                if (!ad.place.projections.empty() || !ad.rvalue) {
                    continue;
                }
                const LocalId dst = ad.place.local;
                const bool trackable = dst < assign_counts.size() && assign_counts[dst] == 1;
                auto record = [&](const ConstVal& v) {
                    if (trackable) {
                        known[dst] = v;
                    }
                };
                switch (ad.rvalue->kind) {
                    case MirRvalue::Use: {
                        auto v =
                            operand_value(std::get<MirRvalue::UseData>(ad.rvalue->data).operand,
                                          *func, known, global_consts);
                        if (v) {
                            record(*v);
                        }
                        break;
                    }
                    case MirRvalue::Cast: {
                        auto& cd = std::get<MirRvalue::CastData>(ad.rvalue->data);
                        if (cd.check_only || !cd.iface_concrete.empty()) {
                            break;
                        }
                        auto v = operand_value(cd.operand, *func, known, global_consts);
                        if (!v || !cd.target_type) {
                            break;
                        }
                        if (is_float_kind(cd.target_type)) {
                            record(ConstVal{true, as_double(*v), 0});
                        } else if (cd.target_type->is_integer() && v->is_float) {
                            // 畳み込み点: float→int縮小castを整数定数へ置換（ゼロ方向切り捨て=fptosi/fptoui同等）
                            const int64_t iv = static_cast<int64_t>(v->f);
                            MirConstant c;
                            c.value = iv;
                            c.type = cd.target_type;
                            ad.rvalue = MirRvalue::use(MirOperand::constant(std::move(c)));
                            record(ConstVal{false, 0.0, iv});
                            folded_any = true;
                        } else if (cd.target_type->is_integer()) {
                            record(*v);
                        }
                        break;
                    }
                    case MirRvalue::BinaryOp: {
                        auto& bd = std::get<MirRvalue::BinaryOpData>(ad.rvalue->data);
                        auto lv = operand_value(bd.lhs, *func, known, global_consts);
                        auto rv = operand_value(bd.rhs, *func, known, global_consts);
                        if (!lv || !rv) {
                            break;
                        }
                        // 値追跡のみ（書き換えはfloat→int縮小castの位置で行う）。除算は0除算を避ける
                        const bool as_float = lv->is_float || rv->is_float;
                        if (as_float) {
                            const double a = as_double(*lv);
                            const double b = as_double(*rv);
                            double r = 0.0;
                            if (bd.op == MirBinaryOp::Add) {
                                r = a + b;
                            } else if (bd.op == MirBinaryOp::Sub) {
                                r = a - b;
                            } else if (bd.op == MirBinaryOp::Mul) {
                                r = a * b;
                            } else if (bd.op == MirBinaryOp::Div && b != 0.0) {
                                r = a / b;
                            } else {
                                break;
                            }
                            record(ConstVal{true, r, 0});
                        } else {
                            const int64_t a = lv->i;
                            const int64_t b = rv->i;
                            int64_t r = 0;
                            if (bd.op == MirBinaryOp::Add) {
                                r = a + b;
                            } else if (bd.op == MirBinaryOp::Sub) {
                                r = a - b;
                            } else if (bd.op == MirBinaryOp::Mul) {
                                r = a * b;
                            } else if (bd.op == MirBinaryOp::Div && b != 0) {
                                r = a / b;
                            } else {
                                break;
                            }
                            record(ConstVal{false, 0.0, r});
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        if (!folded_any) {
            continue;
        }

        // 未参照になったfloat定義文をNop化する（Nopは発行対象外）。連鎖するため不動点まで反復
        std::vector<int> uses;
        bool changed = true;
        while (changed) {
            changed = false;
            if (!count_uses(*func, uses)) {
                break;
            }
            for (auto& block : func->basic_blocks) {
                if (!block) {
                    continue;
                }
                for (auto& stmt : block->statements) {
                    if (!stmt || stmt->kind != MirStatement::Assign) {
                        continue;
                    }
                    auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                    if (!ad.place.projections.empty()) {
                        continue;
                    }
                    const LocalId dst = ad.place.local;
                    if (dst < func->locals.size() && is_float_kind(func->locals[dst].type) &&
                        known.count(dst) && uses[dst] == 0) {
                        stmt->kind = MirStatement::Nop;
                        stmt->data = std::monostate{};
                        changed = true;
                    }
                }
            }
        }

        // 参照も代入も残っていないfloatローカルを整数型へ差し替える（SV004検証はローカル型を全数走査するため）
        if (!count_uses(*func, uses)) {
            continue;
        }
        std::vector<bool> still_assigned(func->locals.size(), false);
        for (const auto& block : func->basic_blocks) {
            if (!block) {
                continue;
            }
            for (const auto& stmt : block->statements) {
                if (stmt && stmt->kind == MirStatement::Assign) {
                    const auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                    if (ad.place.local < still_assigned.size()) {
                        still_assigned[ad.place.local] = true;
                    }
                }
            }
            if (block->terminator && block->terminator->kind == MirTerminator::Call) {
                const auto& cd = std::get<MirTerminator::CallData>(block->terminator->data);
                if (cd.destination && cd.destination->local < still_assigned.size()) {
                    still_assigned[cd.destination->local] = true;
                }
            }
        }
        for (auto& local : func->locals) {
            if (is_float_kind(local.type) && local.id < uses.size() && uses[local.id] == 0 &&
                !still_assigned[local.id]) {
                local.type = hir::make_int();
            }
        }
    }
}

}  // namespace cm::mir::opt
