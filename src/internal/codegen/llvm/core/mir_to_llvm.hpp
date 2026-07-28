#pragma once

#include "context.hpp"
#include "internal/mir/mir_splitter.hpp"
#include "internal/mir/nodes.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::llvm_backend {

/// MIR → LLVM IR 変換器
class MIRToLLVM {
   private:
    LLVMContext& ctx;
    llvm::Module* module;
    llvm::IRBuilder<>* builder;

    // 現在の関数コンテキスト
    llvm::Function* currentFunction = nullptr;
    const mir::MirFunction* currentMIRFunction = nullptr;

    // ローカル変数マッピング
    std::unordered_map<mir::LocalId, llvm::Value*> locals;

    // allocaされた変数のセット（SSA代入で上書きされないようにする）
    std::unordered_set<mir::LocalId> allocatedLocals;

    // ASM入出力operandで参照される変数のセット（volatile store/loadの対象）
    // BUG修正(v0.14.2): 以前はallocatedLocals全体をvolatile化していたが、ASM参照変数のみに限定してコードサイズ爆発を防止
    std::unordered_set<mir::LocalId> asmReferencedLocals;

    // 基本ブロックマッピング
    std::unordered_map<mir::BlockId, llvm::BasicBlock*> blocks;

    // グローバル変数/関数マッピング
    std::unordered_map<std::string, llvm::GlobalVariable*> globals;
    std::unordered_map<std::string, llvm::Function*> functions;

    // グローバル変数マッピング（MirGlobalVar名 -> LLVM GlobalVariable）
    std::unordered_map<std::string, llvm::GlobalVariable*> globalVariables;

    // static変数マッピング（関数名_変数名 -> グローバル変数）
    std::unordered_map<std::string, llvm::GlobalVariable*> staticVariables;

    // 構造体型キャッシュ
    std::unordered_map<std::string, llvm::StructType*> structTypes;
    // typedef定義マップ（名前→解決済み型）: MirProgramから取得
    std::unordered_map<std::string, hir::TypePtr> typedefDefs;
    std::unordered_map<std::string, const mir::MirStruct*> structDefs;

    // enum型キャッシュ（Tagged Union対応）
    std::unordered_map<std::string, const mir::MirEnum*> enumDefs;
    std::unordered_map<std::string, llvm::StructType*> enumTypes;  // Tagged Union構造体型

    // インターフェース関連
    std::unordered_set<std::string> interfaceNames;
    std::unordered_map<std::string, llvm::StructType*> interfaceTypes;  // fat pointer型
    std::unordered_map<std::string, llvm::GlobalVariable*>
        vtableGlobals;  // type_interface -> vtable
    const mir::MirProgram* currentProgram = nullptr;

    // モジュール分割コンパイル時の全関数参照リスト
    // declareExternalFunctionでcurrentProgramがNULLの場合に使用
    std::vector<const mir::MirFunction*> allModuleFunctions;

    // ターゲット情報キャッシュ
    bool isWasmTarget = false;  // WASMターゲットかどうか（境界チェックで使用）
    bool isUefiTarget = false;  // UEFIターゲットかどうか（Win64 ABI適用に使用）

   public:
    /// コンストラクタ
    explicit MIRToLLVM(LLVMContext& context)
        : ctx(context), module(&context.getModule()), builder(&context.getBuilder()) {}

    /// MIRプログラム全体を変換
    void convert(const mir::MirProgram& program);

    /// モジュール単位での変換（差分コンパイル用）
    /// ModuleProgramのextern関数はdeclareのみ、自モジュール関数は完全変換
    void convert(const mir::ModuleProgram& module);

    /// 型変換（公開：関数シグネチャ生成で使用）
    llvm::Type* convertType(const hir::TypePtr& type);

    /// ポインタ演算の要素サイズをDataLayoutから計算する
    int64_t getElementAllocSize(const hir::TypePtr& type);

