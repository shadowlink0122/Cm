/// @file program.cpp
/// @brief MIRプログラム全体/モジュール単位の変換エントリポイント

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

// MIRプログラム全体を変換
void MIRToLLVM::convert(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert, "Starting MIR to LLVM conversion");

    // std::cerr << "[MIR2LLVM] Starting conversion with " << program.functions.size()
    //           << " functions\n";

    currentProgram = &program;

    // アドレス取得された関数を収集（sret変換の除外判定用。C14 Phase 4）
    addressTakenFunctions.clear();
    collectAddressTakenFunctions(program.functions);

    // typedef定義マップをコピー（convertTypeでTypeAlias/Struct名の解決に使用）
    typedefDefs = program.typedef_defs;

    // ターゲット判定をキャッシュ（境界チェック・ABI設定で使用）
    std::string triple = module->getTargetTriple();
    isWasmTarget = triple.find("wasm") != std::string::npos;
    isUefiTarget = triple.find("windows") != std::string::npos;

    // インターフェース名を収集
    // std::cerr << "[MIR2LLVM] Collecting interfaces (" << program.interfaces.size() << ")...\n";
    size_t iface_count = 0;
    const size_t MAX_INTERFACES = 10000;  // 無限ループ防止
    for (const auto& iface : program.interfaces) {
        if (++iface_count > MAX_INTERFACES) {
            throw std::runtime_error("Too many interfaces in MIR program");
        }
        if (iface) {
            // std::cerr << "[MIR2LLVM]   Interface: " << iface->name << "\n";
            interfaceNames.insert(iface->name);
        }
    }
    // std::cerr << "[MIR2LLVM] Interfaces collected\n";

    // 構造体型を先に定義（2パスアプローチ）
    // パス1: 全ての構造体をopaque型として作成
    // std::cerr << "[MIR2LLVM] Pass 1: Creating struct types (" << program.structs.size() <<
    // ")...\n";
    for (const auto& structPtr : program.structs) {
        const auto& structDef = *structPtr;
        const auto& name = structDef.name;

        // 構造体定義を保存
        structDefs[name] = &structDef;

        // LLVM構造体型を作成（まずopaque型として）
        auto structType = llvm::StructType::create(ctx.getContext(), name);
        structTypes[name] = structType;
    }

    // パス2: フィールド型を設定
    // std::cerr << "[MIR2LLVM] Pass 2: Setting struct bodies...\n";
    for (const auto& structPtr : program.structs) {
        const auto& structDef = *structPtr;
        const auto& name = structDef.name;

        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : structDef.fields) {
            fieldTypes.push_back(convertType(field.type));
        }

        // 構造体のボディを設定
        auto structType = structTypes[name];
        structType->setBody(fieldTypes);
    }

    // パス3: インポートモジュールのstruct型を動的に推論・登録
    // program.structsに含まれないが、関数のメソッドとして参照されるstruct型を関数bodyのフィールドプロジェクションから推論して登録する
    {
        // 全関数のローカル型から参照されるstruct名を収集
        std::unordered_map<std::string, const mir::MirFunction*> missingStructFunctions;
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            // メソッド名パターン: StructName__method
            const auto& funcName = func->name;
            size_t lastDunder = funcName.rfind("__");
            if (lastDunder == std::string::npos || lastDunder == 0)
                continue;

            std::string structName = funcName.substr(0, lastDunder);
            // 既に登録済みならスキップ
            if (structTypes.count(structName) > 0)
                continue;
            // ジェネリック型パラメータパターン（__T__, __K__等）はスキップ
            if (structName.find("__T__") != std::string::npos ||
                structName.find("__K__") != std::string::npos ||
                structName.find("__V__") != std::string::npos)
                continue;

            // 最初のパラメータが*StructNameであることを確認
            if (func->arg_locals.size() >= 1) {
                auto firstArgLocal = func->arg_locals[0];
                if (firstArgLocal < func->locals.size()) {
                    const auto& localType = func->locals[firstArgLocal].type;
                    if (localType && localType->kind == hir::TypeKind::Pointer &&
                        localType->element_type &&
                        localType->element_type->kind == hir::TypeKind::Struct &&
                        localType->element_type->name == structName) {
                        missingStructFunctions[structName] = func.get();
                    }
                }
            }
        }

        // 欠落struct型を関数bodyのフィールドプロジェクションから推論
        for (const auto& [structName, func] : missingStructFunctions) {
            // フィールドプロジェクションを解析してフィールド数と型を推論
            // _1.*.N = copy(rhs) パターンを検出
            std::map<uint32_t, hir::TypePtr> fieldTypeMap;

            for (const auto& bb : func->basic_blocks) {
                if (!bb)
                    continue;
                for (const auto& stmt : bb->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Assign)
                        continue;
                    const auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                    const auto& place = assign.place;

                    // _1.*.N パターン（selfへのフィールド書き込み）を検出
                    if (place.projections.size() >= 2 &&
                        place.projections[0].kind == mir::ProjectionKind::Deref &&
                        place.projections[1].kind == mir::ProjectionKind::Field) {
                        uint32_t fieldId = place.projections[1].field_id;

                        // rvalueの型からフィールド型を推論
                        if (assign.rvalue && assign.rvalue->kind == mir::MirRvalue::Use) {
                            const auto& useData =
                                std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                            if (useData.operand) {
                                hir::TypePtr rhsType = nullptr;
                                if (useData.operand->type) {
                                    rhsType = useData.operand->type;
                                } else if (useData.operand->kind == mir::MirOperand::Copy ||
                                           useData.operand->kind == mir::MirOperand::Move) {
                                    const auto* rhsPlace =
                                        std::get_if<mir::MirPlace>(&useData.operand->data);
                                    if (rhsPlace && rhsPlace->local < func->locals.size()) {
                                        rhsType = func->locals[rhsPlace->local].type;
                                    }
                                } else if (useData.operand->kind == mir::MirOperand::Constant) {
                                    const auto* c =
                                        std::get_if<mir::MirConstant>(&useData.operand->data);
                                    if (c && c->type) {
                                        rhsType = c->type;
                                    }
                                }
                                if (rhsType && fieldTypeMap.find(fieldId) == fieldTypeMap.end()) {
                                    fieldTypeMap[fieldId] = rhsType;
                                }
                            }
                        }
                    }
                }
            }

            if (fieldTypeMap.empty())
                continue;

            // フィールド数を決定（最大fieldId + 1）
            uint32_t numFields = 0;
            for (const auto& [fid, _] : fieldTypeMap) {
                if (fid + 1 > numFields)
                    numFields = fid + 1;
            }

            // LLVM構造体型を作成
            std::vector<llvm::Type*> fieldTypes;
            for (uint32_t i = 0; i < numFields; ++i) {
                auto it = fieldTypeMap.find(i);
                if (it != fieldTypeMap.end() && it->second) {
                    fieldTypes.push_back(convertType(it->second));
                } else {
                    // 型が不明なフィールドはi32として扱う
                    fieldTypes.push_back(ctx.getI32Type());
                }
            }

            auto newStructType = llvm::StructType::create(ctx.getContext(), fieldTypes, structName);
            structTypes[structName] = newStructType;

            if (cm::debug::debug_mode()) {
                std::cerr << "[MIR2LLVM] 動的推論: struct " << structName
                          << " (フィールド数: " << numFields << ")" << std::endl;
            }
        }
    }

    // enum型を処理（Tagged Unionは構造体として生成）
    for (const auto& enumPtr : program.enums) {
        if (!enumPtr)
            continue;
        const auto& enumDef = *enumPtr;
        enumDefs[enumDef.name] = &enumDef;

        // Tagged Unionの場合は構造体型を生成
        if (enumDef.is_tagged_union()) {
            // Tagged Union: { i32 tag, i8[N] payload }
            // Nは最大ペイロードサイズ
            uint32_t maxPayloadSize = enumDef.max_payload_size();
            if (maxPayloadSize == 0)
                maxPayloadSize = 8;  // 最低8バイト

            std::vector<llvm::Type*> enumFieldTypes;
            enumFieldTypes.push_back(ctx.getI32Type());  // tag
            enumFieldTypes.push_back(
                llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize));  // payload

            auto enumStructType = llvm::StructType::create(ctx.getContext(), enumFieldTypes,
                                                           enumDef.name + "_tagged");
            enumTypes[enumDef.name] = enumStructType;
        }
        // シンプルなenumはi32として扱う（enumTypes追加なし）
    }

    // インターフェース型（fat pointer）を定義
    // std::cerr << "[MIR2LLVM] Creating interface fat pointer types...\n";
    for (const auto& iface : program.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // グローバル変数を生成
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;

        auto llvmType = convertType(gv->type);
        if (!llvmType)
            continue;

        // リンケージの決定
        auto linkage =
            gv->is_export ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;

        // 初期値の決定
        llvm::Constant* initialValue = nullptr;
        if (gv->init_value) {
            // 文字列型の場合: IRBuilderなしでグローバル文字列定数を作成
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                // 文字列データをグローバル定数として配置
                auto strConstant = llvm::ConstantDataArray::getString(ctx.getContext(), str, true);
                auto strGlobal = new llvm::GlobalVariable(*module, strConstant->getType(), true,
                                                          llvm::GlobalValue::PrivateLinkage,
                                                          strConstant, gv->name + ".str");
                strGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                // i8* へのポインタを取得
                initialValue = llvm::ConstantExpr::getBitCast(
                    llvm::ConstantExpr::getInBoundsGetElementPtr(
                        strConstant->getType(), strGlobal,
                        llvm::ArrayRef<llvm::Constant*>{
                            llvm::ConstantInt::get(ctx.getI64Type(), 0),
                            llvm::ConstantInt::get(ctx.getI64Type(), 0)}),
                    ctx.getPtrType());
            }
            // 整数型の場合
            else if (std::holds_alternative<int64_t>(gv->init_value->value)) {
                auto val = std::get<int64_t>(gv->init_value->value);
                initialValue = llvm::ConstantInt::get(llvmType, val);
            }
            // 浮動小数点型の場合
            else if (std::holds_alternative<double>(gv->init_value->value)) {
                auto val = std::get<double>(gv->init_value->value);
                initialValue = llvm::ConstantFP::get(llvmType, val);
            }
            // bool型の場合
            else if (std::holds_alternative<bool>(gv->init_value->value)) {
                auto val = std::get<bool>(gv->init_value->value);
                initialValue = llvm::ConstantInt::get(llvmType, val ? 1 : 0);
            }
        }

        // 初期値が設定されなかった場合はゼロ初期化
        if (!initialValue) {
            initialValue = llvm::Constant::getNullValue(llvmType);
        }

        // LLVM GlobalVariableを作成
        auto globalVar = new llvm::GlobalVariable(*module, llvmType, gv->is_const, linkage,
                                                  initialValue, gv->name);

        // グローバル変数マップに登録
        globalVariables[gv->name] = globalVar;
    }
    // 重複した関数はスキップ
    // std::cerr << "[MIR2LLVM] Declaring function signatures...\n";
    std::set<std::string> declaredFunctions;
    for (const auto& func : program.functions) {
        auto funcId = generateFunctionId(*func);
        // 既に宣言済みの場合はスキップ（重複を防ぐ）
        if (declaredFunctions.count(funcId) > 0) {
            continue;
        }
        declaredFunctions.insert(funcId);

        auto llvmFunc = convertFunctionSignature(*func);
        functions[funcId] = llvmFunc;
    }

    // vtableを生成（関数宣言後に実行）
    // std::cerr << "[MIR2LLVM] Generating vtables...\n";
    generateVTables(program);

    // === Dead Function Elimination (DFE) ===
    // エントリポイントから到達可能な関数のみをLLVM IRに変換する
    // これにより、未使用のimport関数のIR生成をスキップしてメモリ消費を削減
    std::unordered_set<std::string> reachableFunctions;
    {
        // 関数名→インデックスのマップを構築
        std::unordered_map<std::string, size_t> funcNameToIndex;
        for (size_t i = 0; i < program.functions.size(); ++i) {
            if (program.functions[i]) {
                funcNameToIndex[program.functions[i]->name] = i;
            }
        }

        // 各関数の呼び出し先を収集
        auto collectCallees = [](const mir::MirFunction& func) -> std::vector<std::string> {
            std::vector<std::string> callees;
            for (const auto& block : func.basic_blocks) {
                if (!block || !block->terminator)
                    continue;
                if (block->terminator->kind == mir::MirTerminator::Call) {
                    auto& callData =
                        std::get<mir::MirTerminator::CallData>(block->terminator->data);
                    if (callData.func && callData.func->kind == mir::MirOperand::FunctionRef) {
                        auto& funcName = std::get<std::string>(callData.func->data);
                        callees.push_back(funcName);
                    }
                }
                // Statement内のFunctionRef（関数ポインタ）も収集
                for (const auto& stmt : block->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Assign)
                        continue;
                    auto& assignData = std::get<mir::MirStatement::AssignData>(stmt->data);
                    if (!assignData.rvalue)
                        continue;
                    if (assignData.rvalue->kind == mir::MirRvalue::Use) {
                        auto& useData = std::get<mir::MirRvalue::UseData>(assignData.rvalue->data);
                        if (useData.operand &&
                            useData.operand->kind == mir::MirOperand::FunctionRef) {
                            auto& funcName = std::get<std::string>(useData.operand->data);
                            callees.push_back(funcName);
                        }
                    }
                }
            }
            return callees;
        };

        // BFS: エントリポイントから到達可能な関数を収集
        std::queue<std::string> worklist;

        // エントリポイント: export関数、main、extern関数、#[test]関数
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            // #[test] 関数はJITテストランナーが直接呼び出すため到達可能扱いにする
            bool is_test_fn = false;
            for (const auto& attr : func->attributes) {
                if (attr == "test") {
                    is_test_fn = true;
                    break;
                }
            }
            if (func->is_export || func->is_extern || func->name == "main" ||
                func->name == "_start" || func->name == "start_kernel" ||
                func->name.find("__lambda_") == 0 || is_test_fn) {
                if (reachableFunctions.insert(func->name).second) {
                    worklist.push(func->name);
                }
            }
        }

        // メソッド関数（TypeName__method パターン）のベース型がreachableなら追加
        // vtableエントリからも到達可能関数を追加
        for (const auto& vt : program.vtables) {
            if (!vt)
                continue;
            for (const auto& entry : vt->entries) {
                if (reachableFunctions.insert(entry.impl_function_name).second) {
                    worklist.push(entry.impl_function_name);
                }
            }
        }

        // BFS実行
        while (!worklist.empty()) {
            std::string current = worklist.front();
            worklist.pop();

            auto it = funcNameToIndex.find(current);
            if (it != funcNameToIndex.end()) {
                auto callees = collectCallees(*program.functions[it->second]);
                for (const auto& callee : callees) {
                    if (reachableFunctions.insert(callee).second) {
                        worklist.push(callee);
                    }
                }
            }

            // interface dispatch関数（InterfaceName__method）の場合、対応するvtableのimpl関数も到達可能に追加
            // 例: Printable__print が呼ばれる場合、Point__print等を追加
            size_t dunder = current.rfind("__");
            if (dunder != std::string::npos && dunder > 0) {
                std::string possibleInterface = current.substr(0, dunder);
                if (interfaceNames.count(possibleInterface) > 0) {
                    // このインターフェースのvtableからimpl関数を追加
                    for (const auto& vt : program.vtables) {
                        if (!vt)
                            continue;
                        if (vt->interface_name == possibleInterface) {
                            for (const auto& entry : vt->entries) {
                                if (reachableFunctions.insert(entry.impl_function_name).second) {
                                    worklist.push(entry.impl_function_name);
                                }
                            }
                        }
                    }
                }
            }
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert,
                                "DFE: " + std::to_string(reachableFunctions.size()) + "/" +
                                    std::to_string(program.functions.size()) +
                                    " functions reachable");
    }

    // 関数実装
    // 重複した関数はスキップ
    declaredFunctions.clear();
    // std::cerr << "[MIR2LLVM] Converting " << program.functions.size()
    //           << " function implementations...\n";
    size_t skippedCount = 0;
    for (size_t i = 0; i < program.functions.size(); ++i) {
        const auto& func = program.functions[i];
        auto funcId = generateFunctionId(*func);
        // std::cerr << "[MIR2LLVM] [" << (i + 1) << "/" << program.functions.size()
        //           << "] Converting function: " << funcId << "\n";
        if (declaredFunctions.count(funcId) > 0) {
            // std::cerr << "[MIR2LLVM]   -> Skipping duplicate\n";
            continue;
        }
        declaredFunctions.insert(funcId);

        // DFE: 到達不能な関数はスキップ（宣言は残すがbodyは生成しない）
        if (!reachableFunctions.empty() && reachableFunctions.count(func->name) == 0) {
            skippedCount++;
            continue;
        }

        convertFunction(*func);
        // std::cerr << "[MIR2LLVM]   -> Done converting " << funcId << "\n";
    }

    if (skippedCount > 0) {
        cm::debug::codegen::log(
            cm::debug::codegen::Id::LLVMConvertEnd,
            "DFE: skipped " + std::to_string(skippedCount) + " unreachable functions");
    }

    // std::cerr << "[MIR2LLVM] Conversion complete!\n";
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvertEnd,
                            "MIR to LLVM conversion complete");
}

