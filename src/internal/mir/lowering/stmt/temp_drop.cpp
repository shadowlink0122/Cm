// MIR lowering - 文単位一時オブジェクトのdropパス（C12）
// let/assign/式文のlowering中に確保された無名文字列一時（cm_string_concat・cm_*_to_stringの結果）を、
// 文のMIR範囲をスキャンしてエスケープ解析し、所有権が移動しなかったものへcm_string_free呼び出しを発行する

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/stmt.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

namespace {

// 呼び出し先が引数ポインタを保持しない（読み取りのみ・新規バッファを返す）ことが既知のランタイム関数。
// これらへ渡っただけの一時は解放してよい。それ以外の呼び出し先へ渡った一時は所有権移動とみなす
bool is_non_retaining_callee(const std::string& name) {
    if (name.rfind("cm_println_", 0) == 0 || name.rfind("cm_print_", 0) == 0 ||
        name.rfind("cm_format_", 0) == 0 || name.rfind("__builtin_string_", 0) == 0) {
        return true;
    }
    // スライスの読み取り系と高階関数（map/filter/len等）は引数スライスを保持しない。
    // 格納系（cm_slice_push_*等）はホワイトリストに含めない
    if (name.rfind("cm_slice_get", 0) == 0 || name.rfind("__builtin_array_", 0) == 0 ||
        name == "cm_slice_len" || name == "cm_slice_cap" || name == "cm_slice_first_i32" ||
        name == "cm_slice_first_i64" || name == "cm_slice_last_i32" ||
        name == "cm_slice_last_i64" || name == "cm_slice_free") {
        return true;
    }
    return name == "cm_string_concat" || name == "cm_strlen" || name == "cm_strcmp" ||
           name == "strlen" || name == "strcmp" || name == "cm_string_free";
}

// Placが一時を参照しているか（ベースまたは添字投影）
bool place_uses(const MirPlace& place, const std::unordered_set<LocalId>& temps) {
    if (temps.count(place.local) > 0) {
        return true;
    }
    for (const auto& proj : place.projections) {
        if (proj.kind == ProjectionKind::Index && temps.count(proj.index_local) > 0) {
            return true;
        }
    }
    return false;
}

// オペランドが一時を参照しているか
bool operand_uses(const MirOperandPtr& op, const std::unordered_set<LocalId>& temps) {
    if (!op) {
        return false;
    }
    if (op->kind == MirOperand::Copy || op->kind == MirOperand::Move) {
        return place_uses(std::get<MirPlace>(op->data), temps);
    }
    return false;
}

// Rvalue中の一時の使用が所有権エスケープに当たるか判定し、該当した一時をescapedへ追加する。
// BinaryOp/UnaryOp/FormatConvertはスカラ結果または新規バッファを返す読み取りのみでエスケープしない。
// Use（ポインタのコピー）・Aggregate（コンテナへの格納）・Cast・Refは所有権が移るためエスケープ
void collect_rvalue_escapes(const MirRvaluePtr& rvalue, const std::unordered_set<LocalId>& temps,
                            std::unordered_set<LocalId>& escaped) {
    if (!rvalue) {
        return;
    }
    auto mark = [&](const MirOperandPtr& op) {
        if (op && (op->kind == MirOperand::Copy || op->kind == MirOperand::Move)) {
            const auto& place = std::get<MirPlace>(op->data);
            if (temps.count(place.local) > 0) {
                escaped.insert(place.local);
            }
        }
    };
    switch (rvalue->kind) {
        case MirRvalue::Use:
            mark(std::get<MirRvalue::UseData>(rvalue->data).operand);
            break;
        case MirRvalue::BinaryOp:
        case MirRvalue::UnaryOp:
        case MirRvalue::FormatConvert:
            // 読み取りのみ（文字列比較等）。ポインタは保持されない
            break;
        case MirRvalue::Ref:
            if (temps.count(std::get<MirRvalue::RefData>(rvalue->data).place.local) > 0) {
                escaped.insert(std::get<MirRvalue::RefData>(rvalue->data).place.local);
            }
            break;
        case MirRvalue::Aggregate:
            for (const auto& op : std::get<MirRvalue::AggregateData>(rvalue->data).operands) {
                mark(op);
            }
            break;
        case MirRvalue::Cast:
            mark(std::get<MirRvalue::CastData>(rvalue->data).operand);
            break;
    }
}

// 指定範囲（開始ブロックの途中以降＋開始時点以降に作られた全ブロック）をエスケープ解析し、
// エスケープしなかった一時を現在位置で解放する（文スコープ・腕スコープ共通の実装）
void scan_and_free_temps(LoweringContext& ctx, BlockId start_block, size_t start_stmt_index,
                         size_t start_block_count, const std::vector<LocalId>& string_temps,
                         const std::vector<LocalId>& slice_temps) {
    if (string_temps.empty() && slice_temps.empty()) {
        return;
    }

    std::unordered_set<LocalId> temps(string_temps.begin(), string_temps.end());
    temps.insert(slice_temps.begin(), slice_temps.end());
    std::unordered_set<LocalId> escaped;

    auto scan_block = [&](const BasicBlock* block, size_t from_index) {
        if (!block) {
            return;
        }
        for (size_t i = from_index; i < block->statements.size(); ++i) {
            const auto& stmt = block->statements[i];
            if (!stmt || stmt->kind != MirStatement::Assign) {
                continue;
            }
            const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
            collect_rvalue_escapes(assign.rvalue, temps, escaped);
            // 一時自身への書き込み（投影付き）や添字使用も保守的にエスケープ扱いにする
            if (!assign.place.projections.empty() && place_uses(assign.place, temps)) {
                escaped.insert(assign.place.local);
            }
        }
        if (block->terminator) {
            const auto& term = *block->terminator;
            if (term.kind == MirTerminator::Call) {
                const auto& call = std::get<MirTerminator::CallData>(term.data);
                std::string callee;
                if (call.func && call.func->kind == MirOperand::FunctionRef) {
                    callee = std::get<std::string>(call.func->data);
                }
                bool retains = !is_non_retaining_callee(callee);
                for (const auto& arg : call.args) {
                    if (retains && operand_uses(arg, temps)) {
                        const auto& place = std::get<MirPlace>(arg->data);
                        escaped.insert(place.local);
                    }
                }
            } else if (term.kind == MirTerminator::Return) {
                // 戻り値ローカル経由の使用は事前の代入で検出済みだが、保守的に全一時を保持する
                // （トラッキング範囲でReturnが現れるのは?演算子の早期return経路のみ）
            }
        }
    };

    if (start_block < ctx.func->basic_blocks.size()) {
        scan_block(ctx.func->basic_blocks[start_block].get(), start_stmt_index);
    }
    for (size_t b = start_block_count; b < ctx.func->basic_blocks.size(); ++b) {
        scan_block(ctx.func->basic_blocks[b].get(), 0);
    }

    // エスケープしなかった一時を解放する（生成順に解放して問題ない）
    auto emit_free = [&](LocalId temp, const char* free_func, hir::TypePtr operand_type) {
        BlockId next = ctx.new_block();
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{temp}, std::move(operand_type)));
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{MirOperand::function_ref(free_func),
                                                  std::move(args),
                                                  std::nullopt,
                                                  next,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(next);
    };
    for (LocalId temp : string_temps) {
        if (escaped.count(temp) == 0) {
            emit_free(temp, "cm_string_free", hir::make_string());
        }
    }
    // スライス一時（map/filter結果）はヘッダ+データを所有するためcm_slice_freeで解放する
    for (LocalId temp : slice_temps) {
        if (escaped.count(temp) == 0) {
            hir::TypePtr slice_type = nullptr;
            if (temp < ctx.func->locals.size()) {
                slice_type = ctx.func->locals[temp].type;
            }
            emit_free(temp, "cm_slice_free", slice_type);
        }
    }
}

}  // namespace