   private:
    /// 関数のユニークIDを生成（オーバーロード対応）
    std::string generateFunctionId(const mir::MirFunction& func);

    /// 呼び出し時の引数型から関数IDを生成
    std::string generateCallFunctionId(const std::string& baseName,
                                       const std::vector<mir::MirOperandPtr>& args);

    /// 関数シグネチャ変換
    llvm::Function* convertFunctionSignature(const mir::MirFunction& func);

    /// 関数変換
    void convertFunction(const mir::MirFunction& func);

    /// 基本ブロック変換
    void convertBasicBlock(const mir::BasicBlock& block);

    /// ステートメント変換
    void convertStatement(const mir::MirStatement& stmt);

    /// Assign文変換（statement/assign.cpp）
    void convertAssignStatement(const mir::MirStatement::AssignData& assign);

    /// インラインアセンブリ文変換（statement/asm.cpp）
    void convertAsmStatement(const mir::MirStatement::AsmData& asmData);

    /// ターミネータ変換
    void convertTerminator(const mir::MirTerminator& term);

    /// Callターミネータ変換（terminator/call.cpp）
    void convertCallTerminator(const mir::MirTerminator::CallData& callData);

    /// インターフェース/プリミティブimplのメソッド呼び出しディスパッチ（terminator/dispatch.cpp、処理した場合はtrue）
    bool generateMethodCallDispatch(const mir::MirTerminator::CallData& callData,
                                    std::vector<llvm::Value*>& args);

    /// 通常の直接/間接関数呼び出し生成（terminator/invoke.cpp）
    void generateRegularCall(const mir::MirTerminator::CallData& callData,
                             const std::string& funcName, bool isIndirectCall,
                             llvm::Value* funcPtrValue, std::vector<llvm::Value*>& args);

    /// 高階クロージャ呼び出しの環境化（C6、terminator/invoke.cpp）:
    /// __builtin_array_map/filter*_closureの可変個キャプチャ引数をスタック上のi64環境配列へ格納し、
    /// 環境からキャプチャを復元してラムダを呼ぶサンクを合成して args = [arr, size, サンク, env] へ正規化する
    void normalizeHofClosureArgs(const mir::MirTerminator::CallData& callData,
                                 const std::string& funcName, std::vector<llvm::Value*>& args);

    /// 右辺値変換
    llvm::Value* convertRvalue(const mir::MirRvalue& rvalue);

    /// オペランド変換
    llvm::Value* convertOperand(const mir::MirOperand& operand);

    /// Place変換（アドレス取得）
    llvm::Value* convertPlaceToAddress(const mir::MirPlace& place);

    /// 定数値変換
    llvm::Constant* convertConstant(const mir::MirConstant& value);

    /// 二項演算変換
    /// lhs_type/rhs_type: オペランドのHIR型（符号なしセマンティクス判定用。
    /// 比較演算のresult_typeはboolになるためオペランド型が必要）
    llvm::Value* convertBinaryOp(mir::MirBinaryOp op, llvm::Value* lhs, llvm::Value* rhs,
                                 const hir::TypePtr& result_type = nullptr,
                                 const hir::TypePtr& lhs_type = nullptr,
                                 const hir::TypePtr& rhs_type = nullptr);

    /// 単項演算変換
    llvm::Value* convertUnaryOp(mir::MirUnaryOp op, llvm::Value* operand);

    /// 論理AND演算（短絡評価付き）
    llvm::Value* convertLogicalAnd(llvm::Value* lhs, llvm::Value* rhs);

    /// 論理OR演算（短絡評価付き）
    llvm::Value* convertLogicalOr(llvm::Value* lhs, llvm::Value* rhs);

    /// フォーマット変換
    llvm::Value* convertFormatConvert(llvm::Value* value, const std::string& format_spec);

    /// 外部関数宣言
    llvm::Function* declareExternalFunction(const std::string& name);

