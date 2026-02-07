#pragma once

#include "../core/base.hpp"

#include <queue>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 疎条件定数伝播（SCCP）
// ============================================================
class SparseConditionalConstantPropagation : public OptimizationPass {
   public:
    std::string name() const override { return "Sparse Conditional Constant Propagation"; }

    bool run(MirFunction& func) override {
        if (func.basic_blocks.empty()) {
            return false;
        }

        rebuild_cfg(func);

        const size_t block_count = func.basic_blocks.size();
        const size_t local_count = func.locals.size();

        std::vector<std::vector<LatticeValue>> in_states(block_count,
                                                         std::vector<LatticeValue>(local_count));
        std::vector<std::vector<LatticeValue>> out_states(block_count,
                                                          std::vector<LatticeValue>(local_count));
        std::vector<bool> reachable(block_count, false);

        // 関数引数はOverdefinedに初期化（呼び出し元から任意の値が渡される可能性がある）
        for (LocalId arg : func.arg_locals) {
            if (arg < local_count) {
                for (size_t b = 0; b < block_count; ++b) {
                    in_states[b][arg] = {LatticeKind::Overdefined, {}};
                    out_states[b][arg] = {LatticeKind::Overdefined, {}};
                }
            }
        }

        analyze(func, in_states, out_states, reachable);

        bool changed = false;
        changed |= apply_constants(func, in_states);

        // デストラクタ関数の場合、ブロック削除をスキップ
        // モノモフィゼーションで生成されたループブロックが誤って到達不可能と判定されることがある
        bool is_destructor = func.name.find("__dtor") != std::string::npos;
        if (!is_destructor) {
            changed |= simplify_cfg(func);
            changed |= remove_unreachable_blocks(func);
        }

        return changed;
    }

   private:
    enum class LatticeKind { Undefined, Constant, Overdefined };

    struct LatticeValue {
        LatticeKind kind = LatticeKind::Undefined;
        MirConstant constant{};
    };

    void rebuild_cfg(MirFunction& func) {
        for (auto& block : func.basic_blocks) {
            if (block) {
                block->update_successors();
            }
        }
        func.build_cfg();
    }

    bool same_type(const hir::TypePtr& a, const hir::TypePtr& b) const {
        if (a == b) {
            return true;
        }
        if (!a || !b) {
            return false;
        }
        if (a->kind != b->kind) {
            return false;
        }
        switch (a->kind) {
            case hir::TypeKind::Pointer:
            case hir::TypeKind::Reference:
                return same_type(a->element_type, b->element_type);
            case hir::TypeKind::Array:
                return a->array_size == b->array_size &&
                       same_type(a->element_type, b->element_type);
            case hir::TypeKind::Struct:
            case hir::TypeKind::Interface:
            case hir::TypeKind::TypeAlias:
            case hir::TypeKind::Generic: {
                if (a->name != b->name) {
                    return false;
                }
                if (a->type_args.size() != b->type_args.size()) {
                    return false;
                }
                for (size_t i = 0; i < a->type_args.size(); ++i) {
                    if (!same_type(a->type_args[i], b->type_args[i])) {
                        return false;
                    }
                }
                return true;
            }
            case hir::TypeKind::Function: {
                if (!same_type(a->return_type, b->return_type)) {
                    return false;
                }
                if (a->param_types.size() != b->param_types.size()) {
                    return false;
                }
                for (size_t i = 0; i < a->param_types.size(); ++i) {
                    if (!same_type(a->param_types[i], b->param_types[i])) {
                        return false;
                    }
                }
                return true;
            }
            default:
                return true;
        }
    }

    bool equal_constant(const MirConstant& a, const MirConstant& b) const {
        if (!same_type(a.type, b.type)) {
            return false;
        }
        return a.value == b.value;
    }

    bool equal_value(const LatticeValue& a, const LatticeValue& b) const {
        if (a.kind != b.kind) {
            return false;
        }
        if (a.kind != LatticeKind::Constant) {
            return true;
        }
        return equal_constant(a.constant, b.constant);
    }

