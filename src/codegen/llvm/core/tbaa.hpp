#pragma once

/// @file tbaa.hpp
/// @brief TBAA（Type Based Alias Analysis）メタデータ管理
/// ループベクトル化を有効にするためのエイリアス解析情報を提供

#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>

namespace cm::codegen::llvm_backend {

/// TBAAメタデータマネージャー
/// LLVMの型ベースエイリアス解析（TBAA）用のメタデータを管理
/// これにより、異なる型のポインタがエイリアスしないことをLLVMに伝え、
/// ループベクトル化などの最適化を有効にする
class TBAAManager {
   public:
    explicit TBAAManager(llvm::LLVMContext& ctx, llvm::Module& mod)
        : context_(ctx), module_(mod), mdBuilder_(ctx) {
        initializeTBAARoots();
    }

    /// int型のTBAAノードを取得
    llvm::MDNode* getIntTBAA() { return intNode_; }

    /// float型のTBAAノードを取得
    llvm::MDNode* getFloatTBAA() { return floatNode_; }

    /// double型のTBAAノードを取得
    llvm::MDNode* getDoubleTBAA() { return doubleNode_; }

    /// char型のTBAAノードを取得
    llvm::MDNode* getCharTBAA() { return charNode_; }

    /// pointer型のTBAAノードを取得
    llvm::MDNode* getPointerTBAA() { return pointerNode_; }

    /// 汎用のTBAAアクセスタグを取得
    /// @param baseType ベース型のTBAAノード
    /// @param accessType アクセス型のTBAAノード
    /// @param isConst constアクセスかどうか
    llvm::MDNode* createAccessTag(llvm::MDNode* baseType, llvm::MDNode* accessType,
                                  bool isConst = false) {
        return mdBuilder_.createTBAAStructTagNode(baseType, accessType, 0, isConst);
    }

    /// int型へのアクセスタグを取得
    llvm::MDNode* getIntAccessTag() {
        if (!intAccessTag_) {
            intAccessTag_ = mdBuilder_.createTBAAStructTagNode(intNode_, intNode_, 0);
        }
        return intAccessTag_;
    }

    /// float型へのアクセスタグを取得
    llvm::MDNode* getFloatAccessTag() {
        if (!floatAccessTag_) {
            floatAccessTag_ = mdBuilder_.createTBAAStructTagNode(floatNode_, floatNode_, 0);
        }
        return floatAccessTag_;
    }

    /// double型へのアクセスタグを取得
    llvm::MDNode* getDoubleAccessTag() {
        if (!doubleAccessTag_) {
            doubleAccessTag_ = mdBuilder_.createTBAAStructTagNode(doubleNode_, doubleNode_, 0);
        }
        return doubleAccessTag_;
    }

    /// char型へのアクセスタグを取得
    llvm::MDNode* getCharAccessTag() {
        if (!charAccessTag_) {
            charAccessTag_ = mdBuilder_.createTBAAStructTagNode(charNode_, charNode_, 0);
        }
        return charAccessTag_;
    }

    /// pointer型へのアクセスタグを取得
    llvm::MDNode* getPointerAccessTag() {
        if (!pointerAccessTag_) {
            pointerAccessTag_ = mdBuilder_.createTBAAStructTagNode(pointerNode_, pointerNode_, 0);
        }
        return pointerAccessTag_;
    }

   private:
    void initializeTBAARoots() {
        // TBAAルートノード作成
        // "Cm TBAA" はCm言語のTBAAツリーのルート
        root_ = mdBuilder_.createTBAARoot("Cm TBAA");

        // 基本型ノード作成（すべてcharからDERIVEしない独立した型として定義）
        // これにより、異なる型間のエイリアスなしが保証される
        charNode_ = mdBuilder_.createTBAAScalarTypeNode("cm_char", root_);
        intNode_ = mdBuilder_.createTBAAScalarTypeNode("cm_int", root_);
        floatNode_ = mdBuilder_.createTBAAScalarTypeNode("cm_float", root_);
        doubleNode_ = mdBuilder_.createTBAAScalarTypeNode("cm_double", root_);
        pointerNode_ = mdBuilder_.createTBAAScalarTypeNode("cm_pointer", root_);
    }

    llvm::LLVMContext& context_;
    llvm::Module& module_;
    llvm::MDBuilder mdBuilder_;

    // TBAAノードキャッシュ
    llvm::MDNode* root_ = nullptr;
    llvm::MDNode* charNode_ = nullptr;
    llvm::MDNode* intNode_ = nullptr;
    llvm::MDNode* floatNode_ = nullptr;
    llvm::MDNode* doubleNode_ = nullptr;
    llvm::MDNode* pointerNode_ = nullptr;

    // アクセスタグキャッシュ
    llvm::MDNode* charAccessTag_ = nullptr;
    llvm::MDNode* intAccessTag_ = nullptr;
    llvm::MDNode* floatAccessTag_ = nullptr;
    llvm::MDNode* doubleAccessTag_ = nullptr;
    llvm::MDNode* pointerAccessTag_ = nullptr;
};

}  // namespace cm::codegen::llvm_backend
