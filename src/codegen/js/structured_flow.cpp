#include "builtins.hpp"
#include "codegen.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <iostream>
#include <set>

namespace cm::codegen::js {

using ast::TypeKind;

// 構造化制御フローの生成
void JSCodeGen::emitStructuredFlow(const mir::MirFunction& func, const mir::MirProgram& program,
                                   const ControlFlowAnalyzer& cfAnalyzer) {
    std::set<mir::BlockId> emittedBlocks;

    // エントリブロックから開始
    mir::BlockId current = func.entry_block;

    while (current != mir::INVALID_BLOCK && emittedBlocks.count(current) == 0) {
        if (current >= func.basic_blocks.size() || !func.basic_blocks[current]) {
            break;
        }

        const auto& block = *func.basic_blocks[current];

        // ループヘッダーの場合は while ループを生成
        if (cfAnalyzer.isLoopHeader(current)) {
            emitLoopBlock(current, func, program, cfAnalyzer, emittedBlocks);

            // ループ後の続きを探す
            // ループヘッダーのターミネーターから出口を見つける
            if (block.terminator && block.terminator->kind == mir::MirTerminator::SwitchInt) {
                const auto& data =
                    std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
                // SwitchIntのotherwiseがループ外への出口
                if (!cfAnalyzer.isLoopHeader(data.otherwise)) {
                    current = data.otherwise;
                    continue;
                }
                // targetsの中でループ外のものを探す
                for (const auto& [val, target] : data.targets) {
                    if (!cfAnalyzer.isLoopHeader(target) && emittedBlocks.count(target) == 0) {
                        current = target;
                        break;
                    }
                }
            }
            break;  // ループ後はフォールアウト
        }

        // 通常のブロック
        emittedBlocks.insert(current);
        emitBlockStatements(block, func);

        // ターミネーターに従って次のブロックを決定
        if (!block.terminator)
            break;

        switch (block.terminator->kind) {
            case mir::MirTerminator::Goto: {
                const auto& data = std::get<mir::MirTerminator::GotoData>(block.terminator->data);
                current = data.target;
                break;
            }
            case mir::MirTerminator::Call: {
                const auto& data = std::get<mir::MirTerminator::CallData>(block.terminator->data);
                emitTerminator(*block.terminator, func, program);
                current = data.success;
                break;
            }
            case mir::MirTerminator::Return: {
                emitLinearTerminator(*block.terminator, func, program);
                current = mir::INVALID_BLOCK;
                break;
            }
            case mir::MirTerminator::SwitchInt: {
                // if/else構造として処理
                emitIfElseBlock(block, func, program, cfAnalyzer, emittedBlocks);
                current = mir::INVALID_BLOCK;  // 別の経路で処理
                break;
            }
            default:
                current = mir::INVALID_BLOCK;
                break;
        }
    }
}

// ループブロックの生成（while (true) { ... if (!cond) break; ... } パターン）
void JSCodeGen::emitLoopBlock(mir::BlockId headerBlock, const mir::MirFunction& func,
                              const mir::MirProgram& program, const ControlFlowAnalyzer& cfAnalyzer,
                              std::set<mir::BlockId>& emittedBlocks) {
    emitter_.emitLine("while (true) {");
    emitter_.increaseIndent();

    // ループ内のブロックを処理
    std::set<mir::BlockId> loopBlocks;
    std::vector<mir::BlockId> workList = {headerBlock};

    // ループに含まれるブロックを収集
    while (!workList.empty()) {
        mir::BlockId bid = workList.back();
        workList.pop_back();

        if (loopBlocks.count(bid) > 0)
            continue;
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;

        loopBlocks.insert(bid);

        const auto& block = *func.basic_blocks[bid];
        if (!block.terminator)
            continue;

        // 後続ブロックをワークリストに追加（ループ外は除く）
        for (mir::BlockId succ : block.successors) {
            if (loopBlocks.count(succ) == 0) {
                // バックエッジ先（ヘッダー）または他のループ内ブロック
                if (succ == headerBlock || cfAnalyzer.getDominators(succ).count(headerBlock) > 0) {
                    workList.push_back(succ);
                }
            }
        }
    }

    // ループ内ブロックを順番に出力
    mir::BlockId current = headerBlock;
    std::set<mir::BlockId> visitedInLoop;

    while (current != mir::INVALID_BLOCK && visitedInLoop.count(current) == 0) {
        if (current >= func.basic_blocks.size() || !func.basic_blocks[current])
            break;

        visitedInLoop.insert(current);
        emittedBlocks.insert(current);
        const auto& block = *func.basic_blocks[current];

        // ステートメントを出力
        emitBlockStatements(block, func);

        if (!block.terminator)
            break;

        switch (block.terminator->kind) {
            case mir::MirTerminator::SwitchInt: {
                const auto& data =
                    std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
                std::string discrim = emitOperand(*data.discriminant, func);

                // 条件分岐: ループを抜けるか続けるか
                // targets[0]がループ継続、otherwiseがループ脱出の場合
                if (current == headerBlock && loopBlocks.count(data.otherwise) == 0) {
                    // if (!cond) break; パターン
                    emitter_.emitLine("if (" + discrim + " === 0) break;");
                    // else側（ループ継続）へ進む
                    if (!data.targets.empty()) {
                        current = data.targets[0].second;
                    } else {
                        current = mir::INVALID_BLOCK;
                    }
                } else if (current == headerBlock && !data.targets.empty() &&
                           loopBlocks.count(data.targets[0].second) == 0) {
                    // 逆パターン: 条件が真でループ脱出
                    emitter_.emitLine("if (" + discrim + " !== 0) break;");
                    current = data.otherwise;
                } else {
                    // 通常のif/else
                    for (const auto& [value, target] : data.targets) {
                        emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) +
                                          ") {");
                        emitter_.increaseIndent();
                        if (loopBlocks.count(target) == 0) {
                            emitter_.emitLine("break;");
                        }
                        emitter_.decreaseIndent();
                        emitter_.emitLine("}");
                    }
                    current = data.otherwise;
                }
                break;
            }
            case mir::MirTerminator::Goto: {
                const auto& data = std::get<mir::MirTerminator::GotoData>(block.terminator->data);
                if (data.target == headerBlock) {
                    // バックエッジ: continue (明示的にcontinueは不要、whileの終わりで自動的に戻る)
                    current = mir::INVALID_BLOCK;  // ループの終わり
                } else if (loopBlocks.count(data.target) > 0) {
                    current = data.target;
                } else {
                    // ループ外へ脱出
                    emitter_.emitLine("break;");
                    current = mir::INVALID_BLOCK;
                }
                break;
            }
            case mir::MirTerminator::Call: {
                const auto& data = std::get<mir::MirTerminator::CallData>(block.terminator->data);
                // 関数呼び出しを出力
                emitTerminator(*block.terminator, func, program);
                if (data.success == headerBlock) {
                    current = mir::INVALID_BLOCK;
                } else {
                    current = data.success;
                }
                break;
            }
            case mir::MirTerminator::Return: {
                emitLinearTerminator(*block.terminator, func, program);
                current = mir::INVALID_BLOCK;
                break;
            }
            default:
                current = mir::INVALID_BLOCK;
                break;
        }
    }

    emitter_.decreaseIndent();
    emitter_.emitLine("}");  // while
}

// if/else構造の生成
void JSCodeGen::emitIfElseBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                                const mir::MirProgram& program,
                                [[maybe_unused]] const ControlFlowAnalyzer& cfAnalyzer,
                                std::set<mir::BlockId>& emittedBlocks) {
    if (!block.terminator || block.terminator->kind != mir::MirTerminator::SwitchInt) {
        return;
    }

    const auto& data = std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
    std::string discrim = emitOperand(*data.discriminant, func);

    // 単純な if/else パターン
    for (const auto& [value, target] : data.targets) {
        emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) + ") {");
        emitter_.increaseIndent();

        if (target < func.basic_blocks.size() && func.basic_blocks[target] &&
            emittedBlocks.count(target) == 0) {
            emittedBlocks.insert(target);
            emitLinearBlock(*func.basic_blocks[target], func, program);
        }

        emitter_.decreaseIndent();
        emitter_.emitLine("} else {");
        emitter_.increaseIndent();
    }

    // otherwise
    if (data.otherwise < func.basic_blocks.size() && func.basic_blocks[data.otherwise] &&
        emittedBlocks.count(data.otherwise) == 0) {
        emittedBlocks.insert(data.otherwise);
        emitLinearBlock(*func.basic_blocks[data.otherwise], func, program);
    }

    // 閉じ括弧
    for (size_t i = 0; i < data.targets.size(); ++i) {
        emitter_.decreaseIndent();
        emitter_.emitLine("}");
    }
}

// ブロック内のステートメントのみ出力（ターミネーターなし）
void JSCodeGen::emitBlockStatements(const mir::BasicBlock& block, const mir::MirFunction& func) {
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }
}

}  // namespace cm::codegen::js
