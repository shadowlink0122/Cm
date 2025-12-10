#pragma once

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <memory>
#include <string>

namespace cm::codegen::llvm_backend {

/// ビルドターゲット
enum class BuildTarget {
    Baremetal,  // no_std, no_main
    Native,     // std, OS依存
    Wasm        // WebAssembly
};

/// ターゲット設定
struct TargetConfig {
    BuildTarget target;
    std::string triple;      // LLVM target triple
    std::string cpu;         // CPU種別
    std::string features;    // CPU機能フラグ
    std::string dataLayout;  // データレイアウト
    bool noStd = false;      // no_std モード
    bool noMain = false;     // no_main モード
    bool debugInfo = false;  // デバッグ情報生成
    int optLevel = 2;        // 最適化レベル (0-3, -1=サイズ)

    /// デフォルト設定取得
    static TargetConfig getDefault(BuildTarget target);

    /// ベアメタル ARM Cortex-M
    static TargetConfig getBaremetalARM() {
        return {
            .target = BuildTarget::Baremetal,
            .triple = "thumbv7m-none-eabi",
            .cpu = "cortex-m3",
            .features = "+thumb2",
            .dataLayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64",
            .noStd = true,
            .noMain = true,
            .optLevel = -1  // サイズ最適化
        };
    }

    /// ネイティブ（ホストOS）
    static TargetConfig getNative();

    /// WebAssembly
    static TargetConfig getWasm() {
        return {
            .target = BuildTarget::Wasm,
            .triple = "wasm32-unknown-wasi",  // WASI用のトリプル
            .cpu = "generic",
            .features = "+simd128",
            .dataLayout = "e-m:e-p:32:32-i64:64-n32:64-S128",
            .noStd = true,
            .noMain = false,
            .optLevel = -1  // サイズ最適化
        };
    }
};

/// LLVM コンテキスト管理
class LLVMContext {
   private:
    // LLVM基本オブジェクト
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    std::unique_ptr<llvm::DIBuilder> debugBuilder;

    // ターゲット設定
    TargetConfig targetConfig;
    // 将来の最適化用（現在未使用）
    [[maybe_unused]] llvm::TargetMachine* targetMachine = nullptr;

    // 組み込み型キャッシュ
    llvm::Type* voidTy = nullptr;
    llvm::Type* boolTy = nullptr;
    llvm::Type* i8Ty = nullptr;
    llvm::Type* i16Ty = nullptr;
    llvm::Type* i32Ty = nullptr;
    llvm::Type* i64Ty = nullptr;
    llvm::Type* f32Ty = nullptr;
    llvm::Type* f64Ty = nullptr;
    llvm::Type* ptrTy = nullptr;  // Opaque pointer (LLVM 15+)

   public:
    /// コンストラクタ
    explicit LLVMContext(const std::string& moduleName, const TargetConfig& config);

    /// デストラクタ
    ~LLVMContext();

    // アクセサ
    llvm::LLVMContext& getContext() { return *context; }
    llvm::Module& getModule() { return *module; }
    llvm::IRBuilder<>& getBuilder() { return *builder; }
    const TargetConfig& getTargetConfig() const { return targetConfig; }

    /// 基本型取得
    llvm::Type* getVoidType() { return voidTy; }
    llvm::Type* getBoolType() { return boolTy; }
    llvm::Type* getI8Type() { return i8Ty; }
    llvm::Type* getI16Type() { return i16Ty; }
    llvm::Type* getI32Type() { return i32Ty; }
    llvm::Type* getI64Type() { return i64Ty; }
    llvm::Type* getF32Type() { return f32Ty; }
    llvm::Type* getF64Type() { return f64Ty; }
    llvm::Type* getPtrType() { return ptrTy; }

    /// サイズ型（usize/isize）取得
    llvm::Type* getSizeType(bool isSigned = false) {
        // ターゲットのポインタサイズに応じて決定
        if (targetConfig.triple.find("64") != std::string::npos) {
            return isSigned ? i64Ty : i64Ty;
        } else {
            return isSigned ? i32Ty : i32Ty;
        }
    }

    /// 組み込み関数宣言
    void declareIntrinsics();

    /// ランタイム関数宣言（no_std用）
    void declareRuntimeFunctions();

    /// パニックハンドラ宣言
    llvm::Function* declarePanicHandler();

    /// メモリ操作関数
    llvm::Function* getMemcpy() { return module->getFunction("memcpy"); }
    llvm::Function* getMemset() { return module->getFunction("memset"); }
    llvm::Function* getMemcmp() { return module->getFunction("memcmp"); }

    /// アロケータ関数（カスタム実装可能）
    llvm::Function* getAlloc() { return module->getFunction("__cm_alloc"); }
    llvm::Function* getDealloc() { return module->getFunction("__cm_dealloc"); }

    /// エントリーポイント設定
    void setupEntryPoint(llvm::Function* mainFunc);

    /// モジュール検証
    bool verify() const;

    /// LLVM IR 出力（デバッグ用）
    void dumpIR() const;

   private:
    /// ターゲットマシン初期化
    void initializeTarget();

    /// 基本型初期化
    void initializeTypes();

    /// no_std モード用の最小限の宣言
    void setupNoStd();

    /// std モード用の標準ライブラリ宣言
    void setupStd();
};

}  // namespace cm::codegen::llvm_backend