    LatticeValue meet(const LatticeValue& a, const LatticeValue& b) const {
        if (a.kind == LatticeKind::Undefined) {
            return b;
        }
        if (b.kind == LatticeKind::Undefined) {
            return a;
        }
        if (a.kind == LatticeKind::Overdefined || b.kind == LatticeKind::Overdefined) {
            return {LatticeKind::Overdefined, {}};
        }
        if (equal_constant(a.constant, b.constant)) {
            return a;
        }
        return {LatticeKind::Overdefined, {}};
    }

    void analyze(const MirFunction& func, std::vector<std::vector<LatticeValue>>& in_states,
                 std::vector<std::vector<LatticeValue>>& out_states, std::vector<bool>& reachable) {
        std::queue<BlockId> worklist;
        const BlockId entry = func.entry_block;

        if (!func.get_block(entry)) {
            return;
        }

        reachable[entry] = true;
        worklist.push(entry);

        // 無限ループ防止のための反復回数制限
        constexpr size_t max_iterations = 10000;
        size_t iterations = 0;

        while (!worklist.empty() && iterations < max_iterations) {
            ++iterations;

            BlockId block_id = worklist.front();
            worklist.pop();

            if (!reachable[block_id]) {
                continue;
            }

            const BasicBlock* block = func.get_block(block_id);
            if (!block) {
                continue;
            }

            auto merged_in = merge_predecessors(func, block_id, out_states, reachable);

            // エントリブロックの場合、関数引数はOverdefinedを維持
            if (block_id == entry) {
                for (LocalId arg : func.arg_locals) {
                    if (arg < merged_in.size()) {
                        merged_in[arg] = {LatticeKind::Overdefined, {}};
                    }
                }
            }

            bool in_changed = !states_equal(merged_in, in_states[block_id]);
            if (in_changed) {
                in_states[block_id] = std::move(merged_in);
            }

            auto new_out = transfer_block(func, *block, in_states[block_id]);
            bool out_changed = !states_equal(new_out, out_states[block_id]);
            if (out_changed) {
                out_states[block_id] = std::move(new_out);
            }

            std::vector<BlockId> successors =
                compute_successors(func, *block, out_states[block_id]);
            for (BlockId succ : successors) {
                if (succ >= reachable.size()) {
                    continue;
                }
                if (!reachable[succ]) {
                    reachable[succ] = true;
                    worklist.push(succ);
                } else if (out_changed || in_changed) {
                    worklist.push(succ);
                }
            }
        }
    }

    std::vector<LatticeValue> merge_predecessors(
        const MirFunction& func, BlockId block_id,
        const std::vector<std::vector<LatticeValue>>& out_states,
        const std::vector<bool>& reachable) {
        std::vector<LatticeValue> merged(out_states[block_id].size());

        const BasicBlock* block = func.get_block(block_id);
        if (!block) {
            return merged;
        }

        bool has_pred = false;
        for (BlockId pred : block->predecessors) {
            if (pred >= out_states.size() || !reachable[pred]) {
                continue;
            }
            if (!func.get_block(pred)) {
                continue;
            }
            if (!has_pred) {
                merged = out_states[pred];
                has_pred = true;
                continue;
            }
            for (size_t i = 0; i < merged.size(); ++i) {
                merged[i] = meet(merged[i], out_states[pred][i]);
            }
        }

        return merged;
    }

