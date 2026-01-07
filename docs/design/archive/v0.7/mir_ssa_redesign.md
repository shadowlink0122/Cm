[English](mir_ssa_redesign.en.html)

# MIR SSA/Phi Node 再設計書

## 1. 背景と問題点

### 現在の問題
現在のMIR実装では、以下の問題が発生しています：

1. **論理演算の不具合**
   - AND/OR演算で短絡評価を実装した際、異なるブロックから合流する値が正しく処理されない
   - NOT演算の結果が反転する
   - 複数のブロックで同一変数への代入がある場合の値の伝播が不正確

2. **ロードマップでの言及**
   - "MIRローダリングは簡略化版（LoweringContextに問題があるため）"
   - "完全なMIRローダリング実装" が必要

3. **将来の拡張への影響**
   - ジェネリクス（v0.6.0）のモノモーフィゼーション
   - パターンマッチング（v0.10.0）の複雑な制御フロー
   - Result/Option型（v0.7.0）のエラー伝播

## 2. 設計方針

### 2.1 SSA形式の厳密な実装
- 各変数への代入は新しいバージョン（SSA値）を生成
- 制御フローが合流する点では必ずPhi命令を生成
- 変数のライフタイムとスコープを正確に追跡

### 2.2 拡張性を考慮した設計
- ジェネリクスのモノモーフィゼーションに対応
- 複雑な制御フロー（match式、?演算子）に対応
- LLVMへの効率的な変換

## 3. 新しいMIR構造

### 3.1 Phi命令の追加

```cpp
// src/mir/include/statements.hpp
enum class StatementKind {
    Assign,
    StorageLive,
    StorageDead,
    Phi,  // 新規追加
};

struct PhiNode {
    LocalId result;
    std::vector<std::pair<LocalId, BlockId>> predecessors;
    // predecessors: [(value, from_block), ...]
};

struct MirStatement {
    // ... 既存のコード ...

    static MirStatementPtr phi(LocalId result,
                               std::vector<std::pair<LocalId, BlockId>> preds) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = StatementKind::Phi;
        stmt->data = PhiNode{result, std::move(preds)};
        return stmt;
    }
};
```

### 3.2 改良されたLoweringContext

```cpp
// src/mir/lowering/lowering_context.hpp
class LoweringContext {
    // ... 既存のフィールド ...

    // SSA値の管理
    struct SSAValue {
        LocalId id;
        BlockId defined_in;
        hir::TypePtr type;
    };

    // 変数名からSSA値へのマッピング（ブロックごと）
    std::unordered_map<BlockId, std::unordered_map<std::string, SSAValue>> ssa_values;

    // Phi命令が必要な場所を記録
    struct PhiLocation {
        BlockId block;
        std::string var_name;
        std::vector<BlockId> predecessors;
    };
    std::vector<PhiLocation> pending_phis;

public:
    // SSA値の作成
    LocalId create_ssa_value(const std::string& name, hir::TypePtr type);

    // Phi命令の生成
    void insert_phi_nodes();

    // ブロック間の値の伝播
    LocalId get_value_in_block(const std::string& name, BlockId block);
};
```

### 3.3 論理演算の正しい実装

```cpp
// src/mir/lowering/expr_lowering_impl.cpp
LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    if (bin.op == hir::HirBinaryOp::And || bin.op == hir::HirBinaryOp::Or) {
        // 左辺を評価
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // ブロック作成
        BlockId eval_rhs_bb = ctx.new_block();
        BlockId skip_rhs_bb = ctx.new_block();
        BlockId merge_bb = ctx.new_block();

        // 短絡評価の分岐
        if (bin.op == hir::HirBinaryOp::And) {
            // AND: lhs が false なら skip_rhs_bb へ
            ctx.set_terminator(MirTerminator::switch_int(
                MirOperand::copy(MirPlace{lhs}),
                {{1, eval_rhs_bb}},  // true -> 右辺を評価
                skip_rhs_bb          // false -> スキップ
            ));

            // 右辺を評価
            ctx.switch_to_block(eval_rhs_bb);
            LocalId rhs = lower_expression(*bin.rhs, ctx);
            LocalId rhs_result = ctx.create_ssa_value("_and_rhs", hir::make_bool());
            ctx.push_statement(MirStatement::assign(
                MirPlace{rhs_result},
                MirRvalue::use(MirOperand::copy(MirPlace{rhs}))
            ));
            ctx.set_terminator(MirTerminator::goto_block(merge_bb));

            // 短絡時: false を結果とする
            ctx.switch_to_block(skip_rhs_bb);
            LocalId false_result = ctx.create_ssa_value("_and_false", hir::make_bool());
            ctx.push_statement(MirStatement::assign(
                MirPlace{false_result},
                MirRvalue::use(MirOperand::constant(MirConstant{hir::make_bool(), int64_t(0)}))
            ));
            ctx.set_terminator(MirTerminator::goto_block(merge_bb));

            // Phi命令で結果を統合
            ctx.switch_to_block(merge_bb);
            LocalId result = ctx.create_ssa_value("_and_result", hir::make_bool());
            ctx.push_statement(MirStatement::phi(
                result,
                {{rhs_result, eval_rhs_bb}, {false_result, skip_rhs_bb}}
            ));

            return result;
        }
        // OR も同様に実装...
    }
    // 通常の二項演算...
}
```