// 条件腕の一時スコープを開始する（文スコープ非アクティブ時は何もしない）
bool begin_arm_temp_scope(LoweringContext& ctx) {
    if (!ctx.stmt_temp_scope.active) {
        return false;
    }
    LoweringContext::ArmTempScope scope;
    scope.start_block = ctx.current_block;
    auto* block = ctx.get_current_block();
    scope.start_stmt_index = block ? block->statements.size() : 0;
    scope.start_block_count = ctx.func->basic_blocks.size();
    ctx.arm_temp_scopes.push_back(std::move(scope));
    return true;
}

// 条件腕の一時スコープを終了し、腕内で完結した一時を腕ブロック内で解放する。
// 腕の結果値（result = copy(値)）はUseエスケープとして自然に保護される
void end_arm_temp_scope(LoweringContext& ctx, bool pushed) {
    if (!pushed || ctx.arm_temp_scopes.empty()) {
        return;
    }
    auto scope = std::move(ctx.arm_temp_scopes.back());
    ctx.arm_temp_scopes.pop_back();
    scan_and_free_temps(ctx, scope.start_block, scope.start_stmt_index, scope.start_block_count,
                        scope.string_temps, scope.slice_temps);
}

// 所有権判定付きの腕スコープ終了（三項演算子用）。
// arm_valueがこの腕で登録されたfresh一時で、腕内での使用が読み取り・非保持呼び出しと
// result_localへのUseコピー1回だけの場合、所有権が結果へ移動したとみなして種別を返す。
// 両腕がともに所有を返した場合のみ、呼び出し側は結果ローカルを外側スコープの所有一時として登録できる
ArmValueOwnership end_arm_temp_scope(LoweringContext& ctx, bool pushed, LocalId arm_value,
                                     LocalId result_local) {
    if (!pushed || ctx.arm_temp_scopes.empty()) {
        return ArmValueOwnership::None;
    }
    auto scope = std::move(ctx.arm_temp_scopes.back());
    ctx.arm_temp_scopes.pop_back();

    // arm_valueの登録種別
    ArmValueOwnership kind = ArmValueOwnership::None;
    for (LocalId t : scope.string_temps) {
        if (t == arm_value) {
            kind = ArmValueOwnership::String;
            break;
        }
    }
    if (kind == ArmValueOwnership::None) {
        for (LocalId t : scope.slice_temps) {
            if (t == arm_value) {
                kind = ArmValueOwnership::Slice;
                break;
            }
        }
    }

    // 腕範囲を走査し、arm_valueの使用が「result_localへのUseコピー1回+読み取り/非保持呼び出しのみ」かを確認する
    bool sole_transfer = true;
    int result_copies = 0;
    if (kind != ArmValueOwnership::None) {
        auto uses_arm_value = [&](const MirOperandPtr& op) {
            if (!op || (op->kind != MirOperand::Copy && op->kind != MirOperand::Move)) {
                return false;
            }
            const auto& place = std::get<MirPlace>(op->data);
            return place.local == arm_value;
        };
        auto scan_block = [&](const BasicBlock* block, size_t from_index) {
            if (!block) {
                return;
            }
            for (size_t i = from_index; i < block->statements.size(); ++i) {
                const auto& stmt = block->statements[i];
                if (!stmt || stmt->kind != MirStatement::Assign) {
                    continue;
                }
                const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                if (!assign.rvalue) {
                    continue;
                }
                switch (assign.rvalue->kind) {
                    case MirRvalue::Use: {
                        const auto& use = std::get<MirRvalue::UseData>(assign.rvalue->data);
                        if (uses_arm_value(use.operand)) {
                            if (assign.place.projections.empty() &&
                                assign.place.local == result_local) {
                                result_copies++;
                            } else {
                                sole_transfer = false;
                            }
                        }
                        break;
                    }
                    case MirRvalue::BinaryOp: {
                        // 読み取りのみ（比較等）。所有権は移らない
                        break;
                    }
                    case MirRvalue::UnaryOp:
                    case MirRvalue::FormatConvert:
                        break;
                    case MirRvalue::Ref:
                        if (std::get<MirRvalue::RefData>(assign.rvalue->data).place.local ==
                            arm_value) {
                            sole_transfer = false;
                        }
                        break;
                    case MirRvalue::Aggregate:
                        for (const auto& op :
                             std::get<MirRvalue::AggregateData>(assign.rvalue->data).operands) {
                            if (uses_arm_value(op)) {
                                sole_transfer = false;
                            }
                        }
                        break;
                    case MirRvalue::Cast:
                        if (uses_arm_value(
                                std::get<MirRvalue::CastData>(assign.rvalue->data).operand)) {
                            sole_transfer = false;
                        }
                        break;
                }
            }
            if (block->terminator && block->terminator->kind == MirTerminator::Call) {
                const auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
                std::string callee;
                if (call.func && call.func->kind == MirOperand::FunctionRef) {
                    callee = std::get<std::string>(call.func->data);
                }
                bool retains = callee.empty() || !is_non_retaining_callee(callee);
                for (const auto& arg : call.args) {
                    if (retains && uses_arm_value(arg)) {
                        sole_transfer = false;
                    }
                }
            } else if (block->terminator && block->terminator->kind == MirTerminator::Return) {
                // return経由の使用は保守的に所有権移動なしとみなす
                sole_transfer = false;
            }
        };
        if (scope.start_block < ctx.func->basic_blocks.size()) {
            scan_block(ctx.func->basic_blocks[scope.start_block].get(), scope.start_stmt_index);
        }
        for (size_t b = scope.start_block_count; b < ctx.func->basic_blocks.size(); ++b) {
            scan_block(ctx.func->basic_blocks[b].get(), 0);
        }
    }

    // 腕内で完結した一時の解放は従来どおり行う（arm_valueはresultへのエスケープで保護される）
    scan_and_free_temps(ctx, scope.start_block, scope.start_stmt_index, scope.start_block_count,
                        scope.string_temps, scope.slice_temps);

    if (kind != ArmValueOwnership::None && sole_transfer && result_copies == 1) {
        return kind;
    }
    return ArmValueOwnership::None;
}