    std::vector<LatticeValue> transfer_block(const MirFunction& func, const BasicBlock& block,
                                             const std::vector<LatticeValue>& in_state) {
        std::vector<LatticeValue> state = in_state;

        for (const auto& stmt : block.statements) {
            if (!stmt) {
                continue;
            }

            // Asmステートメント: 出力オペランドの変数をOverdefinedにマーク
            // インラインアセンブリは実行時に変数を変更するため、定数伝播を抑制
            if (stmt->kind == MirStatement::Asm) {
                const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                for (const auto& operand : asm_data.operands) {
                    // 出力オペランド（+r, =rなど）は定数として扱えない
                    if (!operand.constraint.empty() &&
                        (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                        if (operand.local_id < state.size()) {
                            state[operand.local_id] = {LatticeKind::Overdefined, {}};
                        }
                    }
                }
                continue;
            }

            if (stmt->kind != MirStatement::Assign) {
                continue;
            }
            // no_optフラグがtrueの場合は最適化スキップ（Overdefinedとして扱う）
            if (stmt->no_opt) {
                const auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                if (assign_data.place.local < state.size()) {
                    state[assign_data.place.local] = {LatticeKind::Overdefined, {}};
                }
                continue;
            }
            const auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
            if (assign_data.place.local >= state.size()) {
                continue;
            }
            if (!assign_data.place.projections.empty()) {
                // プロジェクションがある場合（フィールドアクセス、インデックス、デリファレンス）
                // デリファレンス書き込みはエイリアスの可能性があるため、
                // 全てのローカル変数をOverdefinedにする（保守的）
                bool has_deref = false;
                for (const auto& proj : assign_data.place.projections) {
                    if (proj.kind == ProjectionKind::Deref) {
                        has_deref = true;
                        break;
                    }
                }
                if (has_deref) {
                    // ポインタ経由の書き込みは任意のローカル変数に影響する可能性がある
                    for (size_t i = 0; i < state.size(); ++i) {
                        state[i] = {LatticeKind::Overdefined, {}};
                    }
                } else {
                    // フィールドやインデックスアクセスの場合は対象変数のみOverdefined
                    state[assign_data.place.local] = {LatticeKind::Overdefined, {}};
                }
                continue;
            }
            // MirRvalue::Ref（アドレス取得 &var）の場合、ターゲット変数をOverdefinedにする
            // ポinter経由で変更される可能性があるため、定数として扱えない
            if (assign_data.rvalue && assign_data.rvalue->kind == MirRvalue::Ref) {
                const auto& ref_data = std::get<MirRvalue::RefData>(assign_data.rvalue->data);
                if (ref_data.place.local < state.size()) {
                    state[ref_data.place.local] = {LatticeKind::Overdefined, {}};
                }
            }

            LatticeValue value = eval_rvalue(func, *assign_data.rvalue, state);
            if (value.kind == LatticeKind::Constant) {
                if (!can_bind_constant(func, assign_data.place.local, value.constant)) {
                    value.kind = LatticeKind::Overdefined;
                }
            }
            state[assign_data.place.local] = value;
        }

        // Call終端子のdestinationを処理
        // 関数呼び出しの戻り値は実行時に決まるため、Overdefinedにする
        if (block.terminator && block.terminator->kind == MirTerminator::Call) {
            const auto& call_data = std::get<MirTerminator::CallData>(block.terminator->data);
            if (call_data.destination) {
                LocalId dest = call_data.destination->local;
                if (dest < state.size()) {
                    state[dest] = {LatticeKind::Overdefined, {}};
                }
            }
        }

        return state;
    }

    std::vector<BlockId> compute_successors(const MirFunction& func, const BasicBlock& block,
                                            const std::vector<LatticeValue>& state) {
        std::vector<BlockId> successors;
        if (!block.terminator) {
            return successors;
        }

        switch (block.terminator->kind) {
            case MirTerminator::Goto: {
                const auto& data = std::get<MirTerminator::GotoData>(block.terminator->data);
                successors.push_back(data.target);
                break;
            }
            case MirTerminator::SwitchInt: {
                const auto& data = std::get<MirTerminator::SwitchIntData>(block.terminator->data);
                LatticeValue discr = eval_operand(func, *data.discriminant, state);
                if (discr.kind == LatticeKind::Constant) {
                    if (auto* v = std::get_if<int64_t>(&discr.constant.value)) {
                        BlockId target = data.otherwise;
                        for (const auto& [case_value, case_target] : data.targets) {
                            if (case_value == *v) {
                                target = case_target;
                                break;
                            }
                        }
                        successors.push_back(target);
                        break;
                    }
                }
                for (const auto& [_, target] : data.targets) {
                    successors.push_back(target);
                }
                successors.push_back(data.otherwise);
                break;
            }
            case MirTerminator::Call: {
                const auto& data = std::get<MirTerminator::CallData>(block.terminator->data);
                successors.push_back(data.success);
                if (data.unwind) {
                    successors.push_back(*data.unwind);
                }
                break;
            }
            default:
                break;
        }

        return successors;
    }

    bool states_equal(const std::vector<LatticeValue>& a,
                      const std::vector<LatticeValue>& b) const {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (!equal_value(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }

    LatticeValue eval_operand(const MirFunction& func, const MirOperand& operand,
                              const std::vector<LatticeValue>& state) {
        if (operand.kind == MirOperand::Constant) {
            if (auto* constant = std::get_if<MirConstant>(&operand.data)) {
                return {LatticeKind::Constant, *constant};
            }
        }

        if ((operand.kind == MirOperand::Copy || operand.kind == MirOperand::Move)) {
            if (auto* place = std::get_if<MirPlace>(&operand.data)) {
                if (!place->projections.empty()) {
                    return {LatticeKind::Overdefined, {}};
                }
                if (place->local < state.size()) {
                    return state[place->local];
                }
            }
        }

        (void)func;
        return {LatticeKind::Overdefined, {}};
    }

    LatticeValue eval_rvalue(const MirFunction& func, const MirRvalue& rvalue,
                             const std::vector<LatticeValue>& state) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                if (auto* use_data = std::get_if<MirRvalue::UseData>(&rvalue.data)) {
                    if (!use_data->operand) {
                        return {LatticeKind::Overdefined, {}};
                    }
                    return eval_operand(func, *use_data->operand, state);
                }
                return {LatticeKind::Overdefined, {}};
            }
            case MirRvalue::BinaryOp: {
                const auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                LatticeValue lhs = eval_operand(func, *bin_data.lhs, state);
                LatticeValue rhs = eval_operand(func, *bin_data.rhs, state);
                if (lhs.kind == LatticeKind::Overdefined || rhs.kind == LatticeKind::Overdefined) {
                    return {LatticeKind::Overdefined, {}};
                }
                if (lhs.kind == LatticeKind::Undefined || rhs.kind == LatticeKind::Undefined) {
                    return {LatticeKind::Undefined, {}};
                }
                auto folded = eval_binary_op(bin_data.op, lhs.constant, rhs.constant);
                if (folded) {
                    return {LatticeKind::Constant, *folded};
                }
                return {LatticeKind::Overdefined, {}};
            }
            case MirRvalue::UnaryOp: {
                const auto& unary_data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                LatticeValue operand = eval_operand(func, *unary_data.operand, state);
                if (operand.kind == LatticeKind::Overdefined) {
                    return {LatticeKind::Overdefined, {}};
                }
                if (operand.kind == LatticeKind::Undefined) {
                    return {LatticeKind::Undefined, {}};
                }
                auto folded = eval_unary_op(unary_data.op, operand.constant);
                if (folded) {
                    return {LatticeKind::Constant, *folded};
                }
                return {LatticeKind::Overdefined, {}};
            }
            default:
                break;
        }

        return {LatticeKind::Overdefined, {}};
    }

