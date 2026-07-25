#pragma once

#include "loop_detector.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <llvm/Config/llvm-config.h>  // LLVM_VERSION_MAJOR
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SmallVectorMemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cm::codegen::llvm_backend {

// 安全なコード生成クラス（タイムアウト付き）
class SafeCodeGenerator {
   private:
    static constexpr size_t MAX_OUTPUT_SIZE = 100 * 1024 * 1024;  // 100MB制限
    static constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds(30);

   public:
    struct GenerationResult {
        bool success;
        std::vector<uint8_t> data;
        std::string error_message;
        std::chrono::milliseconds elapsed;
    };

    // タイムアウト秒数（CM_CODEGEN_TIMEOUT環境変数で上書き可能。
    // 大規模プロジェクトで既定30秒を超える正当なコード生成に対応し、
    // テストでは小さい値でタイムアウト経路そのものを検証できる）
    static std::chrono::seconds effectiveTimeout(std::chrono::seconds fallback) {
        if (const char* env = std::getenv("CM_CODEGEN_TIMEOUT")) {
            char* end = nullptr;
            long v = std::strtol(env, &end, 10);
            if (end != env && v >= 0) {
                return std::chrono::seconds(v);
            }
        }
        return fallback;
    }

#ifndef _WIN32
    // fork分離によるファイルへのコード生成（M6）。
    // 従来のスレッド方式はタイムアウト時にdetachし、(1)破棄済みスタック
    // （参照キャプチャのresult/buffer）へ書き込むuse-after-free、(2)LLVMの内部ループが
    // 走り続けGB級メモリを保持したまま残留する問題があった。
    // 子プロセスへ分離すればタイムアウト時はSIGKILLで確実に計算資源ごと回収できる。
    // forkはコード生成が単一スレッドで走る前提（他スレッドが保持するロックを
    // 子が引き継ぐとデッドロックしうる）。子は_exitで終了しatexit等を実行しない
    static GenerationResult generateToFileForked(llvm::Module& module,
                                                 llvm::TargetMachine* targetMachine,
#if LLVM_VERSION_MAJOR >= 18
                                                 llvm::CodeGenFileType fileType,
#elif LLVM_VERSION_MAJOR >= 10
                                                 llvm::CodeGenFileType fileType,
#else
                                                 llvm::TargetMachine::CodeGenFileType fileType,
#endif
                                                 const std::string& filename,
                                                 std::chrono::seconds timeout) {
        GenerationResult result;
        result.success = false;
        auto start_time = std::chrono::steady_clock::now();

        pid_t pid = fork();
        if (pid < 0) {
            result.error_message = "fork failed";
            return result;
        }
        if (pid == 0) {
            // 子プロセス: コード生成してファイルへ書き出し、即座に_exitする
            std::error_code EC;
            llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
            if (EC) {
                _exit(4);
            }
            llvm::legacy::PassManager pass;
            if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
                _exit(2);
            }
            pass.run(module);
            dest.flush();
            if (dest.has_error()) {
                _exit(4);
            }
            _exit(0);
        }

        // 親プロセス: デッドラインまで子の終了をポーリングし、超過時はSIGKILLで回収する
        auto deadline = start_time + timeout;
        int status = 0;
        bool finished = false;
        while (true) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                finished = true;
                break;
            }
            if (r < 0) {
                break;  // waitpid失敗（回収済み等）
            }
            if (std::chrono::steady_clock::now() > deadline) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);  // ゾンビ回収
                result.error_message =
                    "Code generation timeout after " + std::to_string(timeout.count()) + " seconds";
                break;
            }
            // 出力サイズの上限チェック（書きかけファイルを監視）
            struct stat st;
            if (::stat(filename.c_str(), &st) == 0 &&
                static_cast<size_t>(st.st_size) > MAX_OUTPUT_SIZE) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                result.error_message = "Output size exceeded " +
                                       std::to_string(MAX_OUTPUT_SIZE / (1024 * 1024)) + "MB limit";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (finished) {
            if (WIFEXITED(status)) {
                switch (WEXITSTATUS(status)) {
                    case 0:
                        result.success = true;
                        break;
                    case 2:
                        result.error_message = "Target doesn't support this file type emission";
                        break;
                    case 4:
                        result.error_message = "Cannot write output file: " + filename;
                        break;
                    default:
                        result.error_message = "Code generation subprocess failed (exit " +
                                               std::to_string(WEXITSTATUS(status)) + ")";
                        break;
                }
            } else if (WIFSIGNALED(status)) {
                result.error_message = "Code generation subprocess crashed (signal " +
                                       std::to_string(WTERMSIG(status)) + ")";
            }
        }

        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return result;
    }
