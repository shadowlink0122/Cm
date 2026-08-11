// LLVMネイティブバックエンドの出力処理
// 出力形式のディスパッチ（emit）とオブジェクト・アセンブリ・LLVM IR・ビットコード・実行可能ファイルの各出力を担う
#include "internal/codegen/llvm/native/codegen.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace cm::codegen::llvm_backend {

// 出力
void LLVMCodeGen::emit() {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmit, options.outputFile);

    switch (options.format) {
        case OutputFormat::ObjectFile:
            emitObjectFile();
            break;
        case OutputFormat::Assembly:
            emitAssembly();
            break;
        case OutputFormat::LLVMIR:
            emitLLVMIR();
            break;
        case OutputFormat::Bitcode:
            emitBitcode();
            break;
        case OutputFormat::Executable:
            emitExecutable();
            break;
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmitEnd, "Output: " + options.outputFile);
}

// オブジェクトファイル出力
void LLVMCodeGen::emitObjectFile() {
    targetManager->emitObjectFile(context->getModule(), options.outputFile);

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        std::string ldScript = options.linkerScript.empty() ? "link.ld" : options.linkerScript;
        targetManager->generateLinkerScript(ldScript);
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLinkerScript, ldScript);
    }
}

// アセンブリ出力
void LLVMCodeGen::emitAssembly() {
    targetManager->emitAssembly(context->getModule(), options.outputFile);
}

// LLVM IR出力（テキスト）
void LLVMCodeGen::emitLLVMIR() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    context->getModule().print(out, nullptr);
}

// ビットコード出力
void LLVMCodeGen::emitBitcode() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    llvm::WriteBitcodeToFile(context->getModule(), out);
}

// 実行可能ファイル生成（リンク）
void LLVMCodeGen::emitExecutable() {
    // まずオブジェクトファイル生成
    std::string objFile = options.outputFile + ".o";
    targetManager->emitObjectFile(context->getModule(), objFile);

    // 使用ライブラリの検出
    bool needsGPU = checkForGPUUsage();
    bool needsNet = checkForNetUsage();
    bool needsSync = checkForSyncUsage();
    bool needsThread = checkForThreadUsage();
    bool needsHTTP = checkForHTTPUsage();
    bool needsPthread = needsSync || needsThread;
    bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

    // リンカ呼び出し
    std::string linkCmd;

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        linkCmd = "arm-none-eabi-ld -T link.ld " + objFile + " -o " + options.outputFile;
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        linkCmd = "lld-link /subsystem:efi_application /entry:efi_main /out:" + options.outputFile +
                  " " + objFile;
    } else if (context->getTargetConfig().target == BuildTarget::Wasm) {
        std::string runtimePath = findRuntimeLibrary();
        linkCmd = "wasm-ld --entry=_start -z stack-size=1048576 " + objFile + " " + runtimePath +
                  " -o " + options.outputFile;
    } else {
        // ネイティブ：システムリンカ使用
        std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
        const bool needsSanitizerRuntime =
            options.sanitizeAddress || options.sanitizeThread || options.sanitizeMemory;
        std::string linkerDriver = "/usr/bin/clang++";
        if (needsSanitizerRuntime) {
            // サニタイザランタイムはHomebrew LLVMのclang++でリンクする（新しい順に探索）。
            // Apple CLTのランタイムはLLVM計装のバージョン記号（__asan_version_mismatch_check_v8等）を持たずリンクできない。
            // また古いcompiler-rt（LLVM 17）は新しいmacOSで初期化に失敗するため、より新しいLLVMを優先する
            linkerDriver = findSanitizerLinkDriver();
        }
        linkCmd = linkerDriver + " -mmacosx-version-min=15.0 -Wl,-dead_strip ";
#ifdef CM_DEFAULT_TARGET_ARCH
        linkCmd += "-arch " + std::string(CM_DEFAULT_TARGET_ARCH) + " ";
#endif
        if (options.sanitizeAddress) {
            linkCmd += "-fsanitize=address ";
        }
        if (options.sanitizeThread) {
            linkCmd += "-fsanitize=thread ";
        }
        if (options.sanitizeMemory) {
            linkCmd += "-fsanitize=memory ";
        }
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty()) {
                linkCmd += " " + gpuRuntimePath;
                linkCmd += " -framework Metal -framework Foundation";
            }
        }

        if (needsNet) {
            std::string netRuntimePath = findStdRuntimeLibrary("net");
            if (!netRuntimePath.empty()) {
                linkCmd += " " + netRuntimePath;
            }
        }

        if (needsSync) {
            std::string syncRuntimePath = findStdRuntimeLibrary("sync");
            if (!syncRuntimePath.empty()) {
                linkCmd += " " + syncRuntimePath;
            }
        }

        if (needsThread) {
            std::string threadRuntimePath = findStdRuntimeLibrary("thread");
            if (!threadRuntimePath.empty()) {
                linkCmd += " " + threadRuntimePath;
            }
        }

        if (needsHTTP) {
            std::string httpRuntimePath = findStdRuntimeLibrary("http");
            if (!httpRuntimePath.empty()) {
                linkCmd += " " + httpRuntimePath;
            }
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

        if (needsCppRuntime) {
            linkCmd += " -lc++";
        }

        if (needsPthread) {
            linkCmd += " -lpthread";
        }

        linkCmd += " -o " + options.outputFile;
#else
        linkCmd = "clang -Wl,--gc-sections ";
        if (options.sanitizeAddress) {
            linkCmd += "-fsanitize=address ";
        }
        if (options.sanitizeThread) {
            linkCmd += "-fsanitize=thread ";
        }
        if (options.sanitizeMemory) {
            linkCmd += "-fsanitize=memory ";
        }
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

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
        if (needsCppRuntime)
            linkCmd += " -lstdc++";
        if (needsPthread)
            linkCmd += " -lpthread";

        // LLVMが浮動小数の%をfmod等のlibmコールへ落とすためlibmをリンクする（O0では畳み込まれず残る。macOSはlibSystem同梱のため不要）
        if (!context->getTargetConfig().noStd)
            linkCmd += " -lm";

        linkCmd += " -o " + options.outputFile;
#endif
    }

    // リンカ実行
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLink, linkCmd);
    auto link_start = std::chrono::steady_clock::now();
    int result = std::system(linkCmd.c_str());
    auto link_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - link_start)
                       .count();
    // リンクはシステムリンカ（clang++/ld）の子プロセス待ちであり、cm自身は計算していない。
    // セキュリティソフトの実行ファイルスキャン等で子プロセスが長時間ブロックされると
    // cmがハングしたように見えるため、閾値超過時に切り分け用の警告を出す
    if (link_ms > 10000) {
        std::cerr << "warning: linker took " << (link_ms / 1000)
                  << "s (cm waits for the system linker; security software scanning new "
                     "binaries can cause this)\n";
    }
    if (result != 0) {
        // R23: wasmのundefined symbolは、native専用FFIモジュール（std::env/fs・native::net/gpu/sync/thread等）を
        // wasmターゲットでコンパイルした場合に発生する（従来は--allow-undefinedで黙って通り、実行時のunknown importで破綻していた）
        if (options.target == BuildTarget::Wasm) {
            throw std::runtime_error(
                "Linking failed\nhint: undefined symbols occur when native-only FFI modules "
                "(std::env/std::fs, native::net/gpu/sync/thread, etc.) are compiled for the wasm "
                "target; these modules are not available on wasm");
        }
        throw std::runtime_error("Linking failed");
    }

    // 一時ファイル削除
    std::remove(objFile.c_str());
}

}  // namespace cm::codegen::llvm_backend