// 文の一時トラッキングを開始する（既にアクティブなら何もしない＝最外の単純文が勝つ）
void StmtLowering::begin_stmt_temp_scope(LoweringContext& ctx) {
    if (ctx.stmt_temp_scope.active) {
        return;
    }
    ctx.stmt_temp_scope.active = true;
    ctx.stmt_temp_scope.start_block = ctx.current_block;
    auto* block = ctx.get_current_block();
    ctx.stmt_temp_scope.start_stmt_index = block ? block->statements.size() : 0;
    ctx.stmt_temp_scope.start_block_count = ctx.func->basic_blocks.size();
    ctx.stmt_temp_scope.string_temps.clear();
    ctx.stmt_temp_scope.slice_temps.clear();
}

// 文の一時トラッキングを終了し、エスケープしなかった文字列一時へcm_string_free呼び出しを発行する
void StmtLowering::end_stmt_temp_scope(LoweringContext& ctx, bool was_active) {
    if (was_active || !ctx.stmt_temp_scope.active) {
        // ネストした呼び出し（外側の文がトラッキング中）では何もしない
        return;
    }
    auto scope = std::move(ctx.stmt_temp_scope);
    ctx.stmt_temp_scope = LoweringContext::StmtTempScope{};

    // 文のlowering中に生成されたMIR範囲（開始ブロックの途中以降＋新規ブロック）をスキャンして解放する
    scan_and_free_temps(ctx, scope.start_block, scope.start_stmt_index, scope.start_block_count,
                        scope.string_temps, scope.slice_temps);
}

}  // namespace cm::mir
