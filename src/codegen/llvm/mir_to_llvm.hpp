#pragma once

#include "../../mir/mir_nodes.hpp"
#include "context.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <unordered_map>
#include <vector>

namespace cm::codegen::llvm_backend {

/// MIR → LLVM IR 変換器
class MIRToLLVM {
   private:
    LLVMContext& ctx;
    llvm::Module* module;
    llvm::IRBuilder<>* builder;

    // 現在の関数コンテキスト
    llvm::Function* currentFunction = nullptr;
    const mir::MirFunction* currentMIRFunction = nullptr;

    // ローカル変数マッピング
    std::unordered_map<mir::LocalId, llvm::Value*> locals;

    // 基本ブロックマッピング
    std::unordered_map<mir::BlockId, llvm::BasicBlock*> blocks;

    // グローバル変数/関数マッピング
    std::unordered_map<std::string, llvm::GlobalVariable*> globals;
    std::unordered_map<std::string, llvm::Function*> functions;

   public:
    /// コンストラクタ
    explicit MIRToLLVM(LLVMContext& context)
        : ctx(context), module(&context.getModule()), builder(&context.getBuilder()) {}

    /// MIRプログラム全体を変換
    void convert(const mir::MirProgram& program);

    /// 型変換（公開：関数シグネチャ生成で使用）
    llvm::Type* convertType(const hir::TypePtr& type);

   private:
    /// 関数変換
    void convertFunction(const mir::MirFunction& func);

    /// 基本ブロック変換
    void convertBasicBlock(const mir::BasicBlock& block);

    /// ステートメント変換
    void convertStatement(const mir::MirStatement& stmt);

    /// ターミネータ変換
    void convertTerminator(const mir::MirTerminator& term);

    /// 右辺値変換
    llvm::Value* convertRvalue(const mir::MirRvalue& rvalue);

    /// オペランド変換
    llvm::Value* convertOperand(const mir::MirOperand& operand);

    /// Place変換（アドレス取得）
    llvm::Value* convertPlaceToAddress(const mir::MirPlace& place);

    /// 定数値変換
    llvm::Constant* convertConstant(const mir::MirConstant& value);

    /// 二項演算変換
    llvm::Value* convertBinaryOp(mir::MirBinaryOp op, llvm::Value* lhs, llvm::Value* rhs);

    /// 単項演算変換
    llvm::Value* convertUnaryOp(mir::MirUnaryOp op, llvm::Value* operand);

    /// フォーマット変換
    llvm::Value* convertFormatConvert(llvm::Value* value, const std::string& format_spec);

    /// 外部関数宣言
    llvm::Function* declareExternalFunction(const std::string& name);

    /// 組み込み関数呼び出し
    llvm::Value* callIntrinsic(const std::string& name, llvm::ArrayRef<llvm::Value*> args);

    /// パニック生成
    void generatePanic(const std::string& message);

    /// MIRオペランドからHIR型情報を取得
    hir::TypePtr getOperandType(const mir::MirOperand& operand);
};

}  // namespace cm::codegen::llvm_backend