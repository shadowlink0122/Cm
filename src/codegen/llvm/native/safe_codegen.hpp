#pragma once

#include "loop_detector.hpp"

#include <atomic>
#include <chrono>
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

        // タイムアウト制御用のatomic flag
        std::atomic<bool> should_stop{false};
        std::atomic<bool> generation_done{false};

        // メモリバッファに書き込む
        llvm::SmallVector<char, 0> buffer;
        llvm::raw_svector_ostream stream(buffer);

        // 別スレッドでコード生成を実行
        std::thread generation_thread([&]() {
            try {
                // PassManagerを作成
                llvm::legacy::PassManager pass;

                // コード生成パスを追加
                if (targetMachine->addPassesToEmitFile(pass, stream, nullptr, fileType)) {
                    result.error_message = "Target doesn't support this file type emission";
                    generation_done.store(true);
                    return;
                }

                // 定期的にshould_stopをチェックするカスタムパスを追加
                // （LLVMの内部パスに割り込むことはできないため、これは限定的な効果）

                // コード生成を実行
                pass.run(module);
                // raw_svector_ostream doesn't need explicit flush

                // 結果をコピー
                result.data.assign(buffer.begin(), buffer.end());
                result.success = true;
                generation_done.store(true);

            } catch (const std::exception& e) {
                result.error_message = std::string("Code generation error: ") + e.what();
                generation_done.store(true);
            } catch (...) {
                result.error_message = "Unknown error during code generation";
                generation_done.store(true);
            }
        });

        // タイムアウト監視
        auto timeout_time = start_time + timeout;
        while (!generation_done.load()) {
            if (std::chrono::steady_clock::now() > timeout_time) {
                should_stop.store(true);
                result.error_message =
                    "Code generation timeout after " + std::to_string(timeout.count()) + " seconds";

                // スレッドを強制終了（危険だが必要）
                // 注: これは理想的ではないが、LLVMの内部ループから
                // 抜け出す他の方法がない
                if (generation_thread.joinable()) {
                    generation_thread.detach();  // リソースリークのリスクがある
                }

                result.success = false;
                break;
            }

            // サイズチェック（定期的に）
            if (buffer.size() > MAX_OUTPUT_SIZE) {
                should_stop.store(true);
                result.error_message = "Output size exceeded " +
                                       std::to_string(MAX_OUTPUT_SIZE / (1024 * 1024)) + "MB limit";
                if (generation_thread.joinable()) {
                    generation_thread.detach();
                }
                result.success = false;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // スレッドの終了を待つ（正常終了の場合）
        if (generation_thread.joinable() && generation_done.load()) {
            generation_thread.join();
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

        auto result = generateToMemory(module, targetMachine, fileType, timeout);

        if (!result.success) {
            throw std::runtime_error("Failed to generate object file: " + result.error_message);
        }

        // メモリバッファからファイルに書き込み
        std::error_code EC;
        llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }

        dest.write(reinterpret_cast<const char*>(result.data.data()), result.data.size());
        dest.flush();

        // デバッグ情報
        if (result.elapsed.count() > 5000) {  // 5秒以上かかった場合
            std::cerr << "[CODEGEN] Warning: Code generation took " << result.elapsed.count()
                      << "ms\n";
        }
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

        auto result = generateToMemory(module, targetMachine, fileType, timeout);

        if (!result.success) {
            throw std::runtime_error("Failed to generate assembly: " + result.error_message);
        }

        // メモリバッファからファイルに書き込み
        std::error_code EC;
        llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Cannot open output file: " + filename);
        }

        dest.write(reinterpret_cast<const char*>(result.data.data()), result.data.size());
        dest.flush();
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