#pragma once

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <iostream>
#include <set>
#include <map>

namespace cm::codegen::llvm_backend {

// 再帰とインライン化を制限するクラス
class RecursionLimiter {
   private:
    // 関数の呼び出し関係を分析
    struct CallGraph {
        std::map<llvm::Function*, std::set<llvm::Function*>> edges;
        std::map<llvm::Function*, int> recursion_depth;

        void addEdge(llvm::Function* caller, llvm::Function* callee) {
            edges[caller].insert(callee);
        }

        // 再帰的な呼び出しを検出
        bool hasRecursion(llvm::Function* F) {
            std::set<llvm::Function*> visited;
            std::set<llvm::Function*> path;
            return detectCycle(F, visited, path);
        }

        bool detectCycle(llvm::Function* F,
                        std::set<llvm::Function*>& visited,
                        std::set<llvm::Function*>& path) {
            visited.insert(F);
            path.insert(F);

            auto it = edges.find(F);
            if (it != edges.end()) {
                for (auto* callee : it->second) {
                    if (path.count(callee)) {
                        return true;  // サイクル検出（再帰）
                    }
                    if (!visited.count(callee)) {
                        if (detectCycle(callee, visited, path)) {
                            return true;
                        }
                    }
                }
            }

            path.erase(F);
            return false;
        }
    };

   public:
    // モジュール内の再帰を分析して制限
    static void limitRecursionInModule(llvm::Module& module) {
        CallGraph graph;

        // 呼び出し関係を構築
        for (auto& F : module) {
            if (F.isDeclaration()) continue;

            for (auto& BB : F) {
                for (auto& I : BB) {
                    if (auto* call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                        if (auto* callee = call->getCalledFunction()) {
                            graph.addEdge(&F, callee);
                        }
                    }
                }
            }
        }

        // 再帰関数を検出してマーク
        std::set<llvm::Function*> recursive_functions;
        for (auto& F : module) {
            if (F.isDeclaration()) continue;

            if (graph.hasRecursion(&F)) {
                recursive_functions.insert(&F);
                std::cerr << "[RECURSION] 警告: 関数 '" << F.getName().str()
                         << "' は再帰的です\n";

                // インライン化を禁止
                F.addFnAttr(llvm::Attribute::NoInline);
                F.addFnAttr(llvm::Attribute::OptimizeNone);

                std::cerr << "[RECURSION] " << F.getName().str()
                         << " のインライン化と最適化を無効化しました\n";
            }
        }

        // クロージャとイテレータの特別処理
        for (auto& F : module) {
            if (F.isDeclaration()) continue;

            std::string name = F.getName().str();

            // クロージャやイテレータパターンを検出
            if (name.find("closure") != std::string::npos ||
                name.find("iter") != std::string::npos ||
                name.find("lambda") != std::string::npos ||
                name.find("$_") != std::string::npos) {

                // これらの関数の呼び出し深度を制限
                limitCallDepth(&F, 5);  // 最大5レベルまで

                // 大きな関数はインライン化を制限
                if (F.size() > 10) {  // 10ブロック以上
                    F.addFnAttr(llvm::Attribute::NoInline);
                    std::cerr << "[RECURSION] 大きなクロージャ/イテレータ関数 '"
                             << name << "' のインライン化を無効化\n";
                }
            }
        }
    }

    // 特定の関数の呼び出し深度を制限
    static void limitCallDepth(llvm::Function* F, int max_depth) {
        // 呼び出し命令をカウント
        int call_count = 0;
        for (auto& BB : *F) {
            for (auto& I : BB) {
                if (llvm::isa<llvm::CallInst>(I)) {
                    call_count++;
                }
            }
        }

        if (call_count > max_depth) {
            F->addFnAttr(llvm::Attribute::NoInline);
            F->addFnAttr(llvm::Attribute::OptimizeForSize);
            std::cerr << "[RECURSION] 関数 '" << F->getName().str()
                     << "' の呼び出し数が多い（" << call_count
                     << "）ため、最適化を制限\n";
        }
    }

    // インライン化のしきい値を設定
    static void setInlineThreshold(llvm::Module& module, int threshold) {
        for (auto& F : module) {
            if (F.isDeclaration()) continue;

            // 関数サイズを計算
            size_t size = 0;
            for (auto& BB : F) {
                size += BB.size();
            }

            if (size > threshold) {
                F.addFnAttr(llvm::Attribute::NoInline);
                std::cerr << "[RECURSION] 関数 '" << F.getName().str()
                         << "' のサイズが大きい（" << size
                         << " 命令）ため、インライン化を無効化\n";
            }
        }
    }

    // 最適化前にモジュールを分析して問題を防ぐ
    static void preprocessModule(llvm::Module& module, int opt_level) {
        std::cerr << "[RECURSION] モジュールの前処理を開始（最適化レベル: O"
                 << opt_level << "）\n";

        // 再帰を制限
        limitRecursionInModule(module);

        // 最適化レベルに応じたインライン制限
        if (opt_level >= 3) {
            setInlineThreshold(module, 50);  // O3では50命令以上はインライン化しない
        } else if (opt_level >= 2) {
            setInlineThreshold(module, 100);  // O2では100命令以上はインライン化しない
        }

        // 統計情報
        int noinline_count = 0;
        int total_functions = 0;
        for (auto& F : module) {
            if (F.isDeclaration()) continue;
            total_functions++;
            if (F.hasFnAttribute(llvm::Attribute::NoInline)) {
                noinline_count++;
            }
        }

        std::cerr << "[RECURSION] " << noinline_count << "/" << total_functions
                 << " 個の関数でインライン化を無効化しました\n";
    }
};

}  // namespace cm::codegen::llvm_backend