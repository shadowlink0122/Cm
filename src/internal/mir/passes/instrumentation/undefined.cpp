// --sanitize=undefined 計装パスの実装
// ガードの挿入は基本ブロック分割で行う: 対象文の直前でブロックを分割し、SwitchIntでpanicブロックへ分岐する。
// ゼロ除算: SwitchInt(除数, {0 -> panic}, otherwise -> 続き)
// null参照: %t = Eq(ポインタ, null); SwitchInt(%t, {0 -> 続き}, otherwise -> panic)

#include "undefined.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir::opt {

namespace {

constexpr const char* kDivZeroMessage = "runtime error: division by zero";
constexpr const char* kModZeroMessage = "runtime error: modulo by zero";
constexpr const char* kNullDerefMessage = "runtime error: null pointer dereference";

// 挿入するガード1件分
struct Guard {
    // SwitchIntの判別値（除数のクローン、またはnull比較のポインタPlace）
    MirOperandPtr discriminant;
    // trueなら判別値==0でpanic（ゼロ除算）、falseなら判別値!=0でpanic（Eq結果のnull判定）
    bool panic_on_zero = true;
    // null判定の場合に判別値をEq比較へ変換するためのポインタ型（ゼロ除算ではnullptr）
    hir::TypePtr pointer_type;
    const char* message = kDivZeroMessage;
};

// Move/Copy/Constantのオペランドを読み取り専用のクローンとして複製する（Moveは二重消費を避けるためCopyに変換する）
MirOperandPtr clone_operand_as_copy(const MirOperand& op) {
    switch (op.kind) {
        case MirOperand::Move:
        case MirOperand::Copy:
            return MirOperand::copy(std::get<MirPlace>(op.data), op.type);
        case MirOperand::Constant: {
            auto cloned = std::make_unique<MirOperand>();
            cloned->kind = MirOperand::Constant;
            cloned->data = std::get<MirConstant>(op.data);
            cloned->type = op.type;
            return cloned;
        }
        default:
            return nullptr;
    }
}

// 型がnullでない整数型か（除数チェックの対象判定。浮動小数の除算はIEEE 754で定義済みのため対象外）
bool is_integer_type(const hir::TypePtr& type) {
    return type && type->is_integer();
}

// 型が生ポインタか（null参照チェックの対象判定。参照(&)は生成時点で非nullが保証されるため対象外）
bool is_raw_pointer_type(const hir::TypePtr& type) {
    return type && type->kind == hir::TypeKind::Pointer;
}

// PlaceのDeref投影それぞれについて、参照されるポインタ（プレフィックスPlace）のガードを収集する
void collect_null_guards_from_place(const MirFunction& func, const MirPlace& place,
                                    std::vector<Guard>& guards) {
    for (size_t k = 0; k < place.projections.size(); ++k) {
        if (place.projections[k].kind != ProjectionKind::Deref) {
            continue;
        }
        // Derefの手前までのPlaceがポインタ値。型が生ポインタと確定する場合のみガードする
        hir::TypePtr pointer_type;
        if (k == 0) {
            if (place.local < func.locals.size()) {
                pointer_type = func.locals[place.local].type;
            }
        } else {
            pointer_type = place.projections[k - 1].result_type;
        }
        if (!is_raw_pointer_type(pointer_type)) {
            continue;
        }
        MirPlace prefix{place.local, std::vector<PlaceProjection>(place.projections.begin(),
                                                                  place.projections.begin() + k)};
        Guard guard;
        guard.discriminant = MirOperand::copy(std::move(prefix), pointer_type);
        guard.panic_on_zero = false;
        guard.pointer_type = pointer_type;
        guard.message = kNullDerefMessage;
        guards.push_back(std::move(guard));
    }
}

// オペランドがPlace参照ならnullガードを収集する
void collect_null_guards_from_operand(const MirFunction& func, const MirOperandPtr& op,
                                      std::vector<Guard>& guards) {
    if (!op) {
        return;
    }
    if (op->kind == MirOperand::Move || op->kind == MirOperand::Copy) {
        collect_null_guards_from_place(func, std::get<MirPlace>(op->data), guards);
    }
}

// 整数Div/Modのゼロ除数ガードを収集する（除数が非0定数と確定していれば省略する）
void collect_div_guard(const MirRvalue::BinaryOpData& bin, std::vector<Guard>& guards) {
    if (bin.op != MirBinaryOp::Div && bin.op != MirBinaryOp::Mod) {
        return;
    }
    if (!bin.rhs) {
        return;
    }
    const hir::TypePtr& divisor_type = bin.rhs->type ? bin.rhs->type : bin.result_type;
    if (!is_integer_type(divisor_type)) {
        return;
    }
    if (bin.rhs->kind == MirOperand::Constant) {
        const auto& constant = std::get<MirConstant>(bin.rhs->data);
        if (std::holds_alternative<int64_t>(constant.value) &&
            std::get<int64_t>(constant.value) != 0) {
            return;
        }
    }
    auto cloned = clone_operand_as_copy(*bin.rhs);
    if (!cloned) {
        return;
    }
    Guard guard;
    guard.discriminant = std::move(cloned);
    guard.panic_on_zero = true;
    guard.message = bin.op == MirBinaryOp::Div ? kDivZeroMessage : kModZeroMessage;
    guards.push_back(std::move(guard));
}

// 文が必要とするガードを収集する
std::vector<Guard> collect_statement_guards(const MirFunction& func, const MirStatement& stmt) {
    std::vector<Guard> guards;
    if (stmt.kind != MirStatement::Assign) {
        return guards;
    }
    const auto& assign = std::get<MirStatement::AssignData>(stmt.data);
    collect_null_guards_from_place(func, assign.place, guards);
    if (!assign.rvalue) {
        return guards;
    }
    switch (assign.rvalue->kind) {
        case MirRvalue::Use:
            collect_null_guards_from_operand(
                func, std::get<MirRvalue::UseData>(assign.rvalue->data).operand, guards);
            break;
        case MirRvalue::BinaryOp: {
            const auto& bin = std::get<MirRvalue::BinaryOpData>(assign.rvalue->data);
            collect_null_guards_from_operand(func, bin.lhs, guards);
            collect_null_guards_from_operand(func, bin.rhs, guards);
            collect_div_guard(bin, guards);
            break;
        }
        case MirRvalue::UnaryOp:
            collect_null_guards_from_operand(
                func, std::get<MirRvalue::UnaryOpData>(assign.rvalue->data).operand, guards);
            break;
        case MirRvalue::Ref:
            // 借用のアドレス計算に含まれるDerefもガードする（&(*p).f 形式）
            collect_null_guards_from_place(
                func, std::get<MirRvalue::RefData>(assign.rvalue->data).place, guards);
            break;
        case MirRvalue::Aggregate:
            for (const auto& op :
                 std::get<MirRvalue::AggregateData>(assign.rvalue->data).operands) {
                collect_null_guards_from_operand(func, op, guards);
            }
            break;
        case MirRvalue::Cast:
            collect_null_guards_from_operand(
                func, std::get<MirRvalue::CastData>(assign.rvalue->data).operand, guards);
            break;
        case MirRvalue::FormatConvert:
            collect_null_guards_from_operand(
                func, std::get<MirRvalue::FormatConvertData>(assign.rvalue->data).operand, guards);
            break;
    }
    return guards;
}

// Callターミネータの引数・格納先が必要とするnullガードを収集する
std::vector<Guard> collect_terminator_guards(const MirFunction& func, const MirTerminator& term) {
    std::vector<Guard> guards;
    if (term.kind != MirTerminator::Call) {
        return guards;
    }
    const auto& call = std::get<MirTerminator::CallData>(term.data);
    for (const auto& arg : call.args) {
        collect_null_guards_from_operand(func, arg, guards);
    }
    if (call.destination.has_value()) {
        collect_null_guards_from_place(func, *call.destination, guards);
    }
    return guards;
}

// panicブロック（Call panic(message) -> Unreachable）を作成しブロックIDを返す
BlockId create_panic_block(MirFunction& func, const char* message) {
    BlockId panic_block = func.add_block();
    BlockId unreachable_block = func.add_block();

    auto message_operand = std::make_unique<MirOperand>();
    message_operand->kind = MirOperand::Constant;
    message_operand->data = MirConstant{std::string(message), hir::make_string()};
    message_operand->type = hir::make_string();

    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;
    MirTerminator::CallData call_data;
    call_data.func = MirOperand::function_ref("panic");
    call_data.args.push_back(std::move(message_operand));
    call_data.destination = std::nullopt;
    call_data.success = unreachable_block;
    call_term->data = std::move(call_data);
    func.get_block(panic_block)->set_terminator(std::move(call_term));

    func.get_block(unreachable_block)->set_terminator(MirTerminator::unreachable());
    return panic_block;
}

// ガード連鎖をブロックへ挿入する。guard_blockから始まり、全ガード通過後にcont_blockへ到達する
// 戻り値は最初のガードブロック（呼び出し側が分割元ブロックのターミネータ設定に使う場合のためguard_blockそのもの）
void emit_guard_chain(MirFunction& func, BlockId guard_block, BlockId cont_block,
                      std::vector<Guard> guards) {
    BlockId current = guard_block;
    for (size_t i = 0; i < guards.size(); ++i) {
        Guard& guard = guards[i];
        BlockId next = (i + 1 == guards.size()) ? cont_block : func.add_block();
        BlockId panic_block = create_panic_block(func, guard.message);

        auto switch_term = std::make_unique<MirTerminator>();
        switch_term->kind = MirTerminator::SwitchInt;
        if (guard.panic_on_zero) {
            // ゼロ除算: 除数が0ならpanic、それ以外は続行
            switch_term->data = MirTerminator::SwitchIntData{
                std::move(guard.discriminant), {{0, panic_block}}, next};
        } else {
            // null参照: %t = Eq(ポインタ, null) を挿入し、%tが非0（=null）ならpanic
            LocalId temp = func.add_local("_san_null_check", hir::make_bool(), true, false);
            auto null_constant = std::make_unique<MirOperand>();
            null_constant->kind = MirOperand::Constant;
            null_constant->data = MirConstant{int64_t{0}, guard.pointer_type};
            null_constant->type = guard.pointer_type;
            func.get_block(current)->add_statement(MirStatement::assign(
                MirPlace{temp}, MirRvalue::binary(MirBinaryOp::Eq, std::move(guard.discriminant),
                                                  std::move(null_constant), hir::make_bool())));
            switch_term->data = MirTerminator::SwitchIntData{
                MirOperand::copy(MirPlace{temp}, hir::make_bool()), {{0, next}}, panic_block};
        }
        func.get_block(current)->set_terminator(std::move(switch_term));
        current = next;
    }
}

}  // namespace

