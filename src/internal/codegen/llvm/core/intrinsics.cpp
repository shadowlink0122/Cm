// ============================================================
// 組み込み関数管理（IntrinsicsManager）の実装
// ============================================================
// intrinsics.hpp で宣言されたメソッドの実装。ヘッダーには宣言のみを置く

#include "intrinsics.hpp"

namespace cm::codegen::llvm_backend {

void IntrinsicsManager::declareAll() {
    // メモリ操作
    declareMemoryIntrinsics();

    // 数学関数
    declareMathIntrinsics();

    // ビット操作
    declareBitIntrinsics();

    // アトミック操作
    if (!config.noStd) {
        declareAtomicIntrinsics();
    }

    // プラットフォーム固有
    declarePlatformSpecific();
}

llvm::Function* IntrinsicsManager::get(const std::string& name) {
    auto it = intrinsics.find(name);
    if (it != intrinsics.end()) {
        return it->second;
    }

    // LLVM組み込み関数を検索
    if (name.starts_with("llvm.")) {
        return getOrDeclareLLVMIntrinsic(name);
    }

    return nullptr;
}

void IntrinsicsManager::declareMemoryIntrinsics() {
    auto& ctx = *context;

// LLVM 15+ではopaqueポインタ（ptr）を使用
#if LLVM_VERSION_MAJOR >= 15
    auto ptrType = llvm::PointerType::get(ctx, 0);
#else
    auto ptrType = llvm::PointerType::get(ctx, 0);
#endif

// memcpy
#if LLVM_VERSION_MAJOR >= 15
    auto memcpyName = "llvm.memcpy.p0.p0.i64";
#else
    auto memcpyName = "llvm.memcpy.p0i8.p0i8.i64";
#endif
    auto memcpyType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                              {ptrType,                      // dest
                                               ptrType,                      // src
                                               llvm::Type::getInt64Ty(ctx),  // size
                                               llvm::Type::getInt1Ty(ctx)},  // is_volatile
                                              false);
    auto memcpy =
        llvm::Function::Create(memcpyType, llvm::Function::ExternalLinkage, memcpyName, module);
    memcpy->addFnAttr(llvm::Attribute::NoUnwind);
    // LLVM 18+: Memory属性は削除され、自動推論される
    intrinsics["memcpy"] = memcpy;

// memset
#if LLVM_VERSION_MAJOR >= 15
    auto memsetName = "llvm.memset.p0.i64";
#else
    auto memsetName = "llvm.memset.p0i8.i64";
#endif
    auto memsetType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                              {ptrType,                      // dest
                                               llvm::Type::getInt8Ty(ctx),   // value
                                               llvm::Type::getInt64Ty(ctx),  // size
                                               llvm::Type::getInt1Ty(ctx)},  // is_volatile
                                              false);
    auto memset =
        llvm::Function::Create(memsetType, llvm::Function::ExternalLinkage, memsetName, module);
    memset->addFnAttr(llvm::Attribute::NoUnwind);
    // LLVM 18+: Memory属性は削除され、自動推論される
    intrinsics["memset"] = memset;

// memmove
#if LLVM_VERSION_MAJOR >= 15
    auto memmoveName = "llvm.memmove.p0.p0.i64";
#else
    auto memmoveName = "llvm.memmove.p0i8.p0i8.i64";
#endif
    auto memmoveType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                               {ptrType,                      // dest
                                                ptrType,                      // src
                                                llvm::Type::getInt64Ty(ctx),  // size
                                                llvm::Type::getInt1Ty(ctx)},  // is_volatile
                                               false);
    auto memmove =
        llvm::Function::Create(memmoveType, llvm::Function::ExternalLinkage, memmoveName, module);
    memmove->addFnAttr(llvm::Attribute::NoUnwind);
    // LLVM 18+: Memory属性は削除され、自動推論される
    intrinsics["memmove"] = memmove;
}

void IntrinsicsManager::declareMathIntrinsics() {
    auto& ctx = *context;

    // sqrt (f32)
    declareMathFunc("sqrt.f32", llvm::Type::getFloatTy(ctx));

    // sqrt (f64)
    declareMathFunc("sqrt.f64", llvm::Type::getDoubleTy(ctx));

    // sin, cos, log, exp など
    for (const char* name : {"sin", "cos", "tan", "log", "exp", "pow"}) {
        declareMathFunc(std::string(name) + ".f32", llvm::Type::getFloatTy(ctx));
        declareMathFunc(std::string(name) + ".f64", llvm::Type::getDoubleTy(ctx));
    }

    // abs
    declareAbsFunc("abs.i32", llvm::Type::getInt32Ty(ctx));
    declareAbsFunc("abs.i64", llvm::Type::getInt64Ty(ctx));
}

void IntrinsicsManager::declareBitIntrinsics() {
    auto& ctx = *context;

    // bswap (バイトスワップ)
    declareBitFunc("bswap.i16", llvm::Type::getInt16Ty(ctx));
    declareBitFunc("bswap.i32", llvm::Type::getInt32Ty(ctx));
    declareBitFunc("bswap.i64", llvm::Type::getInt64Ty(ctx));

    // ctpop (ビットカウント)
    declareBitFunc("ctpop.i8", llvm::Type::getInt8Ty(ctx));
    declareBitFunc("ctpop.i32", llvm::Type::getInt32Ty(ctx));
    declareBitFunc("ctpop.i64", llvm::Type::getInt64Ty(ctx));

    // ctlz (leading zeros)
    declareBitFunc2("ctlz.i32", llvm::Type::getInt32Ty(ctx));
    declareBitFunc2("ctlz.i64", llvm::Type::getInt64Ty(ctx));

    // cttz (trailing zeros)
    declareBitFunc2("cttz.i32", llvm::Type::getInt32Ty(ctx));
    declareBitFunc2("cttz.i64", llvm::Type::getInt64Ty(ctx));
}

