// LLVMネイティブバックエンドのリンク処理とランタイム解決
// 複数オブジェクトのリンク（linkObjects）・ランタイムライブラリ探索/オンデマンドコンパイル・使用ライブラリ検出を担う
#include "internal/codegen/llvm/native/codegen.hpp"
#include "internal/mir/splitter.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

// 複数オブジェクトファイルからリンク
void LLVMCodeGen::linkObjects(const std::vector<std::filesystem::path>& objects,
                              const std::string& output_file, const mir::MirProgram* program) {
    // 初期化が必要（ターゲット情報取得のため）
    if (!context) {
        initialize("link_module");
    }

    // 使用ライブラリの検出
    // MIRが渡された場合は呼び出し関数名の接頭辞から判定する（モジュール分割経路ではcontextが空でLLVM宣言ベースの判定が効かない）
    bool needsGPU = false;
    bool needsNet = false;
    bool needsSync = false;
    bool needsThread = false;
    bool needsHTTP = false;
    if (program) {
        auto has_prefix = [](const std::string& name, const char* prefix) {
            return name.rfind(prefix, 0) == 0;
        };
        for (const auto& func : program->functions) {
            if (!func)
                continue;
            for (const auto& called : mir::MirSplitter::collect_called_functions(*func)) {
                needsGPU = needsGPU || has_prefix(called, "gpu_");
                needsNet = needsNet || has_prefix(called, "cm_tcp_") ||
                           has_prefix(called, "cm_udp_") || has_prefix(called, "cm_dns_") ||
                           has_prefix(called, "cm_socket_");
                needsSync =
                    needsSync || has_prefix(called, "cm_mutex_") ||
                    has_prefix(called, "cm_rwlock_") || has_prefix(called, "cm_atomic_") ||
                    has_prefix(called, "cm_channel_") || has_prefix(called, "cm_once_") ||
                    has_prefix(called, "atomic_store_") || has_prefix(called, "atomic_load_") ||
                    has_prefix(called, "atomic_fetch_") || has_prefix(called, "atomic_compare_");
                needsThread = needsThread || has_prefix(called, "cm_thread_");
                needsHTTP = needsHTTP || has_prefix(called, "cm_http_");
            }
        }
    } else {
        needsGPU = checkForGPUUsage();
        needsNet = checkForNetUsage();
        needsSync = checkForSyncUsage();
        needsThread = checkForThreadUsage();
        needsHTTP = checkForHTTPUsage();
    }
    bool needsPthread = needsSync || needsThread;
    bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

    // オブジェクトファイルリストを構築
    std::string obj_list;
    for (const auto& obj : objects) {
        obj_list += obj.string() + " ";
    }

    std::string linkCmd;
    auto target = context->getTargetConfig().target;

    if (target == BuildTarget::Baremetal) {
        linkCmd = "arm-none-eabi-ld -T link.ld " + obj_list + "-o " + output_file;
    } else if (target == BuildTarget::BaremetalUEFI) {
        linkCmd = "lld-link /subsystem:efi_application /entry:efi_main /out:" + output_file + " " +
                  obj_list;
    } else if (target == BuildTarget::Wasm) {
        std::string runtimePath = findRuntimeLibrary();
        linkCmd = "wasm-ld --entry=_start -z stack-size=1048576 " + obj_list + runtimePath +
                  " -o " + output_file;
    } else {
        // ネイティブリンク
        std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
        linkCmd = "/usr/bin/clang++ -mmacosx-version-min=15.0 -Wl,-dead_strip ";
#ifdef CM_DEFAULT_TARGET_ARCH
        linkCmd += "-arch " + std::string(CM_DEFAULT_TARGET_ARCH) + " ";
#endif
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += obj_list + runtimePath;

        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty()) {
                linkCmd += " " + gpuRuntimePath;
                linkCmd += " -framework Metal -framework Foundation";
            }
        }
        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            // HTTPランタイムはOpenSSLへ依存する（emit側と同じ探索順: Homebrewの既知プレフィックス→brew --prefix）
            std::string opensslPrefix;
#ifdef CM_DEFAULT_TARGET_ARCH
            std::string targetArch = CM_DEFAULT_TARGET_ARCH;
#else
            std::string targetArch = "arm64";