bool UndefinedCheckInstrumentation::run(MirFunction& func) {
    bool changed = false;
    // 分割で移動した文・ターミネータを再計装しないための処理済み集合（ポインタ同一性で判定）
    std::unordered_set<const MirStatement*> done_statements;
    std::unordered_set<const MirTerminator*> done_terminators;

    for (size_t bi = 0; bi < func.basic_blocks.size(); ++bi) {
        // add_blockでbasic_blocksが再確保されるため、ブロックポインタは都度取得する
        bool block_split = false;
        auto* block = func.basic_blocks[bi].get();
        if (!block) {
            continue;
        }

        for (size_t si = 0; si < block->statements.size(); ++si) {
            MirStatement* stmt = block->statements[si].get();
            if (!stmt || done_statements.count(stmt)) {
                continue;
            }
            auto guards = collect_statement_guards(func, *stmt);
            done_statements.insert(stmt);
            if (guards.empty()) {
                continue;
            }

            // 対象文の直前で分割: 続きブロックへ対象文以降とターミネータを移す
            BlockId cont_block = func.add_block();
            block = func.basic_blocks[bi].get();
            auto* cont = func.get_block(cont_block);
            for (size_t mi = si; mi < block->statements.size(); ++mi) {
                cont->statements.push_back(std::move(block->statements[mi]));
            }
            block->statements.resize(si);
            cont->set_terminator(std::move(block->terminator));

            emit_guard_chain(func, block->id, cont_block, std::move(guards));
            changed = true;
            block_split = true;
            break;
        }
        if (block_split) {
            continue;
        }

        // ターミネータ（Call引数等）のガード
        block = func.basic_blocks[bi].get();
        if (!block->terminator || done_terminators.count(block->terminator.get())) {
            continue;
        }
        auto guards = collect_terminator_guards(func, *block->terminator);
        done_terminators.insert(block->terminator.get());
        if (guards.empty()) {
            continue;
        }
        BlockId cont_block = func.add_block();
        block = func.basic_blocks[bi].get();
        func.get_block(cont_block)->set_terminator(std::move(block->terminator));
        emit_guard_chain(func, block->id, cont_block, std::move(guards));
        changed = true;
    }

    if (changed) {
        func.build_cfg();
    }
    return changed;
}

void instrument_undefined_checks(MirProgram& program) {
    UndefinedCheckInstrumentation pass;
    for (auto& func : program.functions) {
        if (func) {
            pass.run(*func);
        }
    }
}

}  // namespace cm::mir::opt
