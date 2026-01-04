#include "context.hpp"

#include "../../../common/debug/codegen.hpp"

#include <llvm/Config/llvm-config.h>  // LLVM_VERSION_MAJOR を定義（最初にインクルード）
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <stdexcept>

namespace cm::codegen::llvm_backend {

// ターゲット設定のデフォルト取得
TargetConfig TargetConfig::getDefault(BuildTarget target) {
    switch (target) {
        case BuildTarget::Baremetal:
            return getBaremetalARM();
        case BuildTarget::Wasm:
            return getWasm();
        default:
            return getNative();
    }
}

// ネイティブターゲット設定
TargetConfig TargetConfig::getNative() {
    return {.target = BuildTarget::Native,
            .triple = llvm::sys::getDefaultTargetTriple(),
            .cpu = "generic",
            .features = "",
            .dataLayout = "",  // 後で設定
            .noStd = false,
            .noMain = false,
            .debugInfo = false,
            .optLevel = 2};
}

// LLVMコンテキストのコンストラクタ
LLVMContext::LLVMContext(const std::string& moduleName, const TargetConfig& config)
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>(moduleName, *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)),
      targetConfig(config) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit, "Module: " + moduleName);

#if LLVM_VERSION_MAJOR >= 15 && LLVM_VERSION_MAJOR < 17
    // LLVM 15-16でopaque pointerを有効化
    // LLVM 17+では常に有効なため不要
    context->setOpaquePointers(true);
#endif

    // ターゲット初期化
    initializeTarget();

    // 基本型初期化
    initializeTypes();

    // 組み込み関数宣言
    declareIntrinsics();

    // ランタイム関数宣言
    if (config.noStd) {
        setupNoStd();
    } else {
        setupStd();
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInitOK);
}

LLVMContext::~LLVMContext() = default;

// ターゲットマシン初期化
void LLVMContext::initializeTarget() {
    // すべてのターゲットを初期化
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    // モジュール設定
    module->setTargetTriple(targetConfig.triple);

    // データレイアウト設定（簡易版）
    if (!targetConfig.dataLayout.empty()) {
        module->setDataLayout(targetConfig.dataLayout);
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMTarget, targetConfig.triple);
}

// 基本型初期化
void LLVMContext::initializeTypes() {
    auto& ctx = *context;

    voidTy = llvm::Type::getVoidTy(ctx);
    boolTy = llvm::Type::getInt1Ty(ctx);
    i8Ty = llvm::Type::getInt8Ty(ctx);
    i16Ty = llvm::Type::getInt16Ty(ctx);
    i32Ty = llvm::Type::getInt32Ty(ctx);
    i64Ty = llvm::Type::getInt64Ty(ctx);
    f32Ty = llvm::Type::getFloatTy(ctx);
    f64Ty = llvm::Type::getDoubleTy(ctx);
#if LLVM_VERSION_MAJOR >= 15
    // LLVM 15+では不透明ポインタを使用
    ptrTy = llvm::PointerType::getUnqual(ctx);
#else
    // LLVM 14では型付きポインタを使用（i8*）
    ptrTy = llvm::PointerType::get(i8Ty, 0);
#endif
}

// 組み込み関数宣言（基本的なものだけ）
void LLVMContext::declareIntrinsics() {
    // 注意: 組み込み関数は使用時にオンデマンドで宣言される
    // ここでは最小限の宣言のみ行う

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMIntrinsics, "lazy initialization",
                            cm::debug::Level::Trace);
}

// ランタイム関数宣言
void LLVMContext::declareRuntimeFunctions() {
    // printf (デバッグ用)
    auto printfType = llvm::FunctionType::get(i32Ty, {ptrTy},  // format string
                                              true             // variadic
    );
    module->getOrInsertFunction("printf", printfType);

    // puts (簡易出力)
    auto putsType = llvm::FunctionType::get(i32Ty, {ptrTy},  // string
                                            false);
    module->getOrInsertFunction("puts", putsType);
}

// パニックハンドラ宣言
llvm::Function* LLVMContext::declarePanicHandler() {
    auto panicType = llvm::FunctionType::get(voidTy, {ptrTy},  // message
                                             false);

    auto panicFunc = llvm::Function::Create(panicType, llvm::Function::ExternalLinkage,
                                            "__cm_panic", module.get());

    panicFunc->addFnAttr(llvm::Attribute::NoReturn);
    panicFunc->addFnAttr(llvm::Attribute::Cold);

    return panicFunc;
}

// no_std モード設定
void LLVMContext::setupNoStd() {
    // パニックハンドラ
    declarePanicHandler();

    // カスタムアロケータ関数
    auto allocType = llvm::FunctionType::get(ptrTy, {i64Ty, i64Ty},  // size, align
                                             false);
    module->getOrInsertFunction("__cm_alloc", allocType);

    auto deallocType = llvm::FunctionType::get(voidTy, {ptrTy, i64Ty, i64Ty},  // ptr, size, align
                                               false);
    module->getOrInsertFunction("__cm_dealloc", deallocType);

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMRuntime, "no_std mode",
                            cm::debug::Level::Debug);
}

// std モード設定
void LLVMContext::setupStd() {
    // 標準ライブラリ関数
    declareRuntimeFunctions();

    // malloc/free
    auto mallocType = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
    module->getOrInsertFunction("malloc", mallocType);

    auto freeType = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    module->getOrInsertFunction("free", freeType);

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMRuntime, "std mode",
                            cm::debug::Level::Debug);
}

// エントリーポイント設定
void LLVMContext::setupEntryPoint(llvm::Function* mainFunc) {
    if (targetConfig.noMain) {
        // no_main モード：_startをエントリーポイントに
        mainFunc->setName("_start");
    } else {
        // 通常モード：main
        mainFunc->setName("main");
    }
}

// モジュール検証
bool LLVMContext::verify() const {
    std::string errors;
    llvm::raw_string_ostream os(errors);

    if (llvm::verifyModule(*module, &os)) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, "Verification failed: " + errors,
                                cm::debug::Level::Error);
        return false;
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerifyOK);
    return true;
}

// LLVM IR 出力
void LLVMContext::dumpIR() const {
    std::string ir;
    llvm::raw_string_ostream os(ir);
    module->print(os, nullptr);

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMDump, "LLVM IR", cm::debug::Level::Trace);

    // デバッグモードの場合、IRを標準エラー出力に出力
    if (cm::debug::g_debug_mode && cm::debug::g_debug_level <= cm::debug::Level::Trace) {
        llvm::errs() << ir;
    }
}

}  // namespace cm::codegen::llvm_backend