/// @file function.cpp
/// @brief 関数本体と基本ブロックの変換

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

// 関数変換
void MIRToLLVM::convertFunction(const mir::MirFunction& func) {
    // 外部関数（extern）は宣言のみで本体を生成しない
    if (func.is_extern) {
        return;
    }

    // CompilationGuardを使用した無限ループ検出
    auto& guard = get_compilation_guard();

    // 関数のハッシュ値を計算（簡単のため名前とサイズから）
    size_t func_hash = std::hash<std::string>{}(func.name) ^ func.basic_blocks.size();

    try {
        // 関数生成の開始を記録
        ScopedFunctionGuard func_guard(func.name, func_hash);

        // デバッグ用プログレス表示
        guard.show_progress("Function", 0, func.basic_blocks.size());

        // ランタイム関数（cm_*）はスキップ
        // これらはランタイムライブラリで実装されている
        if (func.name.find("cm_print") == 0 || func.name.find("cm_println") == 0 ||
            func.name.find("cm_int_to_string") == 0 || func.name.find("cm_uint_to_string") == 0 ||
            func.name.find("cm_double_to_string") == 0 ||
            func.name.find("cm_float_to_string") == 0 || func.name.find("cm_bool_to_string") == 0 ||
            func.name.find("cm_char_to_string") == 0 || func.name.find("cm_string_concat") == 0 ||
            func.name.find("cm_file_") == 0 || func.name.find("cm_read_") == 0 ||
            func.name.find("cm_io_") == 0) {
            return;
        }

        // 本体がない関数（extern関数）は宣言のみで本体を生成しない
        if (func.basic_blocks.empty()) {
            return;
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMFunction, func.name,
                                cm::debug::Level::Debug);

        // std::cout << "[CODEGEN] Processing function: " << func.name << "\n" << std::flush;

        auto funcId = generateFunctionId(func);
        currentFunction = functions[funcId];
        currentMIRFunction = &func;
        locals.clear();
        blocks.clear();
        allocatedLocals.clear();
        asmReferencedLocals.clear();
        // NOTE: heapAllocatedLocalsはベアメタル対応のため削除（malloc不使用）

        // Bug#12修正: naked関数（ret/iret含むASM）の専用コード生成パス
        // naked関数ではalloca/load/storeを生成せず、関数引数をASMオペランドに直接マッピングする
        if (currentFunction->hasFnAttribute(llvm::Attribute::Naked)) {
            // エントリーブロック作成（ASM呼び出しのみ）
            auto entryBB = llvm::BasicBlock::Create(ctx.getContext(), "entry", currentFunction);
            builder->SetInsertPoint(entryBB);

            // ASMステートメントを探す
            for (const auto& bb : func.basic_blocks) {
                if (!bb)
                    continue;
                for (const auto& stmt : bb->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Asm)
                        continue;
                    auto& asmData = std::get<mir::MirStatement::AsmData>(stmt->data);

                    // Bug#11修正: UEFIターゲットのレジスタリマップ
                    std::string nakedAsmCode = asmData.code;
                    if (isUefiTarget) {
                        struct RegMapping {
                            std::string sysV;
                            std::string placeholder;
                            std::string win64;
                        };
                        static const std::vector<RegMapping> regMappings = {
                            {"%rdi", "@@SYSV_ARG1_64@@", "%rcx"},
                            {"%rsi", "@@SYSV_ARG2_64@@", "%rdx"},
                            {"%edi", "@@SYSV_ARG1_32@@", "%ecx"},
                            {"%esi", "@@SYSV_ARG2_32@@", "%edx"},
                            {"%di", "@@SYSV_ARG1_16@@", "%cx"},
                            {"%si", "@@SYSV_ARG2_16@@", "%dx"},
                        };
                        for (const auto& m : regMappings) {
                            size_t pos = 0;
                            while ((pos = nakedAsmCode.find(m.sysV, pos)) != std::string::npos) {
                                nakedAsmCode.replace(pos, m.sysV.size(), m.placeholder);
                                pos += m.placeholder.size();
                            }
                        }
                        for (const auto& m : regMappings) {
                            size_t pos = 0;
                            while ((pos = nakedAsmCode.find(m.placeholder, pos)) !=
                                   std::string::npos) {
                                nakedAsmCode.replace(pos, m.placeholder.size(), m.win64);
                                pos += m.win64.size();
                            }
                        }
                    }

                    // operandの有無に関わらず、$N→レジスタ名に事前置換して
                    // operandなしASMとして生成（LLVM17 naked+operand crash回避）
                    std::string finalAsmCode = nakedAsmCode;
                    if (!asmData.operands.empty()) {
                        // 呼び出し規約に基づくレジスタ名
                        std::vector<std::string> argRegs;
                        if (isUefiTarget) {
                            // Win64cc: RCX, RDX, R8, R9
                            argRegs = {"%rcx", "%rdx", "%r8", "%r9"};
                        } else {
                            // System V: RDI, RSI, RDX, RCX, R8, R9
                            argRegs = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
                        }

                        // $N → レジスタ名に置換（2段階で交差防止）
                        struct OpMapping {
                            std::string src;
                            std::string placeholder;
                            std::string reg;
                        };
                        std::vector<OpMapping> opMappings;
                        for (size_t i = 0; i < asmData.operands.size(); ++i) {
                            std::string src = "$" + std::to_string(i);
                            std::string ph = "@@NAKED_OP" + std::to_string(i) + "@@";
                            std::string reg =
                                (i < argRegs.size()) ? argRegs[i] : ("%r" + std::to_string(10 + i));
                            opMappings.push_back({src, ph, reg});
                        }
                        // Phase 1: $N → placeholder
                        for (const auto& m : opMappings) {
                            size_t pos = 0;
                            while ((pos = finalAsmCode.find(m.src, pos)) != std::string::npos) {
                                // $$N（エスケープ）はスキップ
                                if (pos > 0 && finalAsmCode[pos - 1] == '$') {
                                    pos += m.src.size();
                                    continue;
                                }
                                finalAsmCode.replace(pos, m.src.size(), m.placeholder);
                                pos += m.placeholder.size();
                            }
                        }
                        // Phase 2: placeholder → レジスタ名
                        for (const auto& m : opMappings) {
                            size_t pos = 0;
                            while ((pos = finalAsmCode.find(m.placeholder, pos)) !=
                                   std::string::npos) {
                                finalAsmCode.replace(pos, m.placeholder.size(), m.reg);
                                pos += m.reg.size();
                            }
                        }
                    }

                    // クロバー制約を生成（ASMコード内で使用されているレジスタを検出）
                    std::string constraints = "~{memory},~{dirflag},~{fpsr},~{flags}";
                    static const std::vector<std::pair<std::string, std::string>> nakedRegPatterns =
                        {
                            {"%r10", "r10"}, {"%r11", "r11"}, {"%r12", "r12"}, {"%r13", "r13"},
                            {"%r14", "r14"}, {"%r15", "r15"}, {"%rax", "rax"}, {"%rbx", "rbx"},
                            {"%rcx", "rcx"}, {"%rdx", "rdx"}, {"%rdi", "rdi"}, {"%rsi", "rsi"},
                            {"%r8", "r8"},   {"%r9", "r9"},
                        };
                    for (const auto& [pattern, clobberName] : nakedRegPatterns) {
                        if (finalAsmCode.find(pattern) != std::string::npos) {
                            constraints += ",~{" + clobberName + "}";
                        }
                    }
                    // rspが使われている場合もクロバーに追加
                    if (finalAsmCode.find("%rsp") != std::string::npos) {
                        constraints += ",~{rsp}";
                    }

                    auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                    auto* inlineAsm =
                        llvm::InlineAsm::get(asmFuncTy, finalAsmCode, constraints, true);
                    builder->CreateCall(asmFuncTy, inlineAsm);
                }
            }

            // naked関数はASM内のretで戻るため、unreachableで終了
            builder->CreateUnreachable();
            return;  // 通常のコード生成をスキップ
        }

        // asm入出力オペランドで参照される変数を事前スキャンしてasmReferencedLocalsに登録
        // これによりSSA定数伝播を防ぎ、asm結果が正しく反映される
        // BUG修正(v0.14.2): メンバ変数asmReferencedLocalsをクリアして再スキャン
        asmReferencedLocals.clear();
        for (size_t bbIdx = 0; bbIdx < func.basic_blocks.size(); ++bbIdx) {
            const auto& bb = func.basic_blocks[bbIdx];
            // DCEで削除されたブロックはスキップ
            if (!bb) {
                continue;
            }
            for (size_t stmtIdx = 0; stmtIdx < bb->statements.size(); ++stmtIdx) {
                const auto& stmt = bb->statements[stmtIdx];
                if (stmt->kind == mir::MirStatement::Asm) {
                    auto& asmData = std::get<mir::MirStatement::AsmData>(stmt->data);
                    for (const auto& operand : asmData.operands) {
                        // 定数オペランド（is_constant=true）はlocal_id=0が設定されているが
                        // 変数を参照していないためスキップ
                        if (operand.is_constant) {
                            continue;
                        }
                        // 変数を参照するオペランド（入力/出力/tied）を登録
                        asmReferencedLocals.insert(operand.local_id);
                    }
                }
            }
        }

        // Hazard #45修正: パラメータlocalsの再代入を事前スキャン
        // MIRで再代入されるパラメータはallocaが必要（cross-block domination回避）
        std::unordered_set<unsigned int> reassignedArgLocals;
        for (size_t bbIdx = 0; bbIdx < func.basic_blocks.size(); ++bbIdx) {
            const auto& bb = func.basic_blocks[bbIdx];
            if (!bb)
                continue;
            for (const auto& stmt : bb->statements) {
                if (stmt->kind == mir::MirStatement::Assign) {
                    auto& assignData = std::get<mir::MirStatement::AssignData>(stmt->data);
                    auto targetLocal = assignData.place.local;
                    // プロジェクションなし（直接代入）の場合のみ再代入と判定
                    // _1.*.0 = ... はフィールド書込みであり、_1自体の再代入ではない
                    if (assignData.place.projections.empty() &&
                        std::find(func.arg_locals.begin(), func.arg_locals.end(), targetLocal) !=
                            func.arg_locals.end()) {
                        reassignedArgLocals.insert(static_cast<unsigned int>(targetLocal));
                    }
                }
            }
        }

        // エントリーブロック作成
        auto entryBB = llvm::BasicBlock::Create(ctx.getContext(), "entry", currentFunction);
        builder->SetInsertPoint(entryBB);

        // パラメータをローカル変数にマップ
        size_t argIdx = 0;
        for (auto& arg : currentFunction->args()) {
            if (argIdx < func.arg_locals.size()) {
                auto localIdx = func.arg_locals[argIdx];
                // 構造体の値渡しパラメータの場合、allocaに格納してポインタとして使用（C ABIで16バイト以下の構造体はレジスタ渡しされる）
                if (arg.getType()->isStructTy() || arg.getType()->isArrayTy()) {
                    // 構造体・配列の値渡しパラメータの場合、allocaに格納してポインタとして使用（構造体: C ABIで16バイト以下はレジスタ渡し、配列: GEP操作にポインタが必要）
                    auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                        "arg_" + std::to_string(argIdx) + "_stack");
                    builder->CreateStore(&arg, alloca);
                    locals[localIdx] = alloca;
                    allocatedLocals.insert(localIdx);  // allocaを追跡
                } else if (arg.getType()->isPointerTy() && argIdx == 0 &&
                           localIdx < func.locals.size()) {
                    // プリミティブ型implメソッドのself引数: i8*で渡されるがローカルはプリミティブ型
                    // 例: int__abs(i8* %arg0) で selfはint型
                    auto& localType = func.locals[localIdx].type;
                    if (localType && (localType->kind == hir::TypeKind::Int ||
                                      localType->kind == hir::TypeKind::UInt ||
                                      localType->kind == hir::TypeKind::Long ||
                                      localType->kind == hir::TypeKind::ULong ||
                                      localType->kind == hir::TypeKind::Float ||
                                      localType->kind == hir::TypeKind::Double ||
                                      localType->kind == hir::TypeKind::Bool ||
                                      localType->kind == hir::TypeKind::Char)) {
                        // i8*をプリミティブ型のポインタにキャストしてload
                        auto primType = convertType(localType);
                        auto primPtrType = llvm::PointerType::get(primType, 0);
                        auto castedPtr =
                            builder->CreateBitCast(&arg, primPtrType, "prim_self_cast");
                        auto loadedVal = builder->CreateLoad(primType, castedPtr, "prim_self_load");
                        // allocaに格納
                        auto alloca = builder->CreateAlloca(primType, nullptr, "prim_self");
                        builder->CreateStore(loadedVal, alloca);
                        locals[localIdx] = alloca;
                        allocatedLocals.insert(localIdx);
                    } else if (reassignedArgLocals.count(static_cast<unsigned int>(localIdx)) > 0) {
                        // Hazard #45修正: 再代入されるポインタ引数のみallocaに格納
                        auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                            "arg_" + std::to_string(argIdx));
                        builder->CreateStore(&arg, alloca);
                        locals[localIdx] = alloca;
                        allocatedLocals.insert(localIdx);
                    } else {
                        locals[localIdx] = &arg;
                    }
                } else if (reassignedArgLocals.count(static_cast<unsigned int>(localIdx)) > 0) {
                    // Hazard #45修正: 再代入される引数のみallocaに格納
                    // cross-block参照時のdomination error回避
                    auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                        "arg_" + std::to_string(argIdx));
                    builder->CreateStore(&arg, alloca);
                    locals[localIdx] = alloca;
                    allocatedLocals.insert(localIdx);
                } else {
                    locals[localIdx] = &arg;
                }
            }
            argIdx++;
        }

        // ローカル変数のアロケーション
        for (size_t i = 0; i < func.locals.size(); ++i) {
            if (std::find(func.arg_locals.begin(), func.arg_locals.end(), i) ==
                    func.arg_locals.end() &&
                i != func.return_local) {  // 引数と戻り値以外
                // 引数以外のローカル変数
                auto& local = func.locals[i];
                if (local.type) {
                    // void型はアロケーションしない
                    if (local.type->kind == hir::TypeKind::Void) {
                        continue;
                    }

                    // asm参照変数（入力/出力/tied）はSSA形式ではなくalloca強制
                    [[maybe_unused]] bool isAsmReferenced =
                        asmReferencedLocals.count(static_cast<unsigned int>(i)) > 0;

                    // Hazard #45修正: 関数ポインタ型のallocaスキップを削除
                    // 以前はSSA形式で扱っていたが、cross-block参照でdomination errorが発生
                    // LLVM mem2regパスが自動的にSSA形式に最適化してくれる
                    // 配列へのポインタ型の一時変数もallocaを生成する（Bug#9修正）
                    // 以前はSSA形式で扱っていたが、Ref(array)の結果がSSA代入されると
                    // locals[ref_result] = locals[array] (配列alloca) となり、Copy時にCreateLoad(allocatedType=[N x T])が配列全体をloadしてしまう。
                    // これにより後続のstore先(ptr alloca=8B)にバッファオーバーフローが発生。
                    // allocaを生成してstore ptr → load ptrの正しいパスを通すことで修正。
                    // Hazard #45修正: 文字列一時変数のallocaスキップを削除
                    // cross-block参照時のdomination error回避のため常にallocaを生成

                    // 動的配列（スライス）の場合
                    if (local.type->kind == hir::TypeKind::Array &&
                        !local.type->array_size.has_value()) {
                        // スライスポインタを格納するallocaを作成
                        auto alloca = builder->CreateAlloca(ctx.getPtrType(), nullptr,
                                                            "slice_" + std::to_string(i));

                        // 要素サイズを計算
                        int64_t elemSize = 4;
                        if (local.type->element_type) {
                            auto elemKind = local.type->element_type->kind;
                            if (elemKind == hir::TypeKind::Array) {
                                // 多次元スライス: 要素はCmSlice構造体（32バイト）
                                elemSize = 32;
                            } else if (elemKind == hir::TypeKind::Long ||
                                       elemKind == hir::TypeKind::ULong ||
                                       elemKind == hir::TypeKind::Double ||
                                       elemKind == hir::TypeKind::Pointer ||
                                       elemKind == hir::TypeKind::String) {
                                elemSize = 8;
                            } else if (elemKind == hir::TypeKind::Char ||
                                       elemKind == hir::TypeKind::Bool) {
                                elemSize = 1;
                            } else if (elemKind == hir::TypeKind::Short ||
                                       elemKind == hir::TypeKind::UShort) {
                                elemSize = 2;
                            } else if (elemKind == hir::TypeKind::Union ||
                                       elemKind == hir::TypeKind::Struct) {
                                // ユニオン・構造体: blob格納のため実サイズをDataLayoutから取得（MIR側の計算と一致させる）
                                auto* elemTy = convertType(local.type->element_type);
                                elemSize = static_cast<int64_t>(
                                    module->getDataLayout().getTypeAllocSize(elemTy));
                            }
                        }

                        // cm_slice_new呼び出しでスライスを初期化
                        // std::cerr << "[MIR2LLVM]     Local " << i
                        //           << " is slice, calling cm_slice_new\n";
                        auto sliceNewFunc = declareExternalFunction("cm_slice_new");
                        auto elemSizeVal = llvm::ConstantInt::get(ctx.getI64Type(), elemSize);
                        auto initialCap = llvm::ConstantInt::get(ctx.getI64Type(), 4);
                        auto slicePtr = builder->CreateCall(sliceNewFunc, {elemSizeVal, initialCap},
                                                            "slice_ptr");
                        builder->CreateStore(slicePtr, alloca);

                        locals[i] = alloca;
                        allocatedLocals.insert(i);
                        continue;
                    }

                    // プリミティブ型へのポインタの場合、一時変数はプリミティブ型として扱う
                    // これは借用selfの値を格納するための一時変数のケース
                    // 注意: impl メソッド（関数名に__を含む）内でのみ適用
                    // また、local_0（self引数）からコピーされる一時変数のみ適用
                    // &result のような通常のアドレス取得には適用しない
                    hir::TypePtr allocType = local.type;
                    bool isPrimitiveImplMethod = (func.name.find("__") != std::string::npos);
                    // 名前が_tで始まる場合は一時変数
                    bool isTempVar =
                        (local.name.size() >= 2 && local.name[0] == '_' && local.name[1] == 't');
                    // さらに、最初の数個のローカル変数（selfのコピー先として使われる）のみに適用
                    // local_0はself引数、local_1/local_2が最初の一時変数として使われることが多い
                    bool isSelfCopyTarget = (i <= 2);
                    if (isPrimitiveImplMethod && isTempVar && isSelfCopyTarget &&
                        local.type->kind == hir::TypeKind::Pointer && local.type->element_type) {
                        auto elemKind = local.type->element_type->kind;
                        if (elemKind == hir::TypeKind::Int || elemKind == hir::TypeKind::UInt ||
                            elemKind == hir::TypeKind::Long || elemKind == hir::TypeKind::ULong ||
                            elemKind == hir::TypeKind::Float || elemKind == hir::TypeKind::Double ||
                            elemKind == hir::TypeKind::Bool || elemKind == hir::TypeKind::Char) {
                            allocType = local.type->element_type;
                        }
                    }

                    auto llvmType = convertType(allocType);

                    // ベアメタル対応: すべての配列はスタックに割り当て（ヒープ不使用）
                    // static変数はグローバル変数として作成
                    // グローバル変数はconvert()で既に作成済み
                    if (local.is_global) {
                        // グローバル変数への参照
                        auto it = globalVariables.find(local.name);
                        if (it != globalVariables.end()) {
                            locals[i] = it->second;
                            allocatedLocals.insert(i);
                        }
                    } else if (local.is_static) {
                        std::string staticKey = func.name + "_" + local.name;
                        auto it = staticVariables.find(staticKey);
                        if (it == staticVariables.end()) {
                            // 初期値を設定（デフォルトはゼロ初期化）
                            llvm::Constant* initialValue = llvm::Constant::getNullValue(llvmType);
                            auto globalVar = new llvm::GlobalVariable(
                                *module, llvmType, false, llvm::GlobalValue::InternalLinkage,
                                initialValue, staticKey);
                            staticVariables[staticKey] = globalVar;
                            locals[i] = globalVar;
                        } else {
                            locals[i] = it->second;
                        }
                        allocatedLocals.insert(i);  // グローバル変数もallocated扱い
                    } else {
                        auto alloca =
                            builder->CreateAlloca(llvmType, nullptr, "local_" + std::to_string(i));
                        locals[i] = alloca;
                        allocatedLocals.insert(i);  // allocaされた変数を記録

                        // 集約型（構造体・ユニオン・固定長配列）のallocaをゼロ初期化する（H4）。
                        // 未初期化フィールド/要素がnative/jitでスタックゴミを返し、wasm/js（ゼロ初期化）と
                        // 挙動が分裂していたのを、全バックエンドでゼロ初期化に統一する。
                        // 後続で個別に初期化されるフィールド（スライスメンバのcm_slice_new等）は
                        // このmemsetの後に上書きされるため順序上の問題はない。
                        bool zeroInitAggregate = false;
                        if (local.type) {
                            if (local.type->kind == hir::TypeKind::Struct ||
                                local.type->kind == hir::TypeKind::Union) {
                                zeroInitAggregate = true;
                            } else if (local.type->kind == hir::TypeKind::Array &&
                                       local.type->array_size.has_value()) {
                                zeroInitAggregate = true;
                            }
                        }
                        if (zeroInitAggregate) {
                            auto dataLayout = module->getDataLayout();
                            auto allocSize = dataLayout.getTypeAllocSize(llvmType);
                            builder->CreateMemSet(alloca,
                                                  llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                  allocSize, llvm::MaybeAlign());
                        }

                        // 構造体型の場合、スライスメンバーを初期化
                        if (local.type->kind == hir::TypeKind::Struct) {
                            auto structName = local.type->name;
                            auto structDefIt = structDefs.find(structName);
                            if (structDefIt != structDefs.end()) {
                                const auto* structDef = structDefIt->second;
                                auto* structLLVMType = structTypes[structName];

                                for (size_t fieldIdx = 0; fieldIdx < structDef->fields.size();
                                     ++fieldIdx) {
                                    const auto& field = structDef->fields[fieldIdx];
                                    // スライスフィールドを探す
                                    if (field.type && field.type->kind == hir::TypeKind::Array &&
                                        !field.type->array_size.has_value()) {
                                        // スライスフィールドのGEPを取得
                                        auto fieldPtr = builder->CreateStructGEP(
                                            structLLVMType, alloca, fieldIdx,
                                            "slice_field_" + field.name);

                                        // 要素サイズを計算
                                        int64_t elemSize = 4;
                                        if (field.type->element_type) {
                                            auto elemKind = field.type->element_type->kind;
                                            if (elemKind == hir::TypeKind::Long ||
                                                elemKind == hir::TypeKind::ULong ||
                                                elemKind == hir::TypeKind::Double ||
                                                elemKind == hir::TypeKind::Pointer ||
                                                elemKind == hir::TypeKind::String) {
                                                elemSize = 8;
                                            } else if (elemKind == hir::TypeKind::Char ||
                                                       elemKind == hir::TypeKind::Bool) {
                                                elemSize = 1;
                                            } else if (elemKind == hir::TypeKind::Short ||
                                                       elemKind == hir::TypeKind::UShort) {
                                                elemSize = 2;
                                            } else if (elemKind == hir::TypeKind::Struct ||
                                                       elemKind == hir::TypeKind::Union) {
                                                // 構造体・ユニオン: blob格納のため実サイズを使用
                                                auto* elemTy =
                                                    convertType(field.type->element_type);
                                                elemSize = static_cast<int64_t>(
                                                    module->getDataLayout().getTypeAllocSize(
                                                        elemTy));
                                            }
                                        }

                                        // cm_slice_new呼び出しでスライスを初期化
                                        auto sliceNewFunc = declareExternalFunction("cm_slice_new");
                                        auto elemSizeVal =
                                            llvm::ConstantInt::get(ctx.getI64Type(), elemSize);
                                        auto initialCap =
                                            llvm::ConstantInt::get(ctx.getI64Type(), 4);
                                        auto slicePtr = builder->CreateCall(
                                            sliceNewFunc, {elemSizeVal, initialCap},
                                            "slice_init_" + field.name);
                                        builder->CreateStore(slicePtr, fieldPtr);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 戻り値用のアロケーション（必要な場合）
        if (func.return_local < func.locals.size()) {
            auto& returnLocal = func.locals[func.return_local];
            if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                // main関数はC標準でi32を返すため、retval allocaもi32に強制
                auto llvmType =
                    (func.name == "main") ? ctx.getI32Type() : convertType(returnLocal.type);
                auto alloca = builder->CreateAlloca(llvmType, nullptr, "retval");
                // main関数のretvalは0で初期化（C標準: 正常終了=0）
                if (func.name == "main") {
                    builder->CreateStore(llvm::ConstantInt::get(ctx.getI32Type(), 0), alloca);
                }

                // struct型の戻り値をゼロ初期化（Tagged Union対応）
                // Result<ulong, long>等のペイロードi8[N]で部分書き込みによる
                // ゴミデータを防止。LLVM struct型全般に適用（安全で汎用的）
                if (llvmType->isStructTy()) {
                    auto dataLayout = module->getDataLayout();
                    auto allocSize = dataLayout.getTypeAllocSize(llvmType);
                    builder->CreateMemSet(alloca, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                          allocSize, llvm::MaybeAlign());
                }

                locals[func.return_local] = alloca;
                allocatedLocals.insert(func.return_local);  // allocaされた変数を記録
            }
        }

        // 到達可能性分析: エントリブロックから到達可能なブロックのみを変換
        // 到達不能ブロック（例: デフォルトの return 0）がLLVM O3でunreachable → ud2 (x86_64 SIGILL) に最適化される問題を防止
        std::unordered_set<size_t> reachableBlocks;
        {
            std::queue<size_t> worklist;
            size_t entry = func.entry_block;
            if (entry < func.basic_blocks.size() && func.basic_blocks[entry]) {
                worklist.push(entry);
                reachableBlocks.insert(entry);
            } else if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
                worklist.push(0);
                reachableBlocks.insert(0);
            }
            while (!worklist.empty()) {
                size_t current = worklist.front();
                worklist.pop();
                const auto& bb = func.basic_blocks[current];
                if (!bb)
                    continue;
                // ターミネーターの遷移先を収集
                if (bb->terminator) {
                    auto addSuccessor = [&](size_t target) {
                        if (target < func.basic_blocks.size() && func.basic_blocks[target] &&
                            reachableBlocks.insert(target).second) {
                            worklist.push(target);
                        }
                    };
                    switch (bb->terminator->kind) {
                        case mir::MirTerminator::Goto: {
                            auto& data =
                                std::get<mir::MirTerminator::GotoData>(bb->terminator->data);
                            addSuccessor(data.target);
                            break;
                        }
                        case mir::MirTerminator::SwitchInt: {
                            auto& data =
                                std::get<mir::MirTerminator::SwitchIntData>(bb->terminator->data);
                            for (auto& [_, target] : data.targets) {
                                addSuccessor(target);
                            }
                            addSuccessor(data.otherwise);
                            break;
                        }
                        case mir::MirTerminator::Call: {
                            auto& data =
                                std::get<mir::MirTerminator::CallData>(bb->terminator->data);
                            addSuccessor(data.success);
                            break;
                        }
                        case mir::MirTerminator::Return:
                            // 遷移先なし
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // 基本ブロック作成（到達可能なブロックのみ）
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロックはスキップ
            if (!func.basic_blocks[i])
                continue;
            // 到達不能ブロックはスキップ
            if (reachableBlocks.count(i) == 0)
                continue;
            auto bbName = "bb" + std::to_string(i);
            blocks[i] = llvm::BasicBlock::Create(ctx.getContext(), bbName, currentFunction);
        }

        // 最初のブロック（エントリブロック）へジャンプ
        // func.entry_blockを使用して正しいエントリポイントにジャンプ
        mir::BlockId entry = func.entry_block;
        if (entry < func.basic_blocks.size() && func.basic_blocks[entry]) {
            builder->CreateBr(blocks[entry]);
        } else if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
            // フォールバック: entry_blockが無効な場合はブロック0を使用
            builder->CreateBr(blocks[0]);
        }

        // 各ブロックを変換（CompilationGuardによる監視）
        // std::cerr << "[MIR2LLVM] Function " << func.name << " has " << func.basic_blocks.size()
        //           << " blocks\n";
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロック / 到達不能ブロックはスキップ
            if (!func.basic_blocks[i] || reachableBlocks.count(i) == 0) {
                continue;
            }

            // std::cerr << "[MIR2LLVM]   Converting block " << i << "/" << func.basic_blocks.size()
            //           << "\n";

            // プログレス表示
            guard.show_progress("Function", i + 1, func.basic_blocks.size());

            convertBasicBlock(*func.basic_blocks[i]);

            // std::cerr << "[MIR2LLVM]   Block " << i << " converted successfully\n";
        }
    } catch (const std::runtime_error& e) {
        // 無限ループエラーのハンドリング
        guard.handle_infinite_loop_error(e);
        throw;  // エラーを再スロー
    }
}

// 基本ブロック変換
void MIRToLLVM::convertBasicBlock(const mir::BasicBlock& block) {
    // std::cerr << "[MIR2LLVM]     Entering convertBasicBlock for block " << block.id << "\n";
    // std::cerr << "[MIR2LLVM]       Block has " << block.statements.size() << " statements\n";
    if (block.terminator) {
        // std::cerr << "[MIR2LLVM]       Block has terminator type "
        // << static_cast<int>(block.terminator->kind) << "\n";
    } else {
    }

    // blocksはunordered_mapなので、countで存在確認
    // std::cerr << "[MIR2LLVM]       Checking if block " << block.id << " is in blocks map...\n";
    if (blocks.count(block.id) > 0) {
        // std::cerr << "[MIR2LLVM]       Setting insert point for block " << block.id << "\n";
        builder->SetInsertPoint(blocks[block.id]);
    } else {
        // ブロックがblocks mapに存在しない（DCEで削除された可能性）
        // std::cerr << "[MIR2LLVM]       Block " << block.id << " not in blocks map, skipping\n";
        if (cm::debug::debug_mode()) {
            debug_msg("CODEGEN",
                      "Warning: BB " + std::to_string(block.id) + " not in blocks map, skipping");
        }
        return;
    }

    // CompilationGuardによるブロックレベル監視
    auto& guard = get_compilation_guard();
    std::string block_name = "BB" + std::to_string(block.id);
    ScopedBlockGuard block_guard(currentMIRFunction ? currentMIRFunction->name : "unknown",
                                 block_name);

    // ステートメント処理
    // デバッグ: main::bb0のステートメントを確認
    if (cm::debug::debug_mode() && currentMIRFunction && currentMIRFunction->name == "main" &&
        block.id == 0) {
        debug_msg("MIR",
                  "main::bb0 has " + std::to_string(block.statements.size()) + " statements");
        for (size_t j = 0; j < block.statements.size(); j++) {
            if (block.statements[j]->kind == mir::MirStatement::Assign) {
                auto& assign = std::get<mir::MirStatement::AssignData>(block.statements[j]->data);
                debug_msg("MIR", "  Statement " + std::to_string(j) + ": assign to local " +
                                     std::to_string(assign.place.local));
            }
        }
    }

    // std::cerr << "[MIR2LLVM]       Starting statement loop, total statements: "
    //           << block.statements.size() << "\n";

    for (size_t stmt_idx = 0; stmt_idx < block.statements.size(); ++stmt_idx) {
        const auto& stmt = block.statements[stmt_idx];

        // ステートメント処理開始のログ
        // std::cerr << "[MIR2LLVM]       Processing statement " << stmt_idx << "/"
        //           << block.statements.size() << " (kind=" << static_cast<int>(stmt->kind) <<
        //           ")\n";

        // 問題のある12個目のステートメントの詳細ログ
        if (currentMIRFunction && currentMIRFunction->name == "main" && stmt_idx == 11) {
            if (stmt->kind == mir::MirStatement::Assign) {
                auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                // std::cerr << "[MIR2LLVM]       Assign to local " << assign.place.local <<
                // "\n";
                if (assign.rvalue) {
                    // std::cerr << "[MIR2LLVM]       Rvalue kind: "
                    //           << static_cast<int>(assign.rvalue->kind) << "\n";
                }
            }
        }

        // 各命令の生成を記録（より詳細な情報を含める）
        std::ostringstream inst_str;
        inst_str << "stmt_kind_" << static_cast<int>(stmt->kind);

        // Assign文の場合は、左辺のローカル変数IDも含める
        if (stmt->kind == mir::MirStatement::Assign) {
            auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
            inst_str << "_L" << assign.place.local;
        }

        guard.add_instruction(inst_str.str());

        convertStatement(*stmt);

        // std::cerr << "[MIR2LLVM]       Statement " << stmt_idx << " processed successfully\n"; std::cerr << "[MIR2LLVM]       About to increment stmt_idx from " << stmt_idx << " to "
        // << (stmt_idx + 1) << "\n";
        // ループの最後の反復かチェック
        if (stmt_idx == block.statements.size() - 1) {
            // std::cerr << "[MIR2LLVM]       Exiting for loop iteration " << stmt_idx << "\n";
        }
        // std::cerr << "[MIR2LLVM]       End of for loop body for stmt_idx=" << stmt_idx <<
        // "\n";
    }

    // ターミネータ処理
    if (block.terminator) {
        // std::cerr << "[MIR2LLVM]       Terminator exists, processing terminator (kind="
        //           << static_cast<int>(block.terminator->kind) << ")\n";

        // ターミネータの生成を記録（より詳細な情報を含める）
        std::ostringstream term_str;
        term_str << "term_kind_" << static_cast<int>(block.terminator->kind);

        // Call terminatorの場合は関数名も含める
        if (block.terminator->kind == mir::MirTerminator::Call) {
            auto& callData = std::get<mir::MirTerminator::CallData>(block.terminator->data);
            if (callData.func) {
                if (callData.func->kind == mir::MirOperand::FunctionRef) {
                    // std::cerr << "[MIR2LLVM]       Call target: "
                    //           << std::get<std::string>(callData.func->data) << "\n";
                    term_str << "_" << std::get<std::string>(callData.func->data);
                } else if (callData.func->kind == mir::MirOperand::Constant) {
                    auto& constant = std::get<mir::MirConstant>(callData.func->data);
                    if (auto* name = std::get_if<std::string>(&constant.value)) {
                        // std::cerr << "[MIR2LLVM]       Call target (const): " << *name <<
                        // "\n";
                        term_str << "_" << *name;
                    }
                }
            }
            // std::cerr << "[MIR2LLVM]       Args count: " << callData.args.size() << "\n";
        }

        guard.add_instruction(term_str.str());

        // std::cerr << "[MIR2LLVM]       Calling convertTerminator()...\n";
        convertTerminator(*block.terminator);
        // std::cerr << "[MIR2LLVM]       convertTerminator() done!\n";
    } else {
        if (cm::debug::debug_mode()) {
            debug_msg("CODEGEN",
                      "ERROR: BB " + std::to_string(block.id) + " has no terminator in MIR!");
        }
    }
}

}  // namespace cm::codegen::llvm_backend