void IntrinsicsManager::declareAtomicIntrinsics() {
    // fence
    auto fenceType = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                                             {llvm::Type::getInt32Ty(*context)},  // ordering
                                             false);
    auto fence = llvm::Function::Create(fenceType, llvm::Function::ExternalLinkage,
                                        "__cm_atomic_fence", module);
    intrinsics["atomic.fence"] = fence;

    // TODO: atomic load/store/cmpxchg
}

void IntrinsicsManager::declarePlatformSpecific() {
    if (config.target == BuildTarget::Baremetal) {
        declareBaremetalIntrinsics();
    } else if (config.target == BuildTarget::Wasm) {
        declareWasmIntrinsics();
    } else {
        declareNativeIntrinsics();
    }
}

void IntrinsicsManager::declareBaremetalIntrinsics() {
    auto& ctx = *context;

    // 割り込み無効化/有効化
    auto voidType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);

    auto disableIrq = llvm::Function::Create(voidType, llvm::Function::ExternalLinkage,
                                             "__cm_disable_irq", module);
    intrinsics["disable_irq"] = disableIrq;

    auto enableIrq = llvm::Function::Create(voidType, llvm::Function::ExternalLinkage,
                                            "__cm_enable_irq", module);
    intrinsics["enable_irq"] = enableIrq;

    // NOP
    auto nop =
        llvm::Function::Create(voidType, llvm::Function::ExternalLinkage, "__cm_nop", module);
    intrinsics["nop"] = nop;

    // WFI (Wait For Interrupt)
    auto wfi =
        llvm::Function::Create(voidType, llvm::Function::ExternalLinkage, "__cm_wfi", module);
    intrinsics["wfi"] = wfi;
}

void IntrinsicsManager::declareWasmIntrinsics() {
    auto& ctx = *context;

    // memory.grow
    auto growType = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx),
                                            {llvm::Type::getInt32Ty(ctx)},  // pages
                                            false);
    auto grow = llvm::Function::Create(growType, llvm::Function::ExternalLinkage,
                                       "__wasm_memory_grow", module);
    intrinsics["wasm.memory.grow"] = grow;

    // memory.size
    auto sizeType = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), false);
    auto size = llvm::Function::Create(sizeType, llvm::Function::ExternalLinkage,
                                       "__wasm_memory_size", module);
    intrinsics["wasm.memory.size"] = size;
}

void IntrinsicsManager::declareNativeIntrinsics() {
    // OS依存の組み込み（システムコールなど）
    if (config.triple.find("linux") != std::string::npos) {
        declareLinuxSyscalls();
    } else if (config.triple.find("windows") != std::string::npos) {
        declareWindowsIntrinsics();
    } else if (config.triple.find("darwin") != std::string::npos) {
        declareDarwinIntrinsics();
    }
}

void IntrinsicsManager::declareLinuxSyscalls() {
    auto& ctx = *context;

    // syscall
    auto syscallType = llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx),
                                               {llvm::Type::getInt64Ty(ctx)},  // syscall number
                                               true                            // variadic
    );
    auto syscall =
        llvm::Function::Create(syscallType, llvm::Function::ExternalLinkage, "syscall", module);
    intrinsics["syscall"] = syscall;
}

void IntrinsicsManager::declareWindowsIntrinsics() {
    // Windows固有の組み込み
}

void IntrinsicsManager::declareDarwinIntrinsics() {
    // macOS/iOS固有の組み込み
}

void IntrinsicsManager::declareMathFunc(const std::string& name, llvm::Type* type) {
    auto funcType = llvm::FunctionType::get(type, {type}, false);
    auto func =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "llvm." + name, module);
    func->addFnAttr(llvm::Attribute::NoUnwind);
    func->addFnAttr(llvm::Attribute::ReadNone);
    intrinsics[name] = func;
}

void IntrinsicsManager::declareAbsFunc(const std::string& name, llvm::Type* type) {
    auto funcType =
        llvm::FunctionType::get(type, {type, llvm::Type::getInt1Ty(*context)},  // has_nsw
                                false);
    auto func =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "llvm." + name, module);
    func->addFnAttr(llvm::Attribute::NoUnwind);
    func->addFnAttr(llvm::Attribute::ReadNone);
    intrinsics[name] = func;
}

void IntrinsicsManager::declareBitFunc(const std::string& name, llvm::Type* type) {
    auto funcType = llvm::FunctionType::get(type, {type}, false);
    auto func =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "llvm." + name, module);
    func->addFnAttr(llvm::Attribute::NoUnwind);
    func->addFnAttr(llvm::Attribute::ReadNone);
    intrinsics[name] = func;
}

void IntrinsicsManager::declareBitFunc2(const std::string& name, llvm::Type* type) {
    auto funcType =
        llvm::FunctionType::get(type, {type, llvm::Type::getInt1Ty(*context)},  // is_zero_undef
                                false);
    auto func =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "llvm." + name, module);
    func->addFnAttr(llvm::Attribute::NoUnwind);
    func->addFnAttr(llvm::Attribute::ReadNone);
    intrinsics[name] = func;
}

llvm::Function* IntrinsicsManager::getOrDeclareLLVMIntrinsic(const std::string& name) {
    // 既存の関数を検索
    if (auto func = module->getFunction(name)) {
        return func;
    }

    // TODO: Intrinsics::IDから自動生成
    return nullptr;
}

}  // namespace cm::codegen::llvm_backend
