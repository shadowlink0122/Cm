#pragma once

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::llvm_backend::optimizations {

/// ループベクトル化最適化 - SIMDインストラクションを活用した並列処理
class Vectorizer {
   public:
    /// ベクトル化設定
    struct Config {
        unsigned vectorWidth;                // デフォルトベクトル幅（SSE: 4, AVX: 8）
        bool enableSLP;                      // Superword Level Parallelism
        bool enableLoopVectorization;        // ループベクトル化
        bool enableIfConversion;             // 条件分岐のベクトル化
        unsigned maxUnrollFactor;            // 最大展開係数
        bool preferPredicatedVectorization;  // 述語付きベクトル化

        // デフォルトコンストラクタ
        Config()
            : vectorWidth(4),
              enableSLP(true),
              enableLoopVectorization(true),
              enableIfConversion(true),
              maxUnrollFactor(4),
              preferPredicatedVectorization(false) {}
    };

    explicit Vectorizer(const Config& config = Config()) : config(config) {}

    /// 関数全体のベクトル化
    bool vectorizeFunction(llvm::Function& func);

    /// 統計情報
    struct Stats {
        unsigned loopsVectorized = 0;
        unsigned slpGroupsVectorized = 0;
        unsigned reductionsVectorized = 0;
        unsigned maskedOpsGenerated = 0;
        unsigned totalSpeedup = 0;  // 推定高速化率（%）
    };

    /// 統計情報の取得
    const Stats& getStats() const { return stats; }

   private:
    Config config;
    Stats stats;

    /// 依存性解析結果
    struct DependenceInfo {
        bool hasLoopCarriedDependence = false;
        int minDependenceDistance = -1;
        std::vector<std::pair<llvm::Instruction*, llvm::Instruction*>> dependencies;
    };

    /// ベクトル化可能性の情報
    struct VectorizationInfo {
        bool canVectorize = false;
        unsigned vectorFactor = 0;
        unsigned interleaveCount = 1;
        llvm::Type* vectorType = nullptr;
        std::vector<llvm::Instruction*> uniformInsts;  // ベクトル化不要な命令
        std::vector<llvm::Instruction*> scalarInsts;   // スカラーのまま残す命令
    };

    /// ループ解析とベクトル化

    /// 簡易版ループのベクトル化
    bool vectorizeLoopSimple(llvm::Loop* loop, llvm::DominatorTree& DT);

    /// 簡易版ループ解析
    VectorizationInfo analyzeLoopSimple(llvm::Loop* loop);

    /// 依存性解析
    DependenceInfo analyzeDependencies(llvm::Loop* loop);

    /// メモリアクセスパターンの解析
    bool analyzeMemoryAccesses(llvm::Loop* loop, VectorizationInfo& info);

    /// リダクション演算の検出
    bool detectReductions(llvm::Loop* loop, VectorizationInfo& info);

    /// ベクトル化実行
    bool performVectorization(llvm::Loop* loop, const VectorizationInfo& info,
                              llvm::IRBuilder<>& builder, llvm::DominatorTree& DT);

    /// ベクトル化されたループ本体の生成
    void generateVectorBody(llvm::Loop* loop, const VectorizationInfo& info,
                            llvm::BasicBlock* vectorBody, llvm::BasicBlock* vectorHeader,
                            llvm::IRBuilder<>& builder);

    /// エピローグの生成
    void generateEpilogue(llvm::Loop* loop, llvm::BasicBlock* vectorExit,
                          llvm::BasicBlock* originalExit, unsigned vectorFactor,
                          llvm::IRBuilder<>& builder);

    /// SLPベクトル化
    bool performSLPVectorization(llvm::Function& func);
    bool trySLPVectorization(const std::vector<llvm::Instruction*>& insts);

    /// ヘルパー関数
    bool checkConsecutiveAccess(llvm::Instruction* inst);
    bool isReductionPHI(llvm::PHINode* phi);
    bool hasSideEffects(llvm::Loop* loop);
    void debugPrintStatistics() const;

    /// スカラーエピローグの生成（余りの処理）
    void generateScalarEpilogue(llvm::Loop* loop, unsigned vectorFactor, llvm::BasicBlock* epilogue,
                                llvm::IRBuilder<>& builder);

    /// SLP（Superword Level Parallelism）ベクトル化

    /// 基本ブロック内のSLPベクトル化
    bool vectorizeSLP(llvm::BasicBlock& bb);

    /// 同型命令のグループ化
    std::vector<std::vector<llvm::Instruction*>> findSLPCandidates(llvm::BasicBlock& bb);

    /// SLPツリーの構築
    struct SLPTree {
        std::vector<llvm::Instruction*> instructions;
        std::vector<std::unique_ptr<SLPTree>> children;
        unsigned cost = 0;
        bool vectorizable = false;
    };

    std::unique_ptr<SLPTree> buildSLPTree(const std::vector<llvm::Instruction*>& seeds);

    /// SLPツリーのベクトル化
    bool vectorizeSLPTree(const SLPTree& tree, llvm::IRBuilder<>& builder);

    /// ヘルパー関数

    /// ベクトル型の作成
    llvm::VectorType* getVectorType(llvm::Type* scalarType, unsigned vectorFactor);

    /// ベクトルのブロードキャスト（スカラー値を全要素に複製）
    llvm::Value* broadcastScalar(llvm::Value* scalar, unsigned vectorFactor,
                                 llvm::IRBuilder<>& builder);

    /// ベクトル要素の抽出
    llvm::Value* extractElement(llvm::Value* vector, unsigned index, llvm::IRBuilder<>& builder);

    /// ベクトル要素の挿入
    llvm::Value* insertElement(llvm::Value* vector, llvm::Value* element, unsigned index,
                               llvm::IRBuilder<>& builder);

    /// ストライドメモリアクセスのベクトル化
    llvm::Value* vectorizeStridedLoad(llvm::LoadInst* load, unsigned stride, unsigned vectorFactor,
                                      llvm::IRBuilder<>& builder);

    /// ストライドメモリ書き込みのベクトル化
    void vectorizeStridedStore(llvm::StoreInst* store, llvm::Value* vectorValue, unsigned stride,
                               llvm::IRBuilder<>& builder);

    /// マスク付きロード/ストアの生成
    llvm::Value* createMaskedLoad(llvm::Value* ptr, llvm::Value* mask, llvm::Type* type,
                                  llvm::IRBuilder<>& builder);

    void createMaskedStore(llvm::Value* value, llvm::Value* ptr, llvm::Value* mask,
                           llvm::IRBuilder<>& builder);

    /// リダクション演算のベクトル化
    llvm::Value* vectorizeReduction(llvm::BinaryOperator* op, unsigned vectorFactor,
                                    llvm::IRBuilder<>& builder);

    /// 水平加算（ベクトル要素の総和）
    llvm::Value* createHorizontalAdd(llvm::Value* vector, llvm::IRBuilder<>& builder);

    /// 水平最大値
    llvm::Value* createHorizontalMax(llvm::Value* vector, llvm::IRBuilder<>& builder);

    /// 水平最小値
    llvm::Value* createHorizontalMin(llvm::Value* vector, llvm::IRBuilder<>& builder);

    /// コスト計算
    unsigned calculateVectorizationCost(const VectorizationInfo& info);

    /// ターゲット固有の最適なベクトル幅を取得
    unsigned getTargetVectorWidth(llvm::Type* scalarType);

    /// 統計情報を出力
    void printStatistics() const;
};

}  // namespace cm::codegen::llvm_backend::optimizations