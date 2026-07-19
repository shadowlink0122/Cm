/// @file asm.cpp
/// @brief MIRインラインアセンブリ文 → LLVM IR 変換（convertStatementのAsmケースを分離）

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/core/mir_to_llvm.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

/// インラインアセンブリ文の変換本体（制約文字列の変換・オペランド再マッピングを含む）
void MIRToLLVM::convertAsmStatement(const mir::MirStatement::AsmData& asmData) {
    debug_msg("llvm_asm", "Emitting inline asm: " + asmData.code +
                              " operands=" + std::to_string(asmData.operands.size()));

    // ASMコードをそのまま使用（レジスタの自動リマップは行わない）
    // 理由: %rdi/%rsi等はABI引数参照だけでなく汎用スクラッチレジスタとしても
    // 使用されるため、一律リマップは意図しない動作を引き起こす
    // ABI引数を参照する場合は ${r:varname} 構文を使用すること
    std::string asmCodeRemapped = asmData.code;

    if (asmData.operands.empty()) {
        // オペランドなし: シンプルなasm
        auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), false);
        // Bug#7修正: 全ASMにhasSideEffects=trueとフラグクロバーを設定
        // must { __asm__("hlt"); } がループから脱出する問題を防止
        // LLVMがASM周辺の制御フローを不正に最適化しないようにする
        std::string constraints = "~{memory},~{dirflag},~{fpsr},~{flags}";
        auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, asmCodeRemapped, constraints,
                                               true  // hasSideEffects: 常にtrue
        );
        // Bug#7修正: must { __asm__() } の場合のみコンパイラバリアを挿入
        // LLVMの制御フロー最適化（hlt後のコードを到達不能と判断）を阻止
        // ハードウェアfence (mfence) ではなくコンパイラバリアを使用（UEFIベアメタル環境ではmfenceがGPFを引き起こす可能性があるため）
        if (asmData.is_must) {
            auto* barrierTy = llvm::FunctionType::get(ctx.getVoidType(), false);
            auto* barrier =
                llvm::InlineAsm::get(barrierTy, "", "~{memory},~{dirflag},~{fpsr},~{flags}", true);
            builder->CreateCall(barrierTy, barrier);
        }
        builder->CreateCall(asmFuncTy, inlineAsm);
        if (asmData.is_must) {
            auto* barrierTy = llvm::FunctionType::get(ctx.getVoidType(), false);
            auto* barrier =
                llvm::InlineAsm::get(barrierTy, "", "~{memory},~{dirflag},~{fpsr},~{flags}", true);
            builder->CreateCall(barrierTy, barrier);
        }
    } else {
        // オペランド付き: 制約文字列とオペランドを生成
        // 出力/入出力オペランドを分類
        // LLVMの制約形式: 出力制約,tied入力,純粋入力の順
        std::vector<llvm::Type*> inputTypes;
        std::vector<llvm::Value*> inputValues;
        std::vector<llvm::Value*> outputPtrs;      // 出力先ポインタ（=r用）
        std::vector<llvm::Type*> outputTypes;      // 出力オペランドの型（=r用）
        std::vector<mir::LocalId> outputLocalIds;  // 出力ローカルIDも記録
        std::string constraints;
        int outputCount = 0;  // =r の数
        int inputCount = 0;   // 入力オペランドの数（将来の拡張/デバッグ用）
        (void)inputCount;  // 現時点では読み取り不要だが、インクリメントは維持

        // AArch64ターゲット判定とオペランド型記録
        // LLVMのAArch64バックエンドがi32に対してxレジスタを割り当てる場合があるため、:w修飾子を付与して32bitレジスタ(w)を強制する
        std::string asmTriple = module->getTargetTriple();
        bool isAArch64Target = (asmTriple.find("aarch64") != std::string::npos ||
                                asmTriple.find("arm64") != std::string::npos);
        std::map<size_t, llvm::Type*> operandElemTypes;  // オペランドindex→LLVM型
        std::map<size_t, bool> operandIsMemory;          // メモリ制約かどうか

        // =m制約用: メモリ出力はポインタを入力として渡す
        std::vector<llvm::Value*> memOutputPtrs;  // =m用のポインタ（入力として渡す）
        std::vector<llvm::Type*> memOutputTypes;  // =m用の要素型（elementtype属性用）
        std::vector<mir::LocalId> memOutputLocalIds;
        std::vector<std::string> memOutputConstraints;

        // m入力制約用: ポインタを渡し、elementtype属性が必要
        std::vector<size_t> memInputIndices;  // pureInputValues内でのm制約インデックス
        std::vector<llvm::Type*> memInputTypes;  // m入力の要素型（elementtype属性用）
        // +m tied入力用: 同様にelementtype属性が必要
        std::vector<size_t> memTiedInputIndices;  // tiedInputValues内での+m制約インデックス
        std::vector<llvm::Type*> memTiedInputTypes;  // +m入力の要素型

        // 2パス方式で入力値を収集
        // 第1パス: +rのtied入力を収集（出力順）
        // 第2パス: 純粋な入力(r,m)を収集
        std::vector<llvm::Value*> tiedInputValues;
        std::vector<llvm::Type*> tiedInputTypes;
        std::vector<llvm::Value*> pureInputValues;
        std::vector<llvm::Type*> pureInputTypes;

        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            auto& operand = asmData.operands[i];

            // 定数オペランドの場合（macro/const）
            if (operand.is_constant) {
                // i,n制約: 定数値をConstantIntとして生成
                llvm::Value* constVal =
                    llvm::ConstantInt::get(ctx.getI64Type(), operand.const_value);
                pureInputValues.push_back(constVal);
                pureInputTypes.push_back(constVal->getType());
                debug_msg("llvm_asm", "[ASM] const operand: " + operand.constraint + " = " +
                                          std::to_string(operand.const_value));
                continue;
            }

            // ローカル変数を取得（localsはstd::map）
            llvm::Value* localPtr = nullptr;
            if (locals.count(operand.local_id) > 0 && locals[operand.local_id]) {
                localPtr = locals[operand.local_id];
            }

            if (!localPtr)
                continue;

            // ローカル変数の実際の型を取得
            llvm::Type* elemType = ctx.getI32Type();  // デフォルトint型
            if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(localPtr)) {
                // allocaから実際の型を取得
                elemType = allocaInst->getAllocatedType();
            } else if (currentMIRFunction && operand.local_id < currentMIRFunction->locals.size()) {
                // SSA値（ConstantInt等）の場合、MIR型情報から型を推定
                auto& localInfo = currentMIRFunction->locals[operand.local_id];
                if (localInfo.type) {
                    auto llvmType = convertType(localInfo.type);
                    if (llvmType && !llvmType->isVoidTy()) {
                        elemType = llvmType;
                    }
                }
            }
            // AArch64用: オペランドの型とメモリ制約を記録
            operandElemTypes[i] = elemType;
            operandIsMemory[i] = (operand.constraint.find('m') != std::string::npos);
            bool hasOutput = operand.constraint[0] == '=' || operand.constraint[0] == '+';
            bool isPureInput = operand.constraint[0] != '=' && operand.constraint[0] != '+';
            bool isTiedInput = operand.constraint[0] == '+';

            // ローカル変数から値をロード
            auto loadValue = [&]() -> llvm::Value* {
                if (llvm::isa<llvm::PointerType>(localPtr->getType()) ||
                    llvm::isa<llvm::AllocaInst>(localPtr)) {
                    auto* loadInst = builder->CreateLoad(elemType, localPtr);
                    if (auto* li = llvm::dyn_cast<llvm::LoadInst>(loadInst)) {
                        li->setVolatile(true);
                    }
                    return loadInst;
                }
                return localPtr;
            };

            if (isTiedInput) {
                // +r, +m: tied入力 - 先に入力値を収集
                bool isMemoryTied = (operand.constraint.find('m') != std::string::npos);
                if (isMemoryTied) {
                    // +m: ポインタを渡す（メモリ出力と同様）
                    // tiedInputValues内でのインデックスを記録（後でelementtype追加用）
                    memTiedInputIndices.push_back(tiedInputValues.size());
                    memTiedInputTypes.push_back(elemType);  // 要素型を記録
                    tiedInputValues.push_back(localPtr);
                    tiedInputTypes.push_back(localPtr->getType());
                } else {
                    // +r: 値をロードして渡す
                    auto* val = loadValue();
                    tiedInputValues.push_back(val);
                    tiedInputTypes.push_back(val->getType());
                }
            } else if (isPureInput) {
                // 制約の種類によって値の渡し方を変える
                std::string constraintType = operand.constraint;
                bool isMemoryConstraint = (constraintType.find('m') != std::string::npos);
                bool isImmediateConstraint = (constraintType.find('i') != std::string::npos ||
                                              constraintType.find('n') != std::string::npos);
                bool isGeneralConstraint = (constraintType.find('g') != std::string::npos);

                if (isMemoryConstraint) {
                    // m制約: ポインタ（アドレス）を渡す
                    // pureInputValues内でのインデックスを記録（後でelementtype追加用）
                    memInputIndices.push_back(pureInputValues.size());
                    memInputTypes.push_back(elemType);  // 要素型を記録
                    pureInputValues.push_back(localPtr);
                    pureInputTypes.push_back(localPtr->getType());
                } else if (isImmediateConstraint) {
                    // i,n制約: 即値（値をロードして渡す、LLVMが定数最適化）
                    auto* val = loadValue();
                    pureInputValues.push_back(val);
                    pureInputTypes.push_back(val->getType());
                } else if (isGeneralConstraint) {
                    // g制約: 汎用オペランド（レジスタ、メモリ、即値のいずれか）
                    // 値をロードして渡す（LLVMが最適な方法を選択）
                    auto* val = loadValue();
                    pureInputValues.push_back(val);
                    pureInputTypes.push_back(val->getType());
                } else {
                    // r制約: 値をロードして渡す
                    auto* val = loadValue();
                    pureInputValues.push_back(val);
                    pureInputTypes.push_back(val->getType());
                }
            }

            if (hasOutput) {
                // 制約の種類をチェック: =m (メモリ出力) か =r (レジスタ出力) か
                std::string constraintType = operand.constraint;
                bool isMemOutput = (constraintType.find('m') != std::string::npos);

                if (isMemOutput) {
                    // =m 制約: メモリ出力
                    // LLVMではメモリ出力はポインタを入力として渡す（void型）
                    // 出力オペランドの場合、ストア可能なポインタが必要
                    if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                        !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                        !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                        auto* alloca = builder->CreateAlloca(elemType, nullptr, "asm_mem_out");
                        localPtr = alloca;
                        allocatedLocals.insert(operand.local_id);
                    }
                    // メモリ出力として記録（後で入力に追加）
                    memOutputPtrs.push_back(localPtr);
                    memOutputTypes.push_back(elemType);  // 要素型を記録
                    memOutputLocalIds.push_back(operand.local_id);
                    // 制約文字列を記録（=m → *m として変換）
                    if (operand.constraint[0] == '=') {
                        memOutputConstraints.push_back("=*m");
                    } else {  // +m
                        memOutputConstraints.push_back("+*m");
                    }
                } else {
                    // =r 制約: レジスタ出力（従来通り）
                    if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                        !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                        !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                        auto* alloca = builder->CreateAlloca(elemType, nullptr, "asm_out_tmp");
                        if (isTiedInput && !tiedInputValues.empty()) {
                            auto* initStore = builder->CreateStore(tiedInputValues.back(), alloca);
                            initStore->setVolatile(true);
                        }
                        localPtr = alloca;
                        // localsマップを更新して、後続のcopy操作がallocaからvolatile loadで値を読み取れるようにする
                        locals[operand.local_id] = alloca;
                        allocatedLocals.insert(operand.local_id);
                    }
                    outputPtrs.push_back(localPtr);
                    outputTypes.push_back(elemType);  // 出力の型を記録
                    outputLocalIds.push_back(operand.local_id);
                    outputCount++;
                }
            }

            if (!constraints.empty())
                constraints += ",";
            constraints += operand.constraint;
        }

        // 入力値をLLVM順序で結合: 純粋入力 → tied入力 → メモリ出力
        // 制約文字列の順序と一致させる（r,... → 0,1,... → *m,...）
        for (size_t i = 0; i < pureInputValues.size(); ++i) {
            inputValues.push_back(pureInputValues[i]);
            inputTypes.push_back(pureInputTypes[i]);
            inputCount++;
        }
        for (size_t i = 0; i < tiedInputValues.size(); ++i) {
            inputValues.push_back(tiedInputValues[i]);
            inputTypes.push_back(tiedInputTypes[i]);
            inputCount++;
        }
        // メモリ出力ポインタを入力として追加（=m, +m制約用）
        for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
            inputValues.push_back(memOutputPtrs[i]);
            inputTypes.push_back(memOutputPtrs[i]->getType());
            inputCount++;
        }

        // 制約文字列をLLVM形式に変換
        // LLVMの制約形式: 出力制約,入力制約の順
        // オペランド番号はこの順序に対応するため、再マッピングが必要
        // +rは「=r」（出力）と「0」（入力をtie）に分解する
        std::string outputConstraints;
        std::string inputConstraints;

        // オペランド番号の再マッピング表（元の番号→LLVM番号）
        std::map<size_t, size_t> operandRemap;
        size_t llvmOutputIdx = 0;  // 出力オペランドのLLVMインデックス
        size_t llvmInputIdx = 0;   // 入力オペランドを数える（出力の後に来る）

        // まず出力オペランドを処理（=r のみ、=m は除外）
        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            const auto& operand = asmData.operands[i];

            if (operand.constraint[0] == '+' || operand.constraint[0] == '=') {
                // メモリ出力(=m, +m)は出力制約に含めない（入力として処理）
                bool isMemOutput = (operand.constraint.find('m') != std::string::npos);
                if (isMemOutput) {
                    // メモリ出力はスキップ（後で入力として追加）
                    continue;
                }

                // =r または +r: 出力オペランド
                operandRemap[i] = llvmOutputIdx;
                llvmOutputIdx++;

                if (operand.constraint[0] == '+') {
                    if (!outputConstraints.empty())
                        outputConstraints += ",";
                    outputConstraints += "=" + operand.constraint.substr(1);
                } else {
                    if (!outputConstraints.empty())
                        outputConstraints += ",";
                    outputConstraints += operand.constraint;
                }
            }
        }

        // 次に入力オペランドを処理
        // inputValuesの構築順序と一致させるため:
        // 1. 純粋入力（r, m等）
        // 2. tied入力（+r）
        // 3. メモリ出力（=m, +m）

        // 1. 純粋入力を先に処理
        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            const auto& operand = asmData.operands[i];

            // 純粋な入力（r, m等）のみ処理
            if (operand.constraint[0] != '+' && operand.constraint[0] != '=') {
                operandRemap[i] = llvmOutputIdx + llvmInputIdx;
                llvmInputIdx++;
                if (!inputConstraints.empty())
                    inputConstraints += ",";
                // m制約は*m（間接メモリ）に変換
                if (operand.constraint.find('m') != std::string::npos) {
                    inputConstraints += "*m";
                } else {
                    inputConstraints += operand.constraint;
                }
            }
        }

        // 2. tied入力（+r）を処理
        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            const auto& operand = asmData.operands[i];

            if (operand.constraint[0] == '+') {
                // +r: 入力としてtied（出力番号を参照）
                // ただし+mはスキップ（メモリ出力として別処理）
                if (operand.constraint.find('m') == std::string::npos) {
                    if (!inputConstraints.empty())
                        inputConstraints += ",";
                    inputConstraints += std::to_string(operandRemap[i]);
                }
            }
        }

        // 3. メモリ出力の制約を入力制約に追加（=*m形式）
        // メモリ出力のオペランド番号も再マッピング
        size_t memOutputStartIdx = llvmOutputIdx + llvmInputIdx;
        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            const auto& operand = asmData.operands[i];
            if ((operand.constraint[0] == '=' || operand.constraint[0] == '+') &&
                operand.constraint.find('m') != std::string::npos) {
                // メモリ出力のオペランド番号を設定
                operandRemap[i] = memOutputStartIdx++;
                if (!inputConstraints.empty())
                    inputConstraints += ",";
                // =m → "*m" (indirect memory), +m も同様
                inputConstraints += "*m";
            }
        }

        // 最終制約: 出力,入力の順
        std::string llvmConstraints = outputConstraints;
        if (!inputConstraints.empty()) {
            if (!llvmConstraints.empty()) {
                llvmConstraints += ",";
            }
            llvmConstraints += inputConstraints;
        }

        // clobberリストを追加（~{memory}, ~{eax}等）
        // is_must=trueの場合はデフォルトで~{memory}を追加
        if (asmData.is_must && !llvmConstraints.empty()) {
            // clobbers配列に~{memory}がなければ追加
            bool hasMemoryClobber = false;
            for (const auto& clob : asmData.clobbers) {
                if (clob == "memory" || clob == "~{memory}") {
                    hasMemoryClobber = true;
                    break;
                }
            }
            if (!hasMemoryClobber) {
                llvmConstraints += ",~{memory}";
            }
        }
        // 明示的なclobbersを追加
        for (const auto& clob : asmData.clobbers) {
            if (!llvmConstraints.empty()) {
                llvmConstraints += ",";
            }
            // ~{...}形式でなければ追加
            if (clob.substr(0, 2) == "~{") {
                llvmConstraints += clob;
            } else {
                llvmConstraints += "~{" + clob + "}";
            }
        }

        // ハードコードレジスタの自動クロバー検出
        // ASMコード内の %reg パターンを検出し、入出力オペランドでないものを
        // 自動的にクロバーとして追加する（LLVMのインライン展開時にレジスタの値が不正に再利用されることを防止）
        {
            // x86-64のレジスタ名一覧（LLVM形式）
            // 64bit → LLVM名のマッピング
            static const std::vector<std::pair<std::string, std::string>> regPatterns = {
                // 64ビットレジスタ
                {"%rax", "rax"},
                {"%rbx", "rbx"},
                {"%rcx", "rcx"},
                {"%rdx", "rdx"},
                {"%rsi", "rsi"},
                {"%rdi", "rdi"},
                {"%rbp", "rbp"},
                {"%r8", "r8"},
                {"%r9", "r9"},
                {"%r10", "r10"},
                {"%r11", "r11"},
                {"%r12", "r12"},
                {"%r13", "r13"},
                {"%r14", "r14"},
                {"%r15", "r15"},
                // 32ビットレジスタ（対応する64ビットをクロバー）
                {"%eax", "rax"},
                {"%ebx", "rbx"},
                {"%ecx", "rcx"},
                {"%edx", "rdx"},
                {"%esi", "rsi"},
                {"%edi", "rdi"},
                // 16ビットレジスタ
                {"%ax", "rax"},
                {"%bx", "rbx"},
                {"%cx", "rcx"},
                {"%dx", "rdx"},
                // 8ビットレジスタ
                {"%al", "rax"},
                {"%bl", "rbx"},
                {"%cl", "rcx"},
                {"%dl", "rdx"},
                {"%ah", "rax"},
                {"%bh", "rbx"},
                {"%ch", "rcx"},
                {"%dh", "rdx"},
            };

            std::set<std::string> detectedClobbers;
            std::string asmCode = asmCodeRemapped;

            for (const auto& [pattern, llvmName] : regPatterns) {
                size_t searchPos = 0;
                while ((searchPos = asmCode.find(pattern, searchPos)) != std::string::npos) {
                    // レジスタ名の直後が英数字やアンダースコアでないことを確認（%r12の検出で%r12bを誤検出しないように）
                    size_t afterPos = searchPos + pattern.size();
                    bool isFullMatch = true;
                    if (afterPos < asmCode.size()) {
                        char nextChar = asmCode[afterPos];
                        // %r8, %r9等の短いパターンと%r8d等の区別
                        if (std::isalnum(nextChar) || nextChar == '_') {
                            // ただし%eax等→%eaxl等は普通ないので、32bit以上のパターンは次の文字がレジスタ拡張子でなければOK
                            if (pattern.size() >= 4) {
                                // %rax, %eax等: 後ろの文字は通常ない
                                isFullMatch = false;
                            } else {
                                // %r8, %al等: %r8d のような拡張をチェック
                                isFullMatch = false;
                            }
                        }
                    }
                    if (isFullMatch) {
                        detectedClobbers.insert(llvmName);
                    }
                    searchPos += pattern.size();
                }
            }

            // オペランドとして使用されているレジスタは除外しない（LLVMは入出力オペランドと重複するクロバーを自動的に無視する）
            // 既に追加済みのクロバーとの重複チェック
            for (const auto& reg : detectedClobbers) {
                std::string clobStr = "~{" + reg + "}";
                if (llvmConstraints.find(clobStr) == std::string::npos) {
                    if (!llvmConstraints.empty()) {
                        llvmConstraints += ",";
                    }
                    llvmConstraints += clobStr;
                }
            }
        }

        // asmコード内のオペランド番号を更新
        // 2段階方式: まず一時プレースホルダーに置換、次に最終番号に置換
        // これにより$0→$1と$1→$0のような交差置換が正しく処理される
        std::string remappedCode = asmCodeRemapped;

        // 第1段階: $N を一時プレースホルダー __TMP_N__ に置換
        // ただし $$N（即値エスケープ）はスキップする
        for (int i = static_cast<int>(asmData.operands.size()) - 1; i >= 0; --i) {
            std::string oldPattern = "$" + std::to_string(i);
            std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
            size_t pos = 0;
            while ((pos = remappedCode.find(oldPattern, pos)) != std::string::npos) {
                // $$N（即値エスケープ）の場合はスキップ
                if (pos > 0 && remappedCode[pos - 1] == '$') {
                    pos++;
                    continue;
                }
                size_t afterNum = pos + oldPattern.length();
                if (afterNum < remappedCode.length() && std::isdigit(remappedCode[afterNum])) {
                    pos++;
                    continue;
                }
                remappedCode.replace(pos, oldPattern.length(), tempPattern);
                pos += tempPattern.length();
            }
        }

        // 第2段階: __TMP_N__ を最終的な$REMAP[N]に置換
        // AArch64ターゲットの場合、i32以下の型のレジスタオペランドに:w修飾子を付与
        // これによりLLVMがxレジスタではなくwレジスタ(32bit)を使用する
        for (size_t i = 0; i < asmData.operands.size(); ++i) {
            std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
            std::string newPattern;
            // AArch64 + i32以下 + 非メモリ制約の場合に :w 修飾子を付与
            bool needsWModifier = false;
            if (isAArch64Target && !operandIsMemory[i]) {
                auto typeIt = operandElemTypes.find(i);
                if (typeIt != operandElemTypes.end()) {
                    llvm::Type* opType = typeIt->second;
                    if (opType->isIntegerTy() && opType->getIntegerBitWidth() <= 32) {
                        needsWModifier = true;
                    }
                }
            }
            if (needsWModifier) {
                newPattern = "${" + std::to_string(operandRemap[i]) + ":w}";
            } else {
                newPattern = "$" + std::to_string(operandRemap[i]);
            }
            size_t pos = 0;
            while ((pos = remappedCode.find(tempPattern, pos)) != std::string::npos) {
                remappedCode.replace(pos, tempPattern.length(), newPattern);
                pos += newPattern.length();
            }
        }

        constraints = llvmConstraints;  // 変換後の制約を使用

        if (outputCount > 0) {
            // 出力がある場合: 戻り値型を設定
            llvm::Type* retType;
            if (outputCount == 1) {
                // 単一出力: 直接その型を返す
                retType = !outputTypes.empty() ? outputTypes[0] : ctx.getI32Type();
            } else {
                // 複数出力: 構造体型を返す
                retType = llvm::StructType::get(ctx.getContext(), outputTypes);
            }

            auto* asmFuncTy = llvm::FunctionType::get(retType, inputTypes, false);
            // 出力オペランドがある場合は常にsideeffect=trueにして最適化を抑制
            auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints,
                                                   true /* hasSideEffects */);
            auto* result = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

            // 出力結果を各ローカル変数にstoreする
            for (size_t i = 0; i < outputLocalIds.size() && i < outputPtrs.size(); ++i) {
                auto local_id = outputLocalIds[i];
                auto* outputPtr = outputPtrs[i];

                if (!outputPtr)
                    continue;

                llvm::Value* outputValue;
                if (outputCount == 1) {
                    // 単一出力: 結果をそのまま使用
                    outputValue = result;
                } else {
                    // 複数出力: 構造体からextractvalueで取り出す
                    outputValue = builder->CreateExtractValue(result, {static_cast<unsigned>(i)});
                }

                // 出力ポインタがある場合はvolatile store
                if (llvm::isa<llvm::PointerType>(outputPtr->getType())) {
                    auto* storeInst = builder->CreateStore(outputValue, outputPtr);
                    storeInst->setVolatile(true);
                    // allocatedLocalsに追加してvolatile loadを強制
                    allocatedLocals.insert(local_id);
                } else {
                    // 直接SSA値として格納（fallback）
                    locals[local_id] = outputValue;
                }
            }

            // m入力制約にelementtype属性を追加（*m間接メモリ用）
            if (!memInputIndices.empty()) {
                if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(result)) {
                    size_t pureInputStartArgIdx = tiedInputValues.size();
                    for (size_t i = 0; i < memInputIndices.size(); ++i) {
                        size_t pureIdx = memInputIndices[i];
                        size_t argIdx = pureInputStartArgIdx + pureIdx;
                        // オペランドの実際の型を使用
                        llvm::Type* memElemType =
                            (i < memInputTypes.size()) ? memInputTypes[i] : ctx.getI32Type();
                        auto elemTypeAttr = llvm::Attribute::get(
                            ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                        callInst->addParamAttr(argIdx, elemTypeAttr);
                    }
                }
            }
        } else if (!inputValues.empty()) {
            // 入力のみ: 戻り値なし
            auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), inputTypes, false);
            auto* inlineAsm =
                llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints, asmData.is_must);
            auto* callInst = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

            // メモリ出力ポインタ（*m制約）にelementtype属性を追加
            // LLVM 17ではindirect memory制約にはelementtype属性が必要
            if (!memOutputPtrs.empty()) {
                // メモリ出力のインデックス計算: tied入力 + pure入力 + memOutputIdx
                size_t memOutputStartArgIdx = tiedInputValues.size() + pureInputValues.size();
                for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
                    size_t argIdx = memOutputStartArgIdx + i;
                    // オペランドの実際の型を使用
                    llvm::Type* memElemType =
                        (i < memOutputTypes.size()) ? memOutputTypes[i] : ctx.getI32Type();
                    auto elemTypeAttr = llvm::Attribute::get(
                        ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                    callInst->addParamAttr(argIdx, elemTypeAttr);
                }

                // メモリ出力のローカル変数をallocatedLocalsに追加
                for (const auto& local_id : memOutputLocalIds) {
                    allocatedLocals.insert(local_id);
                }
            }

            // m入力制約にもelementtype属性を追加（*m間接メモリ用）
            if (!memInputIndices.empty()) {
                // memInputIndicesはpureInputValues内でのインデックス
                // 実際のCallInst引数インデックス: tied入力 + pureInputIndex
                size_t pureInputStartArgIdx = tiedInputValues.size();
                for (size_t i = 0; i < memInputIndices.size(); ++i) {
                    size_t pureIdx = memInputIndices[i];
                    size_t argIdx = pureInputStartArgIdx + pureIdx;
                    // オペランドの実際の型を使用
                    llvm::Type* memElemType =
                        (i < memInputTypes.size()) ? memInputTypes[i] : ctx.getI32Type();
                    auto elemTypeAttr = llvm::Attribute::get(
                        ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                    callInst->addParamAttr(argIdx, elemTypeAttr);
                }
            }

            // +m tied入力にもelementtype属性を追加
            if (!memTiedInputIndices.empty()) {
                // memTiedInputIndicesはtiedInputValues内でのインデックス
                // 実際のCallInst引数インデックス: tiedInputIndex （先頭から）
                for (size_t i = 0; i < memTiedInputIndices.size(); ++i) {
                    size_t tiedIdx = memTiedInputIndices[i];
                    // オペランドの実際の型を使用
                    llvm::Type* memElemType =
                        (i < memTiedInputTypes.size()) ? memTiedInputTypes[i] : ctx.getI32Type();
                    auto elemTypeAttr = llvm::Attribute::get(
                        ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                    callInst->addParamAttr(tiedIdx, elemTypeAttr);
                }
            }
        }
    }
}

}  // namespace cm::codegen::llvm_backend