    bool can_bind_constant(const MirFunction& func, LocalId local,
                           const MirConstant& constant) const {
        if (local >= func.locals.size()) {
            return false;
        }
        return same_type(func.locals[local].type, constant.type);
    }

    std::optional<MirConstant> eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                              const MirConstant& rhs) {
        if (auto* lhs_int = std::get_if<int64_t>(&lhs.value)) {
            if (auto* rhs_int = std::get_if<int64_t>(&rhs.value)) {
                MirConstant result;
                result.type = lhs.type;
                switch (op) {
                    case MirBinaryOp::Add:
                        result.value = *lhs_int + *rhs_int;
                        return result;
                    case MirBinaryOp::Sub:
                        result.value = *lhs_int - *rhs_int;
                        return result;
                    case MirBinaryOp::Mul:
                        result.value = *lhs_int * *rhs_int;
                        return result;
                    case MirBinaryOp::Div:
                        if (*rhs_int != 0) {
                            result.value = *lhs_int / *rhs_int;
                            return result;
                        }
                        break;
                    case MirBinaryOp::Mod:
                        if (*rhs_int != 0) {
                            result.value = *lhs_int % *rhs_int;
                            return result;
                        }
                        break;
                    case MirBinaryOp::BitAnd:
                        result.value = *lhs_int & *rhs_int;
                        return result;
                    case MirBinaryOp::BitOr:
                        result.value = *lhs_int | *rhs_int;
                        return result;
                    case MirBinaryOp::BitXor:
                        result.value = *lhs_int ^ *rhs_int;
                        return result;
                    case MirBinaryOp::Shl:
                        result.value = *lhs_int << *rhs_int;
                        return result;
                    case MirBinaryOp::Shr:
                        result.value = *lhs_int >> *rhs_int;
                        return result;
                    case MirBinaryOp::Eq:
                        result.value = (*lhs_int == *rhs_int);
                        return result;
                    case MirBinaryOp::Ne:
                        result.value = (*lhs_int != *rhs_int);
                        return result;
                    case MirBinaryOp::Lt:
                        result.value = (*lhs_int < *rhs_int);
                        return result;
                    case MirBinaryOp::Le:
                        result.value = (*lhs_int <= *rhs_int);
                        return result;
                    case MirBinaryOp::Gt:
                        result.value = (*lhs_int > *rhs_int);
                        return result;
                    case MirBinaryOp::Ge:
                        result.value = (*lhs_int >= *rhs_int);
                        return result;
                    default:
                        break;
                }
            }
        }

        if (auto* lhs_bool = std::get_if<bool>(&lhs.value)) {
            if (auto* rhs_bool = std::get_if<bool>(&rhs.value)) {
                MirConstant result;
                result.type = lhs.type;
                switch (op) {
                    case MirBinaryOp::Eq:
                        result.value = (*lhs_bool == *rhs_bool);
                        return result;
                    case MirBinaryOp::Ne:
                        result.value = (*lhs_bool != *rhs_bool);
                        return result;
                    default:
                        break;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<MirConstant> eval_unary_op(MirUnaryOp op, const MirConstant& operand) {
        MirConstant result;
        result.type = operand.type;

        if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
            switch (op) {
                case MirUnaryOp::Neg:
                    result.value = -*int_val;
                    return result;
                case MirUnaryOp::BitNot:
                    result.value = ~*int_val;
                    return result;
                default:
                    break;
            }
        }

        if (auto* bool_val = std::get_if<bool>(&operand.value)) {
            if (op == MirUnaryOp::Not) {
                result.value = !*bool_val;
                return result;
            }
        }

        return std::nullopt;
    }

    bool apply_constants(MirFunction& func,
                         const std::vector<std::vector<LatticeValue>>& in_states) {
        bool changed = false;

        for (auto& block : func.basic_blocks) {
            if (!block) {
                continue;
            }
            if (block->id >= in_states.size()) {
                continue;
            }
            std::vector<LatticeValue> state = in_states[block->id];

            for (auto& stmt : block->statements) {
                if (!stmt) {
                    continue;
                }

                // ASMステートメント: 出力オペランドをOverdefinedにマーク
                if (stmt->kind == MirStatement::Asm) {
                    const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                    for (const auto& operand : asm_data.operands) {
                        if (!operand.constraint.empty() &&
                            (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                            if (operand.local_id < state.size()) {
                                state[operand.local_id] = {LatticeKind::Overdefined, {}};
                            }
                        }
                    }
                    continue;
                }

                if (stmt->kind != MirStatement::Assign) {
                    continue;
                }
                // no_optフラグがtrueの場合は定数置換をスキップするが、
                // 代入先変数はOverdefinedにする（後続の参照で誤った定数が使われないよう）
                if (stmt->no_opt) {
                    auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                    if (assign_data.place.projections.empty() &&
                        assign_data.place.local < state.size()) {
                        state[assign_data.place.local] = {LatticeKind::Overdefined, {}};
                    }
                    continue;
                }
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                if (assign_data.rvalue) {
                    changed |= rewrite_rvalue(func, *assign_data.rvalue, state);
                }

                if (assign_data.place.local >= state.size()) {
                    continue;
                }

                if (!assign_data.place.projections.empty()) {
                    // プロジェクションがある場合
                    bool has_deref = false;
                    for (const auto& proj : assign_data.place.projections) {
                        if (proj.kind == ProjectionKind::Deref) {
                            has_deref = true;
                            break;
                        }
                    }
                    if (has_deref) {
                        // デリファレンス書き込みは全てのローカル変数に影響する可能性がある
                        for (size_t i = 0; i < state.size(); ++i) {
                            state[i] = {LatticeKind::Overdefined, {}};
                        }
                    } else {
                        state[assign_data.place.local] = {LatticeKind::Overdefined, {}};
                    }
                    continue;
                }

                LatticeValue value = eval_rvalue(func, *assign_data.rvalue, state);
                if (value.kind == LatticeKind::Constant &&
                    !can_bind_constant(func, assign_data.place.local, value.constant)) {
                    value.kind = LatticeKind::Overdefined;
                }

                if (value.kind == LatticeKind::Constant) {
                    changed |= replace_with_constant(assign_data.rvalue, value.constant);
                }

                state[assign_data.place.local] = value;
            }

            // Call終端子のdestinationを処理
            if (block->terminator && block->terminator->kind == MirTerminator::Call) {
                const auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
                if (call_data.destination) {
                    LocalId dest = call_data.destination->local;
                    if (dest < state.size()) {
                        state[dest] = {LatticeKind::Overdefined, {}};
                    }
                }
            }

            if (block->terminator) {
                changed |= rewrite_terminator(func, *block->terminator, state);
            }
        }

        return changed;
    }

    bool rewrite_rvalue(const MirFunction& func, MirRvalue& rvalue,
                        const std::vector<LatticeValue>& state) {
        bool changed = false;

        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& use_data = std::get<MirRvalue::UseData>(rvalue.data);
                if (use_data.operand) {
                    changed |= rewrite_operand(func, *use_data.operand, state);
                }
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                if (bin_data.lhs) {
                    changed |= rewrite_operand(func, *bin_data.lhs, state);
                }
                if (bin_data.rhs) {
                    changed |= rewrite_operand(func, *bin_data.rhs, state);
                }
                break;
            }
            case MirRvalue::UnaryOp: {
                auto& unary_data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                if (unary_data.operand) {
                    changed |= rewrite_operand(func, *unary_data.operand, state);
                }
                break;
            }
            case MirRvalue::Aggregate: {
                auto& agg_data = std::get<MirRvalue::AggregateData>(rvalue.data);
                for (auto& op : agg_data.operands) {
                    if (op) {
                        changed |= rewrite_operand(func, *op, state);
                    }
                }
                break;
            }
            case MirRvalue::FormatConvert: {
                auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                if (fmt_data.operand) {
                    changed |= rewrite_operand(func, *fmt_data.operand, state);
                }
                break;
            }
            case MirRvalue::Cast: {
                auto& cast_data = std::get<MirRvalue::CastData>(rvalue.data);
                if (cast_data.operand) {
                    changed |= rewrite_operand(func, *cast_data.operand, state);
                }
                break;
            }
            case MirRvalue::Ref:
                break;
        }

        return changed;
    }

    bool rewrite_terminator(const MirFunction& func, MirTerminator& term,
                            const std::vector<LatticeValue>& state) {
        bool changed = false;

        switch (term.kind) {
            case MirTerminator::SwitchInt: {
                auto& switch_data = std::get<MirTerminator::SwitchIntData>(term.data);
                if (switch_data.discriminant) {
                    changed |= rewrite_operand(func, *switch_data.discriminant, state);
                }
                auto single_target = simplify_switch(func, switch_data, state);
                if (single_target) {
                    Span span = term.span;
                    term.kind = MirTerminator::Goto;
                    term.span = span;
                    term.data = MirTerminator::GotoData{*single_target};
                    changed = true;
                }
                break;
            }
            case MirTerminator::Call: {
                auto& call_data = std::get<MirTerminator::CallData>(term.data);
                if (call_data.func) {
                    changed |= rewrite_operand(func, *call_data.func, state);
                }
                for (auto& arg : call_data.args) {
                    if (arg) {
                        changed |= rewrite_operand(func, *arg, state);
                    }
                }
                break;
            }
            default:
                break;
        }

        return changed;
    }

    bool rewrite_operand(const MirFunction& func, MirOperand& operand,
                         const std::vector<LatticeValue>& state) {
        if (operand.kind != MirOperand::Copy && operand.kind != MirOperand::Move) {
            return false;
        }

        auto* place = std::get_if<MirPlace>(&operand.data);
        if (!place || !place->projections.empty()) {
            return false;
        }
        if (place->local >= state.size()) {
            return false;
        }
        const LatticeValue& value = state[place->local];
        if (value.kind != LatticeKind::Constant) {
            return false;
        }
        if (!can_bind_constant(func, place->local, value.constant)) {
            return false;
        }

        operand.kind = MirOperand::Constant;
        operand.data = value.constant;
        return true;
    }

    bool replace_with_constant(MirRvaluePtr& rvalue, const MirConstant& constant) {
        if (!rvalue) {
            return false;
        }
        if (rvalue->kind == MirRvalue::Use) {
            if (auto* use_data = std::get_if<MirRvalue::UseData>(&rvalue->data)) {
                if (use_data->operand && use_data->operand->kind == MirOperand::Constant) {
                    if (auto* existing = std::get_if<MirConstant>(&use_data->operand->data)) {
                        if (equal_constant(*existing, constant)) {
                            return false;
                        }
                    }
                }
            }
        }
        rvalue = MirRvalue::use(MirOperand::constant(constant));
        return true;
    }

    std::optional<BlockId> simplify_switch(const MirFunction& func,
                                           const MirTerminator::SwitchIntData& switch_data,
                                           const std::vector<LatticeValue>& state) {
        BlockId target = switch_data.otherwise;
        bool all_same = true;
        for (const auto& [_, case_target] : switch_data.targets) {
            if (case_target != target) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            return target;
        }

        if (!state.empty() && switch_data.discriminant) {
            LatticeValue discr = eval_operand(func, *switch_data.discriminant, state);
            if (discr.kind == LatticeKind::Constant) {
                if (auto* v = std::get_if<int64_t>(&discr.constant.value)) {
                    for (const auto& [case_value, case_target] : switch_data.targets) {
                        if (case_value == *v) {
                            return case_target;
                        }
                    }
                    return switch_data.otherwise;
                }
            }
        }

        return std::nullopt;
    }

    bool simplify_cfg(MirFunction& func) {
        bool changed = false;
        for (auto& block : func.basic_blocks) {
            if (!block || !block->terminator) {
                continue;
            }
            if (block->terminator->kind == MirTerminator::SwitchInt) {
                auto& switch_data = std::get<MirTerminator::SwitchIntData>(block->terminator->data);
                auto target = simplify_switch(func, switch_data, {});
                if (target) {
                    Span span = block->terminator->span;
                    block->terminator = MirTerminator::goto_block(*target, span);
                    changed = true;
                }
            }
        }
        if (changed) {
            rebuild_cfg(func);
        }
        return changed;
    }

    bool remove_unreachable_blocks(MirFunction& func) {
        rebuild_cfg(func);

        std::vector<bool> reachable(func.basic_blocks.size(), false);
        std::queue<BlockId> worklist;

        if (!func.get_block(func.entry_block)) {
            return false;
        }

        reachable[func.entry_block] = true;
        worklist.push(func.entry_block);

        while (!worklist.empty()) {
            BlockId current = worklist.front();
            worklist.pop();

            if (auto* block = func.get_block(current)) {
                for (BlockId succ : block->successors) {
                    if (succ >= reachable.size()) {
                        continue;
                    }
                    if (!reachable[succ]) {
                        reachable[succ] = true;
                        worklist.push(succ);
                    }
                }
            }
        }

        bool changed = false;
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            if (!reachable[i] && func.basic_blocks[i]) {
                func.basic_blocks[i] = nullptr;
                changed = true;
            }
        }

        if (changed) {
            rebuild_cfg(func);
        }
        return changed;
    }
};

}  // namespace cm::mir::opt
