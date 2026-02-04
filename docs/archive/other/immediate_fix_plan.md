[English](immediate_fix_plan.en.html)

# 論理演算の即時修正案

## 1. 問題の分析

現在の論理演算（AND/OR/NOT）で発生している問題は、MIRレベルでの値の伝播に起因しています。SSA/Phi nodeの完全な実装は理想的ですが、時間がかかるため、まず即座に修正可能な代替案を提示します。

## 2. 即時修正案

### 案1: 条件分岐を使わずに算術演算として実装

```cpp
// src/mir/lowering/expr_lowering_impl.cpp
LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    if (bin.op == hir::HirBinaryOp::And) {
        // 両辺を評価（短絡評価なし）
        LocalId lhs = lower_expression(*bin.lhs, ctx);
        LocalId rhs = lower_expression(*bin.rhs, ctx);

        // ビット演算として実装: lhs & rhs
        LocalId result = ctx.new_temp(hir::make_bool());
        ctx.push_statement(MirStatement::assign(
            MirPlace{result},
            MirRvalue::binary_op(
                MirBinaryOp::BitAnd,  // 既存のビットAND演算を使用
                MirOperand::copy(MirPlace{lhs}),
                MirOperand::copy(MirPlace{rhs})
            )
        ));
        return result;
    }
    else if (bin.op == hir::HirBinaryOp::Or) {
        LocalId lhs = lower_expression(*bin.lhs, ctx);
        LocalId rhs = lower_expression(*bin.rhs, ctx);

        // ビット演算として実装: lhs | rhs
        LocalId result = ctx.new_temp(hir::make_bool());
        ctx.push_statement(MirStatement::assign(
            MirPlace{result},
            MirRvalue::binary_op(
                MirBinaryOp::BitOr,
                MirOperand::copy(MirPlace{lhs}),
                MirOperand::copy(MirPlace{rhs})
            )
        ));
        return result;
    }
    // ...
}
```

**メリット**:
- 実装が簡単
- 制御フローが単純
- SSA/Phi nodeが不要

**デメリット**:
- 短絡評価が効かない（両辺が常に評価される）
- パフォーマンスへの影響

### 案2: 三項演算子として実装

```cpp
LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    if (bin.op == hir::HirBinaryOp::And) {
        // lhs ? rhs : false として実装
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // 三項演算子の構造を作成
        auto ternary = std::make_unique<hir::HirTernary>();
        ternary->condition = std::make_unique<hir::HirExpr>(/* lhs を表す式 */);
        ternary->then_expr = bin.rhs;  // rhsをそのまま評価
        ternary->else_expr = std::make_unique<hir::HirLiteral>(false);

        return lower_ternary(*ternary, ctx);
    }
    // ...
}
```

**メリット**:
- 既存の三項演算子のロジックを再利用
- 短絡評価が実現できる
- 三項演算子が正しく動作していれば信頼性が高い

**デメリット**:
- HIR式の動的生成が必要
- コードが複雑になる

### 案3: LLVM IRで直接実装（推奨）

```cpp
// src/codegen/llvm/mir_to_llvm.cpp
llvm::Value* MIRToLLVM::convertLogicalAnd(llvm::Value* lhs, llvm::Value* rhs) {
    // 現在のブロックと関数を取得
    auto currentFunc = builder->GetInsertBlock()->getParent();

    // ブロックを作成
    auto evalRhsBB = llvm::BasicBlock::Create(ctx, "and.rhs", currentFunc);
    auto mergeBB = llvm::BasicBlock::Create(ctx, "and.merge", currentFunc);

    // lhsをboolに変換（i8 -> i1）
    auto lhsBool = builder->CreateICmpNE(lhs,
                                         llvm::ConstantInt::get(ctx.getI8Type(), 0));

    // 条件分岐
    builder->CreateCondBr(lhsBool, evalRhsBB, mergeBB);

    // RHSを評価
    builder->SetInsertPoint(evalRhsBB);
    auto rhsBool = builder->CreateICmpNE(rhs,
                                         llvm::ConstantInt::get(ctx.getI8Type(), 0));
    auto rhsI8 = builder->CreateZExt(rhsBool, ctx.getI8Type());
    builder->CreateBr(mergeBB);

    // PHIノード
    builder->SetInsertPoint(mergeBB);
    auto phi = builder->CreatePHI(ctx.getI8Type(), 2);
    phi->addIncoming(llvm::ConstantInt::get(ctx.getI8Type(), 0),
                    builder->GetInsertBlock()->getSinglePredecessor());
    phi->addIncoming(rhsI8, evalRhsBB);

    return phi;
}
```

**メリット**:
- LLVM IRレベルで正確な制御が可能
- PHIノードをLLVMが自動管理
- 短絡評価も実装可能

**デメリット**:
- MIRレベルでの表現と乖離
- デバッグが難しい

## 3. 推奨する段階的アプローチ

### Step 1: 即時修正（1時間）
案1（ビット演算）を実装して、機能的な正確性を確保

### Step 2: 短絡評価の復活（2時間）
案3（LLVM IR直接実装）で短絡評価を実現

### Step 3: 長期的改善（1週間）
SSA/Phi nodeの完全実装（別途設計書参照）

## 4. NOT演算の修正

NOT演算については、より単純な修正が可能です：

```cpp
// src/codegen/llvm/mir_to_llvm.cpp
case mir::MirUnaryOp::Not: {
    // i8の0/1を反転: value XOR 1
    auto one = llvm::ConstantInt::get(ctx.getI8Type(), 1);
    return builder->CreateXor(operand, one, "logical_not");
}
```

## 5. テストケース

```cm
// tests/logical_ops_fix.cm
int main() {
    // AND演算
    assert((true && true) == true);
    assert((true && false) == false);
    assert((false && true) == false);
    assert((false && false) == false);

    // OR演算
    assert((true || true) == true);
    assert((true || false) == true);
    assert((false || true) == true);
    assert((false || false) == false);

    // NOT演算
    assert(!true == false);
    assert(!false == true);

    // 複合演算
    assert(!(true && false) == true);
    assert((true || false) && !false == true);

    println("All tests passed!");
    return 0;
}
```

## 6. 実装優先度

1. **最優先**: NOT演算の修正（XOR実装）- 最も簡単
2. **高**: AND/OR演算のビット演算実装 - 機能的正確性を確保
3. **中**: LLVM IRレベルでの短絡評価 - パフォーマンス改善
4. **低**: SSA/Phi node完全実装 - 長期的な設計改善

この段階的アプローチにより、即座に問題を解決しながら、将来的により良い実装に移行できます。