#endif

    // メモリバッファへの安全なコード生成
    static GenerationResult generateToMemory(llvm::Module& module,
                                             llvm::TargetMachine* targetMachine,
#if LLVM_VERSION_MAJOR >= 18
                                             llvm::CodeGenFileType fileType,
#elif LLVM_VERSION_MAJOR >= 10
                                             llvm::CodeGenFileType fileType,
#else
                                             llvm::TargetMachine::CodeGenFileType fileType,
#endif
                                             std::chrono::seconds timeout = DEFAULT_TIMEOUT) {
        GenerationResult result;
        result.success = false;

        auto start_time = std::chrono::steady_clock::now();

        // 共有状態はheapに置き、スレッドには値キャプチャで渡す（M6）。
        // 従来は参照キャプチャのため、タイムアウトでdetachした後に呼び出し元の
        // スタックが破棄され、走り続けるスレッドがuse-after-freeを起こしていた
        struct SharedState {
            std::atomic<bool> generation_done{false};
            llvm::SmallVector<char, 0> buffer;
            GenerationResult result;
        };
        auto state = std::make_shared<SharedState>();

        // 別スレッドでコード生成を実行
        std::thread generation_thread([state, &module, targetMachine, fileType]() {
            try {
                llvm::raw_svector_ostream stream(state->buffer);
                // PassManagerを作成
                llvm::legacy::PassManager pass;

                // コード生成パスを追加
                if (targetMachine->addPassesToEmitFile(pass, stream, nullptr, fileType)) {
                    state->result.error_message = "Target doesn't support this file type emission";
                    state->generation_done.store(true);
                    return;
                }

                // コード生成を実行
                pass.run(module);

                // 結果をコピー
                state->result.data.assign(state->buffer.begin(), state->buffer.end());
                state->result.success = true;
                state->generation_done.store(true);

            } catch (const std::exception& e) {
                state->result.error_message = std::string("Code generation error: ") + e.what();
                state->generation_done.store(true);
            } catch (...) {
                state->result.error_message = "Unknown error during code generation";
                state->generation_done.store(true);
            }
        });

        // タイムアウト監視
        auto timeout_time = start_time + timeout;
        bool timed_out = false;
        while (!state->generation_done.load()) {
            if (std::chrono::steady_clock::now() > timeout_time) {
                result.error_message =
                    "Code generation timeout after " + std::to_string(timeout.count()) + " seconds";
                timed_out = true;
                break;
            }

            // サイズチェック（定期的に）
            if (state->buffer.size() > MAX_OUTPUT_SIZE) {
                result.error_message = "Output size exceeded " +
                                       std::to_string(MAX_OUTPUT_SIZE / (1024 * 1024)) + "MB limit";
                timed_out = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (timed_out) {
            // スレッドは強制終了できないためdetachする。共有状態はshared_ptrで
            // スレッド側が保持しており、use-after-freeは起きない
            // （moduleとtargetMachineは呼び出し元が生存させている前提。
            // POSIXではfork分離のgenerateToFileForkedが優先され、この経路は使われない）
            if (generation_thread.joinable()) {
                generation_thread.detach();
            }
            result.success = false;
        } else {
            if (generation_thread.joinable()) {
                generation_thread.join();
            }
            result = state->result;
        }

        auto end_time = std::chrono::steady_clock::now();
        result.elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        return result;
    }

    // 安全なオブジェクトファイル生成
    static void emitObjectFileSafe(llvm::Module& module, llvm::TargetMachine* targetMachine,
                                   const std::string& filename,
                                   std::chrono::seconds timeout = DEFAULT_TIMEOUT) {
        // UEFI/baremetalターゲットではPreCodeGenValidatorをスキップ
        // 意図的な無限ループ（halt/hang）や最小モジュールが正常なため
        std::string triple = module.getTargetTriple();
        bool isBaremetalOrUefi =
            triple.find("windows") != std::string::npos || triple.find("none") != std::string::npos;
        if (!isBaremetalOrUefi && !PreCodeGenValidator::validate(module)) {
            throw std::runtime_error(
                "Code generation aborted: infinite loop or excessive complexity detected");
        }

#if LLVM_VERSION_MAJOR >= 18
        auto fileType = llvm::CodeGenFileType::ObjectFile;
#else
        auto fileType = llvm::CGFT_ObjectFile;
#endif
        timeout = effectiveTimeout(timeout);

#ifndef _WIN32
        // POSIX: fork分離でコード生成（タイムアウト時はSIGKILLで確実に回収。M6）
        auto result = generateToFileForked(module, targetMachine, fileType, filename, timeout);
        if (!result.success && result.error_message == "fork failed") {
            result = generateToMemory(module, targetMachine, fileType, timeout);
            if (result.success) {
                writeBufferToFile(result, filename);
            }
        }
#else
        auto result = generateToMemory(module, targetMachine, fileType, timeout);
        if (result.success) {
            writeBufferToFile(result, filename);
        }
#endif

        if (!result.success) {
            throw std::runtime_error("Failed to generate object file: " + result.error_message);
        }

        // デバッグ情報
        if (result.elapsed.count() > 5000) {  // 5秒以上かかった場合
            std::cerr << "[CODEGEN] Warning: Code generation took " << result.elapsed.count()
                      << "ms\n";
        }
    }

    // メモリバッファ生成の結果をファイルへ書き込む（スレッド方式のフォールバック用）
    static void writeBufferToFile(const GenerationResult& result, const std::string& filename) {
        std::error_code EC;
        llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }
        dest.write(reinterpret_cast<const char*>(result.data.data()), result.data.size());
        dest.flush();
    }

    // 安全なアセンブリ生成
    static void emitAssemblySafe(llvm::Module& module, llvm::TargetMachine* targetMachine,
                                 const std::string& filename,
                                 std::chrono::seconds timeout = DEFAULT_TIMEOUT) {
        // UEFI/baremetalターゲットではPreCodeGenValidatorをスキップ
        std::string triple = module.getTargetTriple();
        bool isBaremetalOrUefi =
            triple.find("windows") != std::string::npos || triple.find("none") != std::string::npos;
        if (!isBaremetalOrUefi && !PreCodeGenValidator::validate(module)) {
            throw std::runtime_error(
                "Code generation aborted: infinite loop or excessive complexity detected");
        }

#if LLVM_VERSION_MAJOR >= 18
        auto fileType = llvm::CodeGenFileType::AssemblyFile;
#else
        auto fileType = llvm::CGFT_AssemblyFile;
#endif
        timeout = effectiveTimeout(timeout);

#ifndef _WIN32
        // POSIX: fork分離でコード生成（タイムアウト時はSIGKILLで確実に回収。M6）
        auto result = generateToFileForked(module, targetMachine, fileType, filename, timeout);
        if (!result.success && result.error_message == "fork failed") {
            result = generateToMemory(module, targetMachine, fileType, timeout);
            if (result.success) {
                writeBufferToFile(result, filename);
            }
        }
#else
        auto result = generateToMemory(module, targetMachine, fileType, timeout);
        if (result.success) {
            writeBufferToFile(result, filename);
        }
#endif

        if (!result.success) {
            throw std::runtime_error("Failed to generate assembly: " + result.error_message);
        }
    }

    // 生成前の複雑度チェック
    static bool checkComplexity(llvm::Module& module, size_t max_functions = 10000,
                                size_t max_instructions = 1000000) {
        size_t function_count = 0;
        size_t instruction_count = 0;

        for (auto& F : module) {
            if (!F.isDeclaration()) {
                function_count++;
                for (auto& BB : F) {
                    instruction_count += BB.size();
                }
            }
        }

        if (function_count > max_functions) {
            std::cerr << "[CODEGEN] Warning: Module has " << function_count
                      << " functions (limit: " << max_functions << ")\n";
            return false;
        }

        if (instruction_count > max_instructions) {
            std::cerr << "[CODEGEN] Warning: Module has " << instruction_count
                      << " instructions (limit: " << max_instructions << ")\n";
            return false;
        }

        return true;
    }
};

}  // namespace cm::codegen::llvm_backend