#endif
            if (targetArch == "arm64") {
                if (std::filesystem::exists("/opt/homebrew/opt/openssl@3/lib")) {
                    opensslPrefix = "/opt/homebrew/opt/openssl@3";
                }
            } else {
                if (std::filesystem::exists("/usr/local/opt/openssl@3/lib")) {
                    opensslPrefix = "/usr/local/opt/openssl@3";
                }
            }
            if (opensslPrefix.empty()) {
                FILE* pipe = popen("brew --prefix openssl@3 2>/dev/null", "r");
                if (pipe) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), pipe)) {
                        opensslPrefix = buffer;
                        while (!opensslPrefix.empty() && opensslPrefix.back() == '\n')
                            opensslPrefix.pop_back();
                    }
                    pclose(pipe);
                }
            }
            if (!opensslPrefix.empty()) {
                linkCmd += " -L" + opensslPrefix + "/lib";
            }
            linkCmd += " -lssl -lcrypto";
        }
        if (needsCppRuntime)
            linkCmd += " -lc++";
        if (needsPthread)
            linkCmd += " -lpthread";
        linkCmd += " -o " + output_file;
#else
        linkCmd = "clang -Wl,--gc-sections ";
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += obj_list + runtimePath;
        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty())
                linkCmd += " " + gpuRuntimePath;
        }
        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            linkCmd += " -lssl -lcrypto";
        }
        linkCmd += " -o " + output_file;
        if (needsCppRuntime)
            linkCmd += " -lstdc++";
        if (needsPthread)
            linkCmd += " -lpthread";
#endif
    }

    if (cm::debug::debug_mode()) {
        std::cerr << "[LINK] " << linkCmd << "\n";
    }

    auto link_start = std::chrono::steady_clock::now();
    int ret = std::system(linkCmd.c_str());
    auto link_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - link_start)
                       .count();
    // emitExecutableと同じ切り分け警告（システムリンカ待ちの長時間化はcm外の要因）
    if (link_ms > 10000) {
        std::cerr << "warning: linker took " << (link_ms / 1000)
                  << "s (cm waits for the system linker; security software scanning new "
                     "binaries can cause this)\n";
    }
    if (ret != 0) {
        // R23: wasmのundefined symbolは、native専用FFIモジュール（std::env/fs・native::net/gpu/sync/thread等）を
        // wasmターゲットでコンパイルした場合に発生する（従来は--allow-undefinedで黙って通り、実行時のunknown importで破綻していた）
        if (target == BuildTarget::Wasm) {
            throw std::runtime_error(
                "リンクコマンド失敗: " + linkCmd +
                "\nhint: undefined "
                "symbolはネイティブ専用FFI（std::env/std::fs・native::net/gpu/sync/thread等）を"
                "wasmターゲットで使用した場合に発生します。これらのモジュールはwasmでは未対応です");
        }
        throw std::runtime_error("リンクコマンド失敗: " + linkCmd);
    }
}

// ランタイムライブラリのパスを検索
std::string LLVMCodeGen::findRuntimeLibrary() {
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
#ifdef CM_RUNTIME_WASM_PATH
        if (std::filesystem::exists(CM_RUNTIME_WASM_PATH)) {
            return CM_RUNTIME_WASM_PATH;
        }
#endif
        if (std::filesystem::exists("build/lib/cm_runtime_wasm.o")) {
            return "build/lib/cm_runtime_wasm.o";
        }
        return compileWasmRuntimeOnDemand();
    }

#ifdef CM_RUNTIME_PATH
    if (std::filesystem::exists(CM_RUNTIME_PATH)) {
        return CM_RUNTIME_PATH;
    }
#endif

    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/cm_runtime.o";
    }
    std::vector<std::string> searchPaths = {
        "build/lib/cm_runtime.o",
        "./build/lib/cm_runtime.o",
        "../build/lib/cm_runtime.o",
        ".tmp/cm_runtime.o",
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return compileRuntimeOnDemand();
}

// ランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/internal/codegen/llvm/native/runtime.c",
        "./src/internal/codegen/llvm/native/runtime.c",
        "../src/internal/codegen/llvm/native/runtime.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm runtime library. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime.o";

    std::string compileCmd = "clang -c " + runtimeSource + " -o " + outputPath + " -O2";
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit, "Compiling runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm runtime library");
    }

    return outputPath;
}

// WASMランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileWasmRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/internal/codegen/llvm/wasm/runtime_wasm.c",
        "./src/internal/codegen/llvm/wasm/runtime_wasm.c",
        "../src/internal/codegen/llvm/wasm/runtime_wasm.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm WASM runtime source. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::vector<std::string> clangPaths = {
        "/opt/homebrew/opt/llvm@17/bin/clang",
        "/opt/homebrew/opt/llvm/bin/clang",
        "/usr/local/opt/llvm@17/bin/clang",
        "/usr/local/opt/llvm/bin/clang",
    };

    std::string wasmClang;
    for (const auto& path : clangPaths) {
        if (std::filesystem::exists(path)) {
            wasmClang = path;
            break;
        }
    }

    if (wasmClang.empty()) {
        throw std::runtime_error(
            "Cannot find WASM-capable clang. "
            "Please install LLVM with Homebrew: brew install llvm@17");
    }

    std::string sourceDir = std::filesystem::path(runtimeSource).parent_path().string();

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime_wasm.o";

    std::string compileCmd = wasmClang + " -c " + runtimeSource + " -o " + outputPath +
                             " --target=wasm32-wasi -O2 -ffunction-sections -fdata-sections"
                             " -nostdlib -D__wasi__ -I" +
                             sourceDir;
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                            "Compiling WASM runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm WASM runtime library");
    }

    return outputPath;
}

// GPU関数の使用を検出
bool LLVMCodeGen::checkForGPUUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("gpu_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "GPU function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Net関数の使用を検出
bool LLVMCodeGen::checkForNetUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_tcp_") == 0 || name.find("cm_udp_") == 0 ||
                name.find("cm_dns_") == 0 || name.find("cm_socket_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Net function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Sync関数の使用を検出
bool LLVMCodeGen::checkForSyncUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_mutex_") == 0 || name.find("cm_rwlock_") == 0 ||
                name.find("cm_atomic_") == 0 || name.find("cm_channel_") == 0 ||
                name.find("cm_once_") == 0 || name.find("atomic_store_") == 0 ||
                name.find("atomic_load_") == 0 || name.find("atomic_fetch_") == 0 ||
                name.find("atomic_compare_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Sync function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Thread関数の使用を検出
bool LLVMCodeGen::checkForThreadUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_thread_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Thread function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// HTTP関数の使用を検出
bool LLVMCodeGen::checkForHTTPUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_http_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "HTTP function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// GPUランタイムライブラリのパスを検索
std::string LLVMCodeGen::findGPURuntimeLibrary() {
#ifdef CM_GPU_RUNTIME_PATH
    if (std::filesystem::exists(CM_GPU_RUNTIME_PATH)) {
        return CM_GPU_RUNTIME_PATH;
    }
#endif
    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/cm_gpu_runtime.o";
    }
    std::vector<std::string> searchPaths = {
        "build/lib/cm_gpu_runtime.o",
        "./build/lib/cm_gpu_runtime.o",
        "../build/lib/cm_gpu_runtime.o",
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, "GPU runtime library not found");
    return "";
}

// stdランタイムライブラリの汎用検索
std::string LLVMCodeGen::findStdRuntimeLibrary(const std::string& name) {
#ifdef CM_NET_RUNTIME_PATH
    if (name == "net" && std::filesystem::exists(CM_NET_RUNTIME_PATH)) {
        return CM_NET_RUNTIME_PATH;
    }
#endif
#ifdef CM_SYNC_RUNTIME_PATH
    if (name == "sync" && std::filesystem::exists(CM_SYNC_RUNTIME_PATH)) {
        return CM_SYNC_RUNTIME_PATH;
    }
#endif
#ifdef CM_THREAD_RUNTIME_PATH
    if (name == "thread" && std::filesystem::exists(CM_THREAD_RUNTIME_PATH)) {
        return CM_THREAD_RUNTIME_PATH;
    }
#endif
#ifdef CM_HTTP_RUNTIME_PATH
    if (name == "http" && std::filesystem::exists(CM_HTTP_RUNTIME_PATH)) {
        return CM_HTTP_RUNTIME_PATH;
    }
#endif

    std::string ext = (name == "sync") ? ".a" : ".o";
    std::string filename = "cm_" + name + "_runtime" + ext;

    // ホームディレクトリの~/.cm/lib/も検索（make install対応）
    std::string homeLib;
    if (const char* home = std::getenv("HOME")) {
        homeLib = std::string(home) + "/.cm/lib/" + filename;
    }
    std::vector<std::string> searchPaths = {
        "build/lib/" + filename,
        "./build/lib/" + filename,
        "../build/lib/" + filename,
    };
    if (!homeLib.empty())
        searchPaths.push_back(homeLib);
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, name + " runtime library not found");
    return "";
}

// インポートされた外部関数があるかチェック
bool LLVMCodeGen::checkForImports() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name != "printf" && name != "puts" && name != "malloc" && name != "free" &&
                name != "memcpy" && name != "memset" && name != "__cm_panic" &&
                name != "__cm_alloc" && name != "__cm_dealloc" && name.find("llvm.") != 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Found imported function: " + name);
                return true;
            }
        }
    }
    return false;
}

}  // namespace cm::codegen::llvm_backend