    /// 組み込みランタイム関数宣言: print/型変換/文字列/スライス/配列系（runtime/builtins.cpp、該当しない場合はnullptr）
    llvm::Function* declareBuiltinRuntimeFunction(const std::string& name);

    /// システムランタイム関数宣言: net/atomic/channel/thread/http系（runtime/system.cpp、該当しない場合はnullptr）
    llvm::Function* declareSystemRuntimeFunction(const std::string& name);

    /// 組み込み関数呼び出し
    llvm::Value* callIntrinsic(const std::string& name, llvm::ArrayRef<llvm::Value*> args);

    /// パニック生成
    void generatePanic(const std::string& message);

    /// MIRオペランドからHIR型情報を取得
    hir::TypePtr getOperandType(const mir::MirOperand& operand);

    /// ポインタ型から指す先の型（pointee type）を取得
    llvm::Type* getPointeeType(const hir::TypePtr& ptrType);

    /// インターフェース型かどうかをチェック
    bool isInterfaceType(const std::string& typeName) const {
        return interfaceNames.count(typeName) > 0;
    }

    /// 構造体がABI上「小さい」かどうかをチェック（値渡し可能かどうか）
    /// System V ABI: 16バイト以下の構造体はレジスタで値渡し
    bool isSmallStruct(const hir::TypePtr& type) const;

    // 戻り値をsret（隠し出力ポインタ）で返すべき関数か（C14 Phase 4）。
    // 非extern・非main・戻り値が16バイト超の構造体・アドレス未取得（間接呼び出しされない）の場合true
    bool needsSretReturn(const mir::MirFunction& func);

    // アドレス取得された関数名（FunctionRefが呼び出し先以外のオペランドに現れる関数。
    // 関数ポインタ・vtable経由の間接呼び出しはsretのシグネチャ変換を追跡できないため除外する）
    std::unordered_set<std::string> addressTakenFunctions;

    // プログラム全体からアドレス取得された関数を収集する（convertの冒頭で呼ぶ）
    void collectAddressTakenFunctions(const std::vector<mir::MirFunctionPtr>& functions);
    // TypeAlias（typedef）を基底型に再帰的に解決するヘルパー
    hir::TypePtr resolveTypeAlias(const hir::TypePtr& type) const;

    /// インターフェース用のfat pointer型を取得（{i8* data, i8** vtable}）
    llvm::StructType* getInterfaceFatPtrType(const std::string& interfaceName);
    llvm::Value* createInterfaceFatPtr(llvm::Value* dataPtr, const std::string& concreteTypeName,
                                       const std::string& interfaceName);

    /// vtableを生成
    void generateVTables(const mir::MirProgram& program);

    /// インターフェースメソッド呼び出しを生成
    llvm::Value* generateInterfaceMethodCall(const std::string& interfaceName,
                                             const std::string& methodName, llvm::Value* receiver,
                                             llvm::ArrayRef<llvm::Value*> args);

    // ============================================================
    // Print/Format Helper Methods (implemented in print_codegen.cpp)
    // ============================================================

    /// print/println呼び出しを生成
    void generatePrintCall(const mir::MirTerminator::CallData& callData, bool isNewline);

    /// cm_format_string呼び出しを生成
    void generateFormatStringCall(const mir::MirTerminator::CallData& callData);

    /// cm_println_format/cm_print_format呼び出しを生成
    void generatePrintFormatCall(const mir::MirTerminator::CallData& callData, bool isNewline);

    /// 値を文字列に変換するコードを生成
    llvm::Value* generateValueToString(llvm::Value* value, const hir::TypePtr& hirType);

    /// フォーマット置換を生成（replace系関数の呼び出し）
    llvm::Value* generateFormatReplace(llvm::Value* currentStr, llvm::Value* value,
                                       const hir::TypePtr& hirType);
};

}  // namespace cm::codegen::llvm_backend