// モジュール単位でのMIR→LLVM IR変換
// extern関数はdeclareのみ、自モジュール関数は完全変換
void MIRToLLVM::convert(const mir::ModuleProgram& module) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert,
                            "Starting module conversion: " + module.module_name);

    // typedef定義マップをコピー（convertTypeでTypeAlias/Struct名の解決に使用）
    if (module.typedef_defs) {
        typedefDefs = *module.typedef_defs;
    }

    // プログラム全体のメタデータ参照（モジュール修飾呼び出しの関数存在判定・インターフェイス検索等）。
    // 未設定だとconvertCallTerminator等がnull参照で落ちる
    currentProgram = module.origin;

    // アドレス取得された関数を収集（sret変換の除外判定用。全体プログラムから計算し全モジュールで同一判定にする）
    addressTakenFunctions.clear();
    if (module.origin) {
        collectAddressTakenFunctions(module.origin->functions);
    }

    // allModuleFunctionsを構築（プログラム全体の関数。originがあれば全関数、無ければ自モジュール+extern）
    // declareExternalFunctionのシグネチャ解決フォールバックに使用。
    // 収集漏れがあると呼び出し時にptr等の誤ったシグネチャを推測し、構造体値渡しABIが崩れる
    allModuleFunctions.clear();
    if (module.origin) {
        for (const auto& func : module.origin->functions) {
            if (func) {
                allModuleFunctions.push_back(func.get());
            }
        }
    } else {
        for (const auto* func : module.functions) {
            allModuleFunctions.push_back(func);
        }
        for (const auto* func : module.extern_functions) {
            allModuleFunctions.push_back(func);
        }
    }

    // ターゲット判定をキャッシュ
    std::string triple = this->module->getTargetTriple();
    isWasmTarget = triple.find("wasm") != std::string::npos;
    isUefiTarget = triple.find("windows") != std::string::npos;

    // === インターフェース名を収集 ===
    for (const auto* iface : module.interfaces) {
        if (iface) {
            interfaceNames.insert(iface->name);
        }
    }

    // === 構造体型を定義（自モジュール + extern） ===
    // パス1: opaque型として作成
    auto register_structs_pass1 = [&](const std::vector<const mir::MirStruct*>& structs) {
        for (const auto* structPtr : structs) {
            const auto& name = structPtr->name;
            if (structTypes.count(name) > 0)
                continue;  // 重複スキップ
            structDefs[name] = structPtr;
            auto structType = llvm::StructType::create(ctx.getContext(), name);
            structTypes[name] = structType;
        }
    };
    register_structs_pass1(module.structs);
    register_structs_pass1(module.extern_structs);

    // パス2: フィールド型を設定
    auto register_structs_pass2 = [&](const std::vector<const mir::MirStruct*>& structs) {
        for (const auto* structPtr : structs) {
            const auto& name = structPtr->name;
            auto it = structTypes.find(name);
            if (it == structTypes.end())
                continue;
            if (it->second->isOpaque()) {
                std::vector<llvm::Type*> fieldTypes;
                for (const auto& field : structPtr->fields) {
                    fieldTypes.push_back(convertType(field.type));
                }
                it->second->setBody(fieldTypes);
            }
        }
    };
    register_structs_pass2(module.structs);
    register_structs_pass2(module.extern_structs);

    // === enum型を定義（自モジュール + extern） ===
    auto register_enums = [&](const std::vector<const mir::MirEnum*>& enums) {
        for (const auto* enumPtr : enums) {
            const auto& enumDef = *enumPtr;
            if (enumDefs.count(enumDef.name) > 0)
                continue;  // 重複スキップ
            enumDefs[enumDef.name] = &enumDef;

            // Tagged Unionの場合は構造体型を生成
            if (enumDef.is_tagged_union()) {
                uint32_t maxPayloadSize = enumDef.max_payload_size();
                if (maxPayloadSize == 0)
                    maxPayloadSize = 8;

                std::vector<llvm::Type*> enumFieldTypes;
                enumFieldTypes.push_back(ctx.getI32Type());
                enumFieldTypes.push_back(llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize));

                auto enumStructType = llvm::StructType::create(ctx.getContext(), enumFieldTypes,
                                                               enumDef.name + "_tagged");
                enumTypes[enumDef.name] = enumStructType;
            }
        }
    };
    register_enums(module.enums);
    register_enums(module.extern_enums);

    // === インターフェースfat pointer型 ===
    for (const auto* iface : module.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // === グローバル変数（extern宣言: 他モジュールが定義を持つ） ===
    // 初期化子なしのExternalLinkage宣言のみ生成し、定義は所有モジュールに一本化する
    for (const auto* gv : module.extern_global_vars) {
        if (!gv)
            continue;
        if (globalVariables.count(gv->name) > 0)
            continue;  // 重複スキップ

        auto llvmType = convertType(gv->type);
        if (!llvmType)
            continue;

        auto globalVar =
            new llvm::GlobalVariable(*this->module, llvmType, gv->is_const,
                                     llvm::GlobalValue::ExternalLinkage, nullptr, gv->name);
        globalVariables[gv->name] = globalVar;
    }

    // === グローバル変数（定義: このモジュールが所有） ===
    for (const auto* gv : module.global_vars) {
        if (!gv)
            continue;
        if (globalVariables.count(gv->name) > 0)
            continue;  // 重複スキップ

        auto llvmType = convertType(gv->type);
        if (!llvmType)
            continue;

        // モジュール分割時は他モジュールから参照されうるため、exportに関わらずExternalLinkageで定義する
        auto linkage = llvm::GlobalValue::ExternalLinkage;

        llvm::Constant* initialValue = nullptr;
        if (gv->init_value) {
            // 文字列型の場合: IRBuilderなしでグローバル文字列定数を作成
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                auto strConstant = llvm::ConstantDataArray::getString(ctx.getContext(), str, true);
                auto strGlobal = new llvm::GlobalVariable(*this->module, strConstant->getType(),
                                                          true, llvm::GlobalValue::PrivateLinkage,
                                                          strConstant, gv->name + ".str");
                strGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                initialValue = llvm::ConstantExpr::getBitCast(
                    llvm::ConstantExpr::getInBoundsGetElementPtr(
                        strConstant->getType(), strGlobal,
                        llvm::ArrayRef<llvm::Constant*>{
                            llvm::ConstantInt::get(ctx.getI64Type(), 0),
                            llvm::ConstantInt::get(ctx.getI64Type(), 0)}),
                    ctx.getPtrType());
            } else if (std::holds_alternative<int64_t>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantInt::get(llvmType, std::get<int64_t>(gv->init_value->value));
            } else if (std::holds_alternative<double>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantFP::get(llvmType, std::get<double>(gv->init_value->value));
            } else if (std::holds_alternative<bool>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantInt::get(llvmType, std::get<bool>(gv->init_value->value) ? 1 : 0);
            }
        }
        if (!initialValue) {
            initialValue = llvm::Constant::getNullValue(llvmType);
        }

        auto globalVar = new llvm::GlobalVariable(*this->module, llvmType, gv->is_const, linkage,
                                                  initialValue, gv->name);
        globalVariables[gv->name] = globalVar;
    }

    // === 関数シグネチャを宣言 ===
    std::set<std::string> declaredFunctions;

    // 自モジュールの関数（完全な定義）
    for (const auto* func : module.functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        auto llvmFunc = convertFunctionSignature(*func);
        functions[funcId] = llvmFunc;
    }

    // extern関数（宣言のみ、ExternalLinkage）
    for (const auto* func : module.extern_functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        auto llvmFunc = convertFunctionSignature(*func);
        // extern関数はExternalLinkageを確保
        llvmFunc->setLinkage(llvm::GlobalValue::ExternalLinkage);
        functions[funcId] = llvmFunc;
    }

    // 残る他モジュール関数も正しいMIRシグネチャで宣言しておく（宣言のみでオブジェクトコストはゼロ）。
    // extern_functionsの参照収集はCall terminator直参照しか見ないため、間接参照される関数が漏れると
    // 呼び出し時のdeclareExternalFunctionフォールバックが誤ったシグネチャ（ptr等）を推測してABIが崩れる
    if (module.origin) {
        for (const auto& func : module.origin->functions) {
            if (!func)
                continue;
            auto funcId = generateFunctionId(*func);
            if (declaredFunctions.count(funcId) > 0)
                continue;
            declaredFunctions.insert(funcId);
            auto llvmFunc = convertFunctionSignature(*func);
            llvmFunc->setLinkage(llvm::GlobalValue::ExternalLinkage);
            functions[funcId] = llvmFunc;
        }
    }

    // === vtable生成 ===
    // 全関数のシグネチャ宣言が済んでいるため、vtableエントリの実装関数はモジュール内の宣言として解決できる。
    // vtable配列はPrivateLinkageでモジュールごとに複製されるが、エントリはExternalLinkage関数への参照であり、
    // fat pointer経由の間接呼び出しはポインタ値を運ぶためモジュール境界を越えても正しく動作する
    if (module.origin) {
        generateVTables(*module.origin);
    }

    // === 自モジュール関数の実装を変換 ===
    declaredFunctions.clear();
    for (const auto* func : module.functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        convertFunction(*func);
    }
    // Bug#45修正: extern_functionsにbody付きのimport先export関数が含まれる場合、bodyも生成する (declareだけだとリンカエラーになる)
    // 定義元モジュールも同じbodyを定義するため、linkonce_odrで重複定義をリンク時にマージする（同一MIR由来なのでODR安全）
    for (const auto* func : module.extern_functions) {
        if (!func->basic_blocks.empty() && !func->is_extern) {
            auto funcId = generateFunctionId(*func);
            if (declaredFunctions.count(funcId) > 0)
                continue;
            declaredFunctions.insert(funcId);
            convertFunction(*func);
            auto it = functions.find(funcId);
            if (it != functions.end() && !it->second->isDeclaration()) {
                it->second->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
            }
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvertEnd,
                            "Module conversion complete: " + module.module_name);
}

}  // namespace cm::codegen::llvm_backend