## 4. LLVM変換の改良

### 4.1 Phi命令のLLVM変換

```cpp
// src/codegen/llvm/mir_to_llvm.cpp
void MIRToLLVM::convertStatement(const mir::MirStatement& stmt) {
    switch (stmt.kind) {
        case mir::StatementKind::Phi: {
            auto& phi_data = std::get<mir::PhiNode>(stmt.data);

            // LLVM PHIノードを作成
            auto phi_type = convertType(currentMIRFunction->locals[phi_data.result].type);
            auto phi = builder->CreatePHI(phi_type, phi_data.predecessors.size());

            // 各predecessorからの値を追加
            for (const auto& [value_id, block_id] : phi_data.predecessors) {
                auto value = locals[value_id];
                auto block = blocks[block_id];
                phi->addIncoming(value, block);
            }

            locals[phi_data.result] = phi;
            break;
        }
        // ... 既存のケース処理 ...
    }
}
```

## 5. 実装計画

### Phase 1: 基盤整備（1-2日）
1. Phi命令の定義を追加
2. SSA値管理機構の実装
3. LoweringContextの拡張

### Phase 2: 論理演算の修正（1日）
1. AND/OR演算でPhi命令を使用
2. NOT演算の修正
3. 単体テストの作成

### Phase 3: LLVM変換（1日）
1. Phi命令のLLVM IR生成
2. 既存テストの通過確認

### Phase 4: 最適化と文書化（1日）
1. 不要なPhi命令の除去
2. 設計文書の更新
3. ロードマップの更新

## 6. 影響範囲

### 影響を受けるファイル
- `src/mir/include/statements.hpp` - Phi命令の定義
- `src/mir/lowering/lowering_context.hpp` - SSA管理機構
- `src/mir/lowering/expr_lowering_impl.cpp` - 論理演算の実装
- `src/codegen/llvm/mir_to_llvm.cpp` - LLVM変換
- `src/mir/mir_interpreter.hpp` - インタプリタでのPhi命令実行

### 後方互換性
- 既存のMIRコードは引き続き動作
- Phi命令は必要な場所にのみ生成される
- 最適化パスで不要なPhi命令を除去可能

## 7. テスト戦略

### 単体テスト
```cm
// tests/mir/test_phi_nodes.cm
int main() {
    // 基本的なPhi命令のテスト
    bool result = true && false;  // Phiが必要
    assert(result == false);

    // 複雑な制御フロー
    int x = 0;
    if (true) {
        x = 1;
    } else {
        x = 2;
    }  // ここでPhi命令
    assert(x == 1);

    return 0;
}
```

### 統合テスト
- 全ての論理演算パターンをテスト
- ネストした制御フローでのPhi命令
- ジェネリクス関数でのPhi命令（将来）

## 8. 将来の拡張

### ジェネリクスサポート（v0.6.0）
- モノモーフィゼーション時のPhi命令の特殊化
- 型パラメータを含むPhi命令の処理

### パターンマッチング（v0.10.0）
- match式の各armからの値統合
- ガード条件での複雑な制御フロー

### エラー伝播（v0.7.0）
- ?演算子での早期リターン
- Result型の値伝播

## 9. リスクと対策

### リスク
1. **パフォーマンス低下**: Phi命令の過剰生成
   - 対策: 最適化パスで不要なPhi命令を除去

2. **複雑性の増加**: SSA形式の管理が複雑
   - 対策: 明確なドキュメントとテストカバレッジ

3. **既存コードへの影響**: 互換性の問題
   - 対策: 段階的な移行と後方互換性の維持

## 10. まとめ

この再設計により：
1. 論理演算の不具合を根本的に解決
2. 将来の言語機能拡張に対応できる堅牢な基盤を構築
3. LLVMへの効率的な変換を実現

実装は段階的に行い、各フェーズでテストを実施して品質を確保します。