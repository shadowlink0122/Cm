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

// WebAssemblyターゲット初期化関数のextern C宣言
extern "C" {
void LLVMInitializeWebAssemblyTargetInfo();
void LLVMInitializeWebAssemblyTarget();
void LLVMInitializeWebAssemblyTargetMC();
void LLVMInitializeWebAssemblyAsmParser();
void LLVMInitializeWebAssemblyAsmPrinter();
}

namespace cm::codegen::llvm_backend {

// ターゲット設定のデフォルト取得
TargetConfig TargetConfig::getDefault(BuildTarget target) {
    switch (target) {
        case BuildTarget::Baremetal:
            return getBaremetalARM();
        case BuildTarget::BaremetalX86:
            return getBaremetalX86();
        case BuildTarget::Wasm:
            return getWasm();
        case BuildTarget::BaremetalUEFI:
            return getBaremetalUEFI();
        default:
            return getNative();
    }
}

// TargetConfig::getNative() はtarget.cppで定義

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
    // ネイティブ（ホスト）ターゲットの初期化
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // WebAssemblyターゲットの初期化（クロスコンパイル用）
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();

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

    // WASM32ではsize_t = i32、ネイティブではsize_t = i64
    auto sizeTy = (targetConfig.target == BuildTarget::Wasm) ? i32Ty : i64Ty;

    // malloc/free
    auto mallocType = llvm::FunctionType::get(ptrTy, {sizeTy}, false);
    module->getOrInsertFunction("malloc", mallocType);

    auto freeType = llvm::FunctionType::get(voidTy, {ptrTy}, false);
    module->getOrInsertFunction("free", freeType);

    // calloc (WASM互換)
    auto callocType = llvm::FunctionType::get(ptrTy, {sizeTy, sizeTy}, false);
    module->getOrInsertFunction("calloc", callocType);

    // realloc (WASM互換)
    auto reallocType = llvm::FunctionType::get(ptrTy, {ptrTy, sizeTy}, false);
    module->getOrInsertFunction("realloc", reallocType);

    // memcpy/memmove/memset (WASM互換)
    auto memcpyType = llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy, sizeTy}, false);
    module->getOrInsertFunction("memcpy", memcpyType);
    module->getOrInsertFunction("memmove", memcpyType);

    auto memsetType = llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty, sizeTy}, false);
    module->getOrInsertFunction("memset", memsetType);

    // ============================================================
    // POSIX I/O 関数宣言（libc経由）
    // ============================================================

    // ssize_t read(int fd, void* buf, size_t count)
    auto readType = llvm::FunctionType::get(sizeTy, {i32Ty, ptrTy, sizeTy}, false);
    module->getOrInsertFunction("read", readType);

    // ssize_t write(int fd, const void* buf, size_t count)
    auto writeType = llvm::FunctionType::get(sizeTy, {i32Ty, ptrTy, sizeTy}, false);
    module->getOrInsertFunction("write", writeType);

    // int open(const char* pathname, int flags, mode_t mode)
    auto openType = llvm::FunctionType::get(i32Ty, {ptrTy, i32Ty, i32Ty}, false);
    module->getOrInsertFunction("open", openType);

    // int close(int fd)
    auto closeType = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    module->getOrInsertFunction("close", closeType);

    // off_t lseek(int fd, off_t offset, int whence)
    auto lseekType = llvm::FunctionType::get(sizeTy, {i32Ty, sizeTy, i32Ty}, false);
    module->getOrInsertFunction("lseek", lseekType);

    // int fsync(int fd)
    auto fsyncType = llvm::FunctionType::get(i32Ty, {i32Ty}, false);
    module->getOrInsertFunction("fsync", fsyncType);

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMRuntime, "std mode",
                            cm::debug::Level::Debug);
}

// エントリーポイント設定
void LLVMContext::setupEntryPoint(llvm::Function* mainFunc) {
    if (targetConfig.target == BuildTarget::BaremetalUEFI) {
        // UEFIモード：efi_mainはユーザーが明示的に定義する
        // setupEntryPointでのリネームは不要
        // Win64呼出規約はmir_to_llvm.cppでefi_main関数に直接設定される
    } else if (targetConfig.noMain) {
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