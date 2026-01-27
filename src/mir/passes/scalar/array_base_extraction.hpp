#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <map>
#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 配列ベースオフセット抽出最適化
//
// 多次元配列アクセス a[i][j] を以下のように変換:
//   Before: 毎アクセスで i*stride を計算
//   After:  base = i*stride を一時変数として抽出し再利用
//
// これにより内側ループでLLVM LICMが base を外に移動可能
// ============================================================
class ArrayBaseExtraction : public OptimizationPass {
   public:
    std::string name() const override { return "ArrayBaseExtraction"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 各ブロックを処理
        for (auto& block : func.basic_blocks) {
            if (block) {
                changed |= process_block(func, *block);
            }
        }

        return changed;
    }

   private:
    // ブロック内の多次元配列アクセスを処理
    bool process_block(MirFunction& func, BasicBlock& block) {
        bool changed = false;

        // 配列ベースオフセットのキャッシュ
        // キー: (配列Local, 外側インデックスLocal) -> ベースオフセットを保持するLocal
        std::map<std::pair<LocalId, LocalId>, LocalId> baseOffsetCache;

        // 挿入する文を収集（索引順序を維持するため後で挿入）
        std::vector<std::pair<size_t, MirStatementPtr>> insertions;

        // 各文を処理
        for (size_t i = 0; i < block.statements.size(); ++i) {
            auto& stmt = block.statements[i];
            if (stmt->kind != MirStatement::Assign)
                continue;

            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

            // 左辺のPlaceをチェック
            if (has_multi_dim_index(assign_data.place)) {
                auto newStmts = transform_place(func, assign_data.place, baseOffsetCache);
                for (auto& s : newStmts) {
                    insertions.push_back({i, std::move(s)});
                }
                if (!newStmts.empty())
                    changed = true;
            }

            // 右辺のRvalue内のPlaceをチェック
            if (assign_data.rvalue) {
                auto newStmts = transform_rvalue(func, *assign_data.rvalue, baseOffsetCache);
                for (auto& s : newStmts) {
                    insertions.push_back({i, std::move(s)});
                }
                if (!newStmts.empty())
                    changed = true;
            }
        }

        // 挿入を適用（後ろから挿入してインデックスを維持）
        for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
            block.statements.insert(block.statements.begin() + it->first, std::move(it->second));
        }

        return changed;
    }

    // 多次元インデックス（2つ以上のIndexプロジェクション）を持つか
    bool has_multi_dim_index(const MirPlace& place) {
        int indexCount = 0;
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Index) {
                indexCount++;
            }
        }
        return indexCount >= 2;
    }

    // Placeを線形化インデックスに変換
    std::vector<MirStatementPtr> transform_place(
        MirFunction& func, MirPlace& place,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache) {
        std::vector<MirStatementPtr> newStmts;

        if (!has_multi_dim_index(place))
            return newStmts;

        // 配列の次元情報を取得
        std::vector<PlaceProjection> indexProjections;

        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Index) {
                indexProjections.push_back(proj);
            }
        }

        if (indexProjections.size() < 2)
            return newStmts;

        // 最初のインデックス（外側ループ変数）のlocal
        LocalId arrayLocal = place.local;
        LocalId outerIndexLocal = indexProjections[0].index_local;

        // キャッシュをチェック
        auto cacheKey = std::make_pair(arrayLocal, outerIndexLocal);
        auto it = baseOffsetCache.find(cacheKey);

        LocalId baseOffsetLocal;
        if (it != baseOffsetCache.end()) {
            // キャッシュヒット
            baseOffsetLocal = it->second;
        } else {
            // キャッシュミス: ベースオフセット計算を生成
            // 配列の型から次元サイズを取得
            // place.typeではなくfunc.localsから型を取得（place.typeはnullの場合がある）
            hir::TypePtr arrayType = nullptr;
            if (arrayLocal < func.locals.size()) {
                arrayType = func.locals[arrayLocal].type;
            }
            uint64_t stride = get_array_stride(arrayType, indexProjections.size());
            if (stride == 0)
                return newStmts;

            // 新しいlocal変数を作成: base = (long)outer_index * stride
            baseOffsetLocal =
                func.add_local("_base_" + std::to_string(func.locals.size()), hir::make_long());

            // outer_indexをlongにキャスト
            LocalId outerIndexCast =
                func.add_local("_idx_cast_" + std::to_string(func.locals.size()), hir::make_long());
            auto castStmt = std::make_unique<MirStatement>();
            castStmt->kind = MirStatement::Assign;
            auto castRvalue =
                MirRvalue::cast(MirOperand::copy(MirPlace(outerIndexLocal)), hir::make_long());
            castStmt->data =
                MirStatement::AssignData{MirPlace(outerIndexCast), std::move(castRvalue)};
            newStmts.push_back(std::move(castStmt));

            // base = casted_index * stride の文を生成
            auto mulStmt = std::make_unique<MirStatement>();
            mulStmt->kind = MirStatement::Assign;

            // stride定数
            MirConstant strideConst;
            strideConst.value = static_cast<int64_t>(stride);
            strideConst.type = hir::make_long();

            // casted_index * stride (両方i64)
            auto mulRvalue = MirRvalue::binary(
                MirBinaryOp::Mul, MirOperand::copy(MirPlace(outerIndexCast), hir::make_long()),
                MirOperand::constant(strideConst), hir::make_long());

            mulStmt->data =
                MirStatement::AssignData{MirPlace(baseOffsetLocal), std::move(mulRvalue)};
            newStmts.push_back(std::move(mulStmt));

            // キャッシュに登録
            baseOffsetCache[cacheKey] = baseOffsetLocal;
        }

        // Placeを変換: a[i][j] -> a[base + j]
        // 最後のインデックス（内側ループ変数）を取得
        LocalId innerIndexLocal = indexProjections.back().index_local;

        // inner_indexをlongにキャスト
        LocalId innerIndexCast =
            func.add_local("_inner_cast_" + std::to_string(func.locals.size()), hir::make_long());
        auto innerCastStmt = std::make_unique<MirStatement>();
        innerCastStmt->kind = MirStatement::Assign;
        auto innerCastRvalue =
            MirRvalue::cast(MirOperand::copy(MirPlace(innerIndexLocal)), hir::make_long());
        innerCastStmt->data =
            MirStatement::AssignData{MirPlace(innerIndexCast), std::move(innerCastRvalue)};
        newStmts.push_back(std::move(innerCastStmt));

        // linear_index = base + casted_inner_index
        LocalId linearIndexLocal =
            func.add_local("_linear_" + std::to_string(func.locals.size()), hir::make_long());

        // linear = base + casted_inner (両方i64)
        auto addStmt = std::make_unique<MirStatement>();
        addStmt->kind = MirStatement::Assign;
        auto addRvalue = MirRvalue::binary(
            MirBinaryOp::Add, MirOperand::copy(MirPlace(baseOffsetLocal), hir::make_long()),
            MirOperand::copy(MirPlace(innerIndexCast), hir::make_long()), hir::make_long());
        addStmt->data = MirStatement::AssignData{MirPlace(linearIndexLocal), std::move(addRvalue)};
        newStmts.push_back(std::move(addStmt));

        // 元のPlaceを単一インデックスに変換
        place.projections.clear();
        place.projections.push_back(PlaceProjection::index(linearIndexLocal));

        return newStmts;
    }

    // Rvalue内のPlaceを変換
    std::vector<MirStatementPtr> transform_rvalue(
        MirFunction& func, MirRvalue& rvalue,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache) {
        std::vector<MirStatementPtr> newStmts;

        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& use_data = std::get<MirRvalue::UseData>(rvalue.data);
                if (use_data.operand) {
                    auto stmts = transform_operand(func, *use_data.operand, baseOffsetCache);
                    newStmts.insert(newStmts.end(), std::make_move_iterator(stmts.begin()),
                                    std::make_move_iterator(stmts.end()));
                }
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                if (bin_data.lhs) {
                    auto stmts = transform_operand(func, *bin_data.lhs, baseOffsetCache);
                    newStmts.insert(newStmts.end(), std::make_move_iterator(stmts.begin()),
                                    std::make_move_iterator(stmts.end()));
                }
                if (bin_data.rhs) {
                    auto stmts = transform_operand(func, *bin_data.rhs, baseOffsetCache);
                    newStmts.insert(newStmts.end(), std::make_move_iterator(stmts.begin()),
                                    std::make_move_iterator(stmts.end()));
                }
                break;
            }
            default:
                break;
        }

        return newStmts;
    }

    // Operand内のPlaceを変換
    std::vector<MirStatementPtr> transform_operand(
        MirFunction& func, MirOperand& operand,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache) {
        std::vector<MirStatementPtr> newStmts;

        if (operand.kind != MirOperand::Copy && operand.kind != MirOperand::Move)
            return newStmts;

        auto* place = std::get_if<MirPlace>(&operand.data);
        if (!place)
            return newStmts;

        return transform_place(func, *place, baseOffsetCache);
    }

    // 配列のストライド（内側次元のサイズ）を取得
    uint64_t get_array_stride(const hir::TypePtr& type, size_t numDimensions) {
        if (!type)
            return 0;
        if (type->kind != hir::TypeKind::Array)
            return 0;

        // 多次元配列: 最内側以外の次元のサイズを乗算
        uint64_t stride = 1;
        hir::TypePtr current = type;
        size_t dimCount = 0;

        while (current && current->kind == hir::TypeKind::Array) {
            dimCount++;
            if (dimCount < numDimensions && current->array_size.has_value()) {
                stride *= current->array_size.value();
            }
            current = current->element_type;
        }

        return stride;
    }
};

}  // namespace cm::mir::opt
