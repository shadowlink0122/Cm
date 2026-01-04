#pragma once

#include "../buffered_codegen.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

namespace cm::codegen::llvm_backend {

// LLVM IR生成用のバッファードジェネレータ
class BufferedLLVMCodeGen : public TwoPhaseCodeGenerator {
   private:
    // LLVM コンテキスト
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // 生成フェーズ
    enum Phase {
        SETUP,      // 初期設定
        GLOBALS,    // グローバル変数・定数
        FUNCTIONS,  // 関数定義
        METADATA,   // メタデータ
        FINALIZE    // 最終処理
    };

    Phase current_phase = SETUP;

   public:
    BufferedLLVMCodeGen(const std::string& module_name) {
        // LLVM環境を初期化
        context = std::make_unique<llvm::LLVMContext>();
        module = std::make_unique<llvm::Module>(module_name, *context);
        builder = std::make_unique<llvm::IRBuilder<>>(*context);

        // より大きな制限を設定（LLVM IRは冗長）
        limits.max_bytes = 500 * 1024 * 1024;  // 500MB
        limits.max_lines = 10000000;           // 1000万行
    }

    // フェーズごとに生成
    bool generate_phase(Phase phase) {
        current_phase = phase;

        switch (phase) {
            case SETUP:
                return generate_setup();
            case GLOBALS:
                return generate_globals();
            case FUNCTIONS:
                return generate_functions();
            case METADATA:
                return generate_metadata();
            case FINALIZE:
                return finalize_module();
        }
        return false;
    }

    // 全フェーズを実行
    std::string generate_all() {
        // フェーズ1: 構造を構築
        if (!generate_phase(SETUP)) {
            return "";
        }
        if (!generate_phase(GLOBALS)) {
            return "";
        }
        if (!generate_phase(FUNCTIONS)) {
            return "";
        }
        if (!generate_phase(METADATA)) {
            return "";
        }
        if (!generate_phase(FINALIZE)) {
            return "";
        }

        // フェーズ2: 文字列に変換
        return generate();
    }

   private:
    bool generate_setup() {
        std::string setup_code;
        llvm::raw_string_ostream os(setup_code);

        // ターゲット情報
        os << "; ModuleID = '" << module->getModuleIdentifier() << "'\n";
        os << "source_filename = \"" << module->getSourceFileName() << "\"\n";
        os << "target datalayout = \"" << module->getDataLayoutStr() << "\"\n";
        os << "target triple = \"" << module->getTargetTriple() << "\"\n\n";

        return add_block("Setup", os.str(), true);  // 必須
    }

    bool generate_globals() {
        std::string globals_code;
        llvm::raw_string_ostream os(globals_code);

        // グローバル変数を収集
        for (auto& global : module->globals()) {
            std::string var_str;
            llvm::raw_string_ostream var_os(var_str);
            global.print(var_os);
            os << var_os.str() << "\n";

            // サイズチェック
            if (var_str.size() > 10000) {  // 1変数が10KB超
                std::cerr << "[LLVM] 警告: 巨大なグローバル変数: " << global.getName().str()
                          << "\n";
            }
        }

        return add_block("Globals", os.str(), false);  // 非必須
    }

    bool generate_functions() {
        std::string funcs_code;
        llvm::raw_string_ostream os(funcs_code);

        size_t func_count = 0;
        size_t total_inst_count = 0;

        // 関数を収集
        for (auto& func : module->functions()) {
            if (func.isDeclaration()) {
                continue;  // 宣言のみはスキップ
            }

            func_count++;

            // 関数サイズを事前チェック
            size_t inst_count = 0;
            for (auto& bb : func) {
                inst_count += bb.size();
            }

            if (inst_count > 10000) {  // 1関数1万命令超
                std::cerr << "[LLVM] 警告: 巨大な関数: " << func.getName().str() << " ("
                          << inst_count << " instructions)\n";

                // 巨大関数は分割して処理
                if (!add_large_function(func)) {
                    return false;
                }
                continue;
            }

            // 通常サイズの関数
            std::string func_str;
            llvm::raw_string_ostream func_os(func_str);
            func.print(func_os);
            os << func_os.str() << "\n\n";

            total_inst_count += inst_count;
        }

        // 統計情報
        os << "; Total functions: " << func_count << "\n";
        os << "; Total instructions: " << total_inst_count << "\n";

        return add_block("Functions", os.str(), true);  // 必須
    }

    bool add_large_function(llvm::Function& func) {
        // 巨大関数は基本ブロックごとに分割
        std::string func_header;
        llvm::raw_string_ostream header_os(func_header);

        // 関数シグネチャ
        header_os << "define ";
        func.getReturnType()->print(header_os);
        header_os << " @" << func.getName() << "(";

        bool first = true;
        for (auto& arg : func.args()) {
            if (!first)
                header_os << ", ";
            arg.getType()->print(header_os);
            header_os << " %" << arg.getName();
            first = false;
        }
        header_os << ") {\n";

        if (!add_block(func.getName().str() + "_header", header_os.str(), true)) {
            return false;
        }

        // 基本ブロックごとに追加
        for (auto& bb : func) {
            std::string bb_str;
            llvm::raw_string_ostream bb_os(bb_str);
            bb.print(bb_os);

            if (!add_block(func.getName().str() + "_" + bb.getName().str(), bb_os.str(), true)) {
                return false;
            }
        }

        // 関数終了
        if (!add_block(func.getName().str() + "_footer", "}\n", true)) {
            return false;
        }

        return true;
    }

    bool generate_metadata() {
        std::string meta_code;
        llvm::raw_string_ostream os(meta_code);

        // メタデータを収集
        auto named_md = module->getNamedMDList();
        for (auto& md : named_md) {
            std::string md_str;
            llvm::raw_string_ostream md_os(md_str);
            md.print(md_os);
            os << md_os.str() << "\n";
        }

        return add_block("Metadata", os.str(), false);  // 非必須
    }

    bool finalize_module() {
        // 事前検証
        if (!validate_size()) {
            set_error("モジュールサイズが制限を超過する見込みです");
            return false;
        }

        // 最終チェック
        std::string summary = "; Module generation complete\n";
        summary += "; Estimated size: " + std::to_string(total_estimated_size / 1024) + " KB\n";
        summary += "; Block count: " + std::to_string(block_count()) + "\n";

        return add_block("Summary", summary, false);
    }

   public:
    // LLVMモジュールを直接取得
    llvm::Module* get_module() { return module.get(); }

    // 検証
    bool verify_module() {
        // LLVM検証パスを実行
        std::string error_str;
        llvm::raw_string_ostream error_os(error_str);

        if (llvm::verifyModule(*module, &error_os)) {
            set_error("モジュール検証失敗: " + error_os.str());
            return false;
        }
        return true;
    }
};

// 使用例
inline std::string generate_llvm_ir_buffered(
    const std::string& module_name, const std::function<void(BufferedLLVMCodeGen&)>& builder) {
    BufferedLLVMCodeGen gen(module_name);

    // ビルダー関数でモジュールを構築
    builder(gen);

    // 検証
    if (!gen.verify_module()) {
        std::cerr << "[LLVM] モジュール検証エラー: " << gen.get_error_message() << "\n";
        return "";
    }

    // 生成
    std::string result = gen.generate_all();

    if (gen.has_generation_error()) {
        std::cerr << "[LLVM] 生成エラー: " << gen.get_error_message() << "\n";
        return "";
    }

    // 統計情報
    auto& stats = gen.get_stats();
    std::cout << "[LLVM] 生成完了:\n";
    std::cout << "  行数: " << stats.total_lines << "\n";
    std::cout << "  サイズ: " << (stats.total_bytes / 1024) << " KB\n";
    std::cout << "  時間: " << stats.generation_time.count() << " ms\n";

    return result;
}

}  // namespace cm::codegen::llvm_backend