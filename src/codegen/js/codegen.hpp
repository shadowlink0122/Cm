#pragma once

#include "../../mir/nodes.hpp"
#include "control_flow.hpp"
#include "emitter.hpp"

#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen::js {

// JavaScript コード生成オプション
struct JSCodeGenOptions {
    std::string outputFile = "output.js";
    bool generateHTML = false;  // HTMLラッパーを生成
    bool minify = false;        // コードを圧縮
    bool sourceMap = false;     // ソースマップを生成
    bool verbose = false;       // 詳細出力
    bool useStrictMode = true;  // "use strict" を追加
    bool esModule = false;      // ES モジュール形式で出力
    int indentSpaces = 4;       // インデント幅
};

// JavaScript コードジェネレータ
class JSCodeGen {
   public:
    explicit JSCodeGen(const JSCodeGenOptions& options = {});

    // MIRプログラムからJavaScriptを生成
    void compile(const mir::MirProgram& program);

    // 生成されたJavaScriptコードを取得
    std::string getGeneratedCode() const { return emitter_.getCode(); }

   private:
    JSCodeGenOptions options_;
    JSEmitter emitter_;

    // 型情報キャッシュ
    std::unordered_map<std::string, const mir::MirStruct*> struct_map_;

    // 関数情報キャッシュ（クロージャキャプチャ用）
    std::unordered_map<std::string, const mir::MirFunction*> function_map_;

    // 生成済み関数名
    std::unordered_set<std::string> generated_functions_;

    // static変数: 関数名_変数名 -> 初期値
    std::unordered_map<std::string, std::string> static_vars_;

    // インターフェース名のセット（型のチェック用）
    std::unordered_set<std::string> interface_names_;

    // 使用されたランタイムヘルパー
    std::unordered_set<std::string> used_runtime_helpers_;
    std::unordered_set<mir::LocalId> current_used_locals_;
    std::unordered_map<mir::LocalId, size_t> current_use_counts_;
    std::unordered_set<mir::LocalId> current_noninline_locals_;
    std::unordered_set<mir::LocalId> inline_candidates_;
    std::unordered_map<mir::LocalId, std::string> inline_values_;
    std::unordered_set<mir::LocalId> declare_on_assign_;
    std::unordered_set<mir::LocalId> declared_locals_;

    // プリアンブル（ヘルパー関数など）
    void emitPreamble();
    void emitPostamble(const mir::MirProgram& program);

    // static変数の収集と出力
    void collectStaticVars(const mir::MirProgram& program);
    void emitStaticVars();

    // vtable生成（インターフェースディスパッチ用）
    void emitVTables(const mir::MirProgram& program);

    // 構造体
    void emitStruct(const mir::MirStruct& st);

    // 関数
    void emitFunction(const mir::MirFunction& func, const mir::MirProgram& program);
    void emitFunctionSignature(const mir::MirFunction& func);
    void emitFunctionBody(const mir::MirFunction& func, const mir::MirProgram& program);
    void expandRuntimeHelperDependencies(std::unordered_set<std::string>& used) const;
    void collectUsedLocals(const mir::MirFunction& func,
                           std::unordered_set<mir::LocalId>& used) const;
    void collectUsedLocalsInOperand(const mir::MirOperand& operand,
                                    std::unordered_set<mir::LocalId>& used) const;
    void collectUsedLocalsInPlace(const mir::MirPlace& place,
                                  std::unordered_set<mir::LocalId>& used) const;
    void collectUsedLocalsInRvalue(const mir::MirRvalue& rvalue,
                                   std::unordered_set<mir::LocalId>& used) const;
    void collectUsedLocalsInTerminator(const mir::MirTerminator& term,
                                       std::unordered_set<mir::LocalId>& used) const;
    bool isLocalUsed(mir::LocalId local) const;
    bool isVoidReturn(const mir::MirFunction& func) const;
    bool hasReturnLocalWrite(const mir::MirFunction& func) const;
    void collectUseCounts(const mir::MirFunction& func);
    void collectDeclareOnAssign(const mir::MirFunction& func);
    bool isInlineableRvalue(const mir::MirRvalue& rvalue) const;
    void collectInlineCandidates(const mir::MirFunction& func);
    void precomputeInlineValues(const mir::MirFunction& func);
    bool tryEmitObjectLiteralReturn(const mir::MirFunction& func);
    bool tryEmitCssReturn(const mir::MirFunction& func);

    // 基本ブロック
    void emitBasicBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                        const mir::MirProgram& program);

    // 線形フロー用の基本ブロック出力（switch/dispatchなし）
    void emitLinearBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                         const mir::MirProgram& program);
    void emitLinearTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                              const mir::MirProgram& program);

    // 構造化制御フロー用（ループ/if-else生成）
    void emitStructuredFlow(const mir::MirFunction& func, const mir::MirProgram& program,
                            const ControlFlowAnalyzer& cfAnalyzer);
    void emitLoopBlock(mir::BlockId headerBlock, const mir::MirFunction& func,
                       const mir::MirProgram& program, const ControlFlowAnalyzer& cfAnalyzer,
                       std::set<mir::BlockId>& emittedBlocks);
    void emitIfElseBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                         const mir::MirProgram& program, const ControlFlowAnalyzer& cfAnalyzer,
                         std::set<mir::BlockId>& emittedBlocks);
    void emitBlockStatements(const mir::BasicBlock& block, const mir::MirFunction& func);

    // 文
    void emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func);

    // 終端命令
    void emitTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                        const mir::MirProgram& program);

    // 式
    std::string emitRvalue(const mir::MirRvalue& rvalue, const mir::MirFunction& func);
    std::string emitOperand(const mir::MirOperand& operand, const mir::MirFunction& func);
    std::string emitOperandWithClone(const mir::MirOperand& operand, const mir::MirFunction& func);
    std::string emitLambdaRef(const std::string& funcName, const mir::MirFunction& func,
                              const std::vector<mir::LocalId>& capturedLocals);
    std::string emitPlace(const mir::MirPlace& place, const mir::MirFunction& func);
    std::string emitConstant(const mir::MirConstant& constant);

    // 型解決ヘルパー
    hir::TypePtr getPlaceType(const mir::MirPlace& place, const mir::MirFunction& func);
    hir::TypePtr getOperandType(const mir::MirOperand& operand, const mir::MirFunction& func);

    // 演算子
    std::string emitBinaryOp(mir::MirBinaryOp op);
    std::string emitUnaryOp(mir::MirUnaryOp op);

    // ヘルパー: ローカル変数名を生成（IDサフィックス付き）
    std::string getLocalVarName(const mir::MirFunction& func, mir::LocalId localId);

    // ヘルパー: static変数かどうかをチェック
    bool isStaticVar(const mir::MirFunction& func, mir::LocalId localId);

    // ヘルパー: static変数のグローバル名を取得
    std::string getStaticVarName(const mir::MirFunction& func, mir::LocalId localId);

    // ヘルパー: CSS構造体判定とフィールド名変換
    bool isCssStruct(const std::string& struct_name) const;
    std::string toKebabCase(const std::string& name) const;
    std::string formatStructFieldKey(const mir::MirStruct& st, const std::string& field_name) const;
    std::string mapExternJsName(const std::string& name) const;
    std::unordered_set<std::string> collectUsedRuntimeHelpers(const std::string& code) const;

    // アドレス取得されるローカル変数のセット（ボクシング必要）
    std::unordered_set<mir::LocalId> boxed_locals_;
};

}  // namespace cm::codegen::js
