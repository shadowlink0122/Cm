// 単相化 - 特殊化後正準化パス（specialization-canonicalization-framework）
// 総称本体は型パラメータTが未確定のままMIRへローワされるため、HIR段の型駆動脱糖（ユニオン等値のタグ+ペイロード展開・スライス等値の内容比較展開など）が総称本体内の式には適用されない。
// 特殊化はローワ済みMIRへの型置換であり、置換後に「本来なら脱糖されていたはずの生命令」が残る。
// このパスが特殊化で型が確定した文をHIR脱糖と同じ判定表で検出し、正準形へ書き換える。
// 書き換え規則はkRulesへの登録制（対象判定matchesと正準形発行emitの組）。
// 発行はHIR/MIRローワの既存正準実装（cm_lower_union_equality・cm_slice_equal呼び出し生成）を再利用し、このパス内での意味論の再実装は禁止する

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/context.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/mir/lowering/monomorphization.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

namespace {

// 射影なしのCopy/Moveオペランドからローカルを取り出す（それ以外はnullopt）
std::optional<LocalId> plain_local_of(const MirOperandPtr& op, const MirFunction& func) {
    if (!op || (op->kind != MirOperand::Copy && op->kind != MirOperand::Move)) {
        return std::nullopt;
    }
    const auto& pl = std::get<MirPlace>(op->data);
    if (!pl.projections.empty() || pl.local >= func.locals.size()) {
        return std::nullopt;
    }
    return pl.local;
}

// 等値書き換えの対象情報（matchesとemitが共有する抽出結果）
struct EqualityTarget {
    bool is_ne;
    LocalId target;
    LocalId lhs;
    LocalId rhs;
    hir::TypePtr lt;
    hir::TypePtr rt;
    bool l_union;
    bool r_union;
};

// 判定: ユニオン/動的スライス同士の生Eq/Ne（HIRのユニオン等値・スライス等値脱糖と同じ判定表）
std::optional<EqualityTarget> extract_equality(const MirStatement& stmt, const MirFunction& func) {
    if (stmt.kind != MirStatement::Assign) {
        return std::nullopt;
    }
    const auto& ad = std::get<MirStatement::AssignData>(stmt.data);
    if (!ad.rvalue || ad.rvalue->kind != MirRvalue::BinaryOp || !ad.place.projections.empty()) {
        return std::nullopt;
    }
    const auto& bd = std::get<MirRvalue::BinaryOpData>(ad.rvalue->data);
    if (bd.op != MirBinaryOp::Eq && bd.op != MirBinaryOp::Ne) {
        return std::nullopt;
    }
    auto ll = plain_local_of(bd.lhs, func);
    auto rl = plain_local_of(bd.rhs, func);
    if (!ll || !rl) {
        return std::nullopt;
    }
    hir::TypePtr lt = func.locals[*ll].type;
    hir::TypePtr rt = func.locals[*rl].type;
    const bool l_union = lt && lt->kind == hir::TypeKind::Union;
    const bool r_union = rt && rt->kind == hir::TypeKind::Union;
    const bool both_dyn_slice = lt && rt && lt->kind == hir::TypeKind::Array &&
                                rt->kind == hir::TypeKind::Array && !lt->array_size.has_value() &&
                                !rt->array_size.has_value();
    if (!l_union && !r_union && !both_dyn_slice) {
        return std::nullopt;
    }
    return EqualityTarget{
        (bd.op == MirBinaryOp::Ne), ad.place.local, *ll, *rl, lt, rt, l_union, r_union};
}

bool match_equality(const MirStatement& stmt, const MirFunction& func) {
    return extract_equality(stmt, func).has_value();
}

// 発行: ユニオンはタグ+ペイロード比較のCFG（cm_lower_union_equality）、動的スライスはcm_slice_equal内容比較。
// 分割済みブロック末尾のctxへ発行し、元文の宛先への代入まで行う
void emit_equality(const MirStatement& stmt, LoweringContext& ctx) {
    auto t = extract_equality(stmt, *ctx.func);
    if (!t) {
        return;
    }
    LocalId result;
    if (t->l_union || t->r_union) {
        result = cm_lower_union_equality(t->is_ne, t->lhs, t->rhs, t->lt, t->rt, t->l_union,
                                         t->r_union, ctx);
    } else {
        LocalId eq = ctx.new_temp(hir::make_bool());
        BlockId success = ctx.new_block();
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{t->lhs}, t->lt));
        args.push_back(MirOperand::copy(MirPlace{t->rhs}, t->rt));
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_equal"),
                                                  std::move(args),
                                                  MirPlace{eq},
                                                  success,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success);
        if (t->is_ne) {
            LocalId neg = ctx.new_temp(hir::make_bool());
            ctx.push_statement(MirStatement::assign(
                MirPlace{neg}, MirRvalue::unary(MirUnaryOp::Not, MirOperand::copy(MirPlace{eq}))));
            result = neg;
        } else {
            result = eq;
        }
    }
    ctx.push_statement(MirStatement::assign(MirPlace{t->target},
                                            MirRvalue::use(MirOperand::copy(MirPlace{result}))));
}

// 書き換え規則（判定と発行の組）。特殊化後に正準化が必要な生命令の族はここへ追加する
struct CanonicalizeRule {
    const char* name;
    bool (*matches)(const MirStatement& stmt, const MirFunction& func);
    void (*emit)(const MirStatement& stmt, LoweringContext& ctx);
};

constexpr CanonicalizeRule kRules[] = {
    {"equality", match_equality, emit_equality},
};

}  // namespace

// 特殊化関数の正準化パス本体。対象文でブロックを分割し（後続文とターミネータを継続ブロックへ退避）、
// 規則のemitが正準形を発行した後に継続ブロックへ接続する。元の文は分割で切り落とされる
void Monomorphization::canonicalize_specialized_function(MirFunction& func) {
    for (size_t bi = 0; bi < func.basic_blocks.size(); ++bi) {
        auto* block = func.basic_blocks[bi].get();
        if (!block) {
            continue;
        }
        for (size_t si = 0; si < block->statements.size(); ++si) {
            auto& stmt = block->statements[si];
            if (!stmt) {
                continue;
            }
            const CanonicalizeRule* rule = nullptr;
            for (const auto& r : kRules) {
                if (r.matches(*stmt, func)) {
                    rule = &r;
                    break;
                }
            }
            if (!rule) {
                continue;
            }
            debug_msg("MONO", std::string("canonicalize ") + rule->name + " in " + func.name);

            // 対象文を退避してからブロックを分割する（分割で文の所有権が動くため）
            MirStatementPtr target_stmt = std::move(block->statements[si]);
            LoweringContext ctx(&func);
            BlockId cont = ctx.new_block();
            auto* cont_block = func.get_block(cont);
            auto* cur = func.basic_blocks[bi].get();  // new_blockで再取得（vector再確保対策）
            for (size_t mi = si + 1; mi < cur->statements.size(); ++mi) {
                cont_block->statements.push_back(std::move(cur->statements[mi]));
            }
            cur->statements.resize(si);
            cont_block->terminator = std::move(cur->terminator);
            ctx.switch_to_block(static_cast<BlockId>(bi));

            rule->emit(*target_stmt, ctx);
            ctx.set_terminator(MirTerminator::goto_block(cont));
            break;  // このブロックは分割済み。継続ブロックは外側ループが後続indexで走査する
        }
    }
}

}  // namespace cm::mir
