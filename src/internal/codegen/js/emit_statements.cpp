#include "builtins.hpp"
#include "codegen.hpp"
#include "types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cm::codegen::js {

using ast::TypeKind;

namespace {
// 狭い整数型（8/16bit）への代入値をラップする。
// JSの数値は53bit精度のため、tiny等への代入は明示的な
// 切り詰めがないと 127+1 が 128 のまま格納されてしまう
std::string wrapNarrowInt(const std::string& expr, const hir::TypePtr& type) {
    if (!type) {
        return expr;
    }
    switch (type->kind) {
        case TypeKind::Tiny:
            return "((" + expr + " << 24) >> 24)";
        case TypeKind::UTiny:
            return "((" + expr + ") & 0xFF)";
        case TypeKind::Short:
            return "((" + expr + " << 16) >> 16)";
        case TypeKind::UShort:
            return "((" + expr + ") & 0xFFFF)";
        default:
            return expr;
    }
}
}  // namespace

void JSCodeGen::emitBasicBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                               [[maybe_unused]] const mir::MirProgram& program) {
    bool needLabels = func.basic_blocks.size() > 1;

    if (needLabels) {
        emitter_.emitLine("case " + std::to_string(block.id) + ":");
        emitter_.increaseIndent();
    }

    // 文
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }

    // 終端命令
    if (block.terminator) {
        emitTerminator(*block.terminator, func, program);
    }

    if (needLabels) {
        emitter_.emitLine("break;");
        emitter_.decreaseIndent();
    }
}

bool JSCodeGen::structArgNeedsClone(const mir::MirOperand& arg, size_t argIndex,
                                    const std::string& funcName, bool isVirtual,
                                    const mir::MirFunction& func) {
    // Copyのみ対象（Moveは所有権移動のためコピー不要）
    if (arg.kind != mir::MirOperand::Copy) {
        return false;
    }
    // ランタイム組み込みは対象外（indexOf/includes等はJSのオブジェクト同一性に依存し、
    // クローンすると===比較が常に不一致になる）
    if (funcName.rfind("__builtin_", 0) == 0 || funcName.rfind("cm_", 0) == 0) {
        return false;
    }
    // implメソッドのself（第1引数）・仮想ディスパッチのレシーバは参照渡しを維持する
    // （LLVM系もselfはポインタ渡しで、メソッド内の変更は呼び出し元へ伝搬する）
    if (argIndex == 0 && (isVirtual || funcName.find("__") != std::string::npos)) {
        return false;
    }
    hir::TypePtr type = getOperandType(arg, func);
    if (!type || type->kind != TypeKind::Struct) {
        return false;
    }
    // インターフェイス値・外部JSオブジェクト（関数型フィールド構造体）はクローンしない
    if (interface_names_.count(type->name) > 0 || structIsForeignObject(type->name)) {
        return false;
    }
    return true;
}

void JSCodeGen::emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func) {
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            const auto& data = std::get<mir::MirStatement::AssignData>(stmt.data);
            mir::LocalId target_local = data.place.local;

            if (data.place.projections.empty() && inline_values_.count(target_local) > 0) {
                break;
            }

            std::string place = emitPlace(data.place, func);

            // interface値へのcoercion: Shape sh = sq; を {data, vtable} で表現する。
            // 引数渡し（Castで処理される）と同様に、代入でもfatオブジェクトを構築する
            if (data.place.projections.empty() && target_local < func.locals.size() &&
                data.rvalue->kind == mir::MirRvalue::Use) {
                const auto& destType = func.locals[target_local].type;
                const bool dest_is_iface =
                    destType && (destType->kind == TypeKind::Interface ||
                                 (destType->kind == TypeKind::Struct &&
                                  interface_names_.count(destType->name) > 0));
                if (dest_is_iface) {
                    const auto& useData = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                    if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                            useData.operand->kind == mir::MirOperand::Move)) {
                        hir::TypePtr srcType = getOperandType(*useData.operand, func);
                        if (srcType && srcType->kind == TypeKind::Struct &&
                            interface_names_.count(srcType->name) == 0) {
                            std::string src = emitOperand(*useData.operand, func);
                            std::string vtableName = sanitizeIdentifier(srcType->name) + "_" +
                                                     sanitizeIdentifier(destType->name) + "_vtable";
                            std::string fat = "{ data: " + src + ", vtable: " + vtableName + " }";
                            if (declare_on_assign_.count(target_local) > 0 &&
                                declared_locals_.count(target_local) == 0) {
                                emitter_.emitLine("let " + place + " = " + fat + ";");
                                declared_locals_.insert(target_local);
                            } else {
                                emitter_.emitLine(place + " = " + fat + ";");
                            }
                            break;
                        }
                    }
                }
            }

            // interface値へのcoercion（射影付きplace・H2）: 構造体フィールドや配列要素の
            // interface型スロットへ具象構造体を代入する場合も {data, vtable} を構築する。
            // 従来は射影なし代入限定で、Box{sh: sq} や b.sh = sq2 は生値が入り
            // receiver.vtable.method が undefined になっていた
            if (!data.place.projections.empty() && target_local < func.locals.size() &&
                data.rvalue->kind == mir::MirRvalue::Use) {
                // 射影後の格納先型を解決する（Field: 構造体定義、Index/Deref: 要素型）
                hir::TypePtr slotType = func.locals[target_local].type;
                for (const auto& proj : data.place.projections) {
                    if (!slotType) {
                        break;
                    }
                    if (proj.result_type) {
                        slotType = proj.result_type;
                        continue;
                    }
                    if (proj.kind == mir::ProjectionKind::Field) {
                        if (slotType->kind == TypeKind::Struct) {
                            auto sit = struct_map_.find(slotType->name);
                            if (sit != struct_map_.end() && sit->second &&
                                proj.field_id < sit->second->fields.size()) {
                                slotType = sit->second->fields[proj.field_id].type;
                                continue;
                            }
                        }
                        slotType = nullptr;
                    } else {
                        slotType = slotType->element_type;
                    }
                }
                const bool slot_is_iface =
                    slotType && (slotType->kind == TypeKind::Interface ||
                                 (slotType->kind == TypeKind::Struct &&
                                  interface_names_.count(slotType->name) > 0));
                if (slot_is_iface) {
                    const auto& useData = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                    if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                            useData.operand->kind == mir::MirOperand::Move)) {
                        hir::TypePtr srcType = getOperandType(*useData.operand, func);
                        if (srcType && srcType->kind == TypeKind::Struct &&
                            interface_names_.count(srcType->name) == 0) {
                            std::string src = emitOperand(*useData.operand, func);
                            std::string vtableName = sanitizeIdentifier(srcType->name) + "_" +
                                                     sanitizeIdentifier(slotType->name) + "_vtable";
                            emitter_.emitLine(place + " = { data: " + src +
                                              ", vtable: " + vtableName + " };");
                            break;
                        }
                    }
                }
            }

            // クロージャ変数への代入かチェック
            if (target_local < func.locals.size() && data.place.projections.empty() &&
                func.locals[target_local].is_closure && data.rvalue->kind == mir::MirRvalue::Use) {
                const auto& useData = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                if (useData.operand->kind == mir::MirOperand::FunctionRef) {
                    const auto& funcName = std::get<std::string>(useData.operand->data);
                    const auto& local = func.locals[target_local];
                    std::string rvalue = emitLambdaRef(funcName, func, local.captured_locals);
                    // 通常の代入と同じlet宣言チェックを適用
                    if (data.place.projections.empty() &&
                        declare_on_assign_.count(target_local) > 0 &&
                        declared_locals_.count(target_local) == 0) {
                        emitter_.emitLine("let " + place + " = " + rvalue + ";");
                        declared_locals_.insert(target_local);
                    } else {
                        emitter_.emitLine(place + " = " + rvalue + ";");
                    }
                    break;
                }
            }

            // 構造体/ユニオンポインタへの丸ごとDeref代入（*p = v）:
            // ポインタオブジェクト（{__arr, __idx}: スライス要素ポインタ）は要素を置換し、オブジェクト直接参照はフィールドコピーで表現する（IIFEは代入左辺にできないため）
            if (data.place.projections.size() == 1 &&
                data.place.projections[0].kind == mir::ProjectionKind::Deref &&
                target_local < func.locals.size()) {
                const auto& lt = func.locals[target_local].type;
                if (lt && lt->kind == TypeKind::Pointer && lt->element_type &&
                    (lt->element_type->kind == TypeKind::Struct ||
                     lt->element_type->kind == TypeKind::Union)) {
                    std::string base = getLocalVarName(func, target_local);
                    std::string rv = emitRvalue(*data.rvalue, func);
                    emitter_.emitLine("__cm_deref_set(" + base + ", " + rv + ");");
                    break;
                }
            }

            std::string rvalue = emitRvalue(*data.rvalue, func);
            if (data.place.projections.empty() && target_local < func.locals.size()) {
                // 32bit以下の整数スロットへwide64静的型の値を代入する場合はNumber化する（H5）。
                // MIRはlong型定数を無キャストでuint等へ流すことがあり、BigIntのまま漏れると後続のNumber演算でTypeErrorになる
                const auto& dt0 = func.locals[target_local].type;
                if (dt0 && data.rvalue->kind == mir::MirRvalue::Use) {
                    const bool dest_small_int =
                        dt0->kind == TypeKind::Int || dt0->kind == TypeKind::UInt ||
                        dt0->kind == TypeKind::Short || dt0->kind == TypeKind::UShort ||
                        dt0->kind == TypeKind::Tiny || dt0->kind == TypeKind::UTiny;
                    if (dest_small_int) {
                        const auto& useData = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                        hir::TypePtr srcType =
                            useData.operand ? getOperandType(*useData.operand, func) : nullptr;
                        if (srcType &&
                            (srcType->kind == TypeKind::Long || srcType->kind == TypeKind::ULong ||
                             srcType->kind == TypeKind::ISize ||
                             srcType->kind == TypeKind::USize)) {
                            const bool dest_unsigned = dt0->kind == TypeKind::UInt ||
                                                       dt0->kind == TypeKind::UShort ||
                                                       dt0->kind == TypeKind::UTiny;
                            rvalue = std::string("Number(BigInt.") +
                                     (dest_unsigned ? "asUintN" : "asIntN") + "(32, __cm_big(" +
                                     rvalue + ")))";
                        }
                    }
                }
                // 狭い整数型（tiny/utiny/short/ushort）への代入は型幅にラップする
                rvalue = wrapNarrowInt(rvalue, func.locals[target_local].type);
                // 64bit整数スロットへの素の整数リテラル代入はBigIntリテラルにする（H5）。
                // 定数側は値駆動で2^53以内をNumberのまま出すため、bigint宣言スロット（TS）と
                // 「wide64スロットは常にBigInt」の実行時不変条件をここで回復する
                const auto& dt = func.locals[target_local].type;
                if (dt && (dt->kind == TypeKind::Long || dt->kind == TypeKind::ULong ||
                           dt->kind == TypeKind::ISize || dt->kind == TypeKind::USize)) {
                    bool plain_int = !rvalue.empty();
                    for (size_t ci = 0; ci < rvalue.size(); ++ci) {
                        char c = rvalue[ci];
                        if (!(std::isdigit(static_cast<unsigned char>(c)) ||
                              (ci == 0 && c == '-'))) {
                            plain_int = false;
                            break;
                        }
                    }
                    if (plain_int && rvalue != "-") {
                        rvalue = (rvalue[0] == '-') ? "(" + rvalue + "n)" : rvalue + "n";
                    }
                }
            }
            if (data.place.projections.empty() && declare_on_assign_.count(target_local) > 0 &&
                declared_locals_.count(target_local) == 0) {
                emitter_.emitLine("let " + place + " = " + rvalue + ";");
                declared_locals_.insert(target_local);
            } else {
                emitter_.emitLine(place + " = " + rvalue + ";");
            }
            break;
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
        case mir::MirStatement::Asm:
            // JSではこれらは無視（インラインアセンブリはJSターゲットでは未サポート）
            break;
    }
}

void JSCodeGen::emitTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                               [[maybe_unused]] const mir::MirProgram& program) {
    switch (term.kind) {
        case mir::MirTerminator::Return: {
            // 戻り値を返す
            if (func.return_local < func.locals.size()) {
                const auto& local = func.locals[func.return_local];
                if (local.type && local.type->kind == ast::TypeKind::Void) {
                    emitter_.emitLine("return;");
                } else {
                    auto it = inline_values_.find(func.return_local);
                    if (it != inline_values_.end()) {
                        emitter_.emitLine("return " + it->second + ";");
                    } else {
                        std::string retVar = getLocalVarName(func, func.return_local);
                        emitter_.emitLine("return " + retVar + ";");
                    }
                }
            } else {
                emitter_.emitLine("return;");
            }
            break;
        }

        case mir::MirTerminator::Goto: {
            const auto& data = std::get<mir::MirTerminator::GotoData>(term.data);
            if (func.basic_blocks.size() > 1) {
                emitter_.emitLine("__block = " + std::to_string(data.target) + ";");
                emitter_.emitLine("continue __dispatch;");
            }
            break;
        }

        case mir::MirTerminator::SwitchInt: {
            const auto& data = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            std::string discrim = emitOperand(*data.discriminant, func);

            // discriminantの型を判定してboolean/integerで分岐
            auto discrimType = getOperandType(*data.discriminant, func);
            bool isBoolDiscrim = (discrimType && discrimType->kind == hir::TypeKind::Bool);

            for (const auto& [value, target] : data.targets) {
                if (isBoolDiscrim) {
                    // Boolean型: truthy/falsy評価（JS: true === 1 は false になるため）
                    if (value == 1) {
                        emitter_.emitLine("if (" + discrim + ") {");
                    } else if (value == 0) {
                        emitter_.emitLine("if (!" + discrim + ") {");
                    } else {
                        emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) +
                                          ") {");
                    }
                } else {
                    // 整数型: 厳密比較（値2がcase 1にマッチする等のバグ防止）
                    emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) + ") {");
                }
                emitter_.increaseIndent();
                emitter_.emitLine("__block = " + std::to_string(target) + ";");
                emitter_.emitLine("continue __dispatch;");
                emitter_.decreaseIndent();
                emitter_.emitLine("}");
            }
            emitter_.emitLine("__block = " + std::to_string(data.otherwise) + ";");
            emitter_.emitLine("continue __dispatch;");
            break;
        }

        case mir::MirTerminator::Call: {
            const auto& data = std::get<mir::MirTerminator::CallData>(term.data);

            // 関数名取得
            std::string funcName;
            if (data.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(data.func->data);
            } else {
                funcName = emitOperand(*data.func, func);
            }

            // 引数
            std::vector<std::string> args;
            bool isFormatFunc = (funcName == "cm_println_format" || funcName == "cm_print_format");

            // 呼び出し先関数を取得
            const mir::MirFunction* calleeFunc = nullptr;
            auto it_func = function_map_.find(funcName);
            if (it_func != function_map_.end()) {
                calleeFunc = it_func->second;
                if (calleeFunc->is_extern) {
                    if (isBuiltinFunction(calleeFunc->name)) {
                        // ランタイムビルトイン（cm_sb_*等のextern "C"宣言）はJS FFI名へマップしない
                        // （mapExternJsNameは全アンダースコアをドット化するためcm.sb.create等に化ける）
                        funcName = calleeFunc->name;
                    } else if (calleeFunc->package_name == "js" ||
                               calleeFunc->package_name.empty()) {
                        funcName = mapExternJsName(calleeFunc->name);
                    } else if (calleeFunc->package_name != "libc") {
                        // 外部パッケージ: pkg.func() 形式に変換
                        std::string alias = calleeFunc->package_name;
                        size_t slashPos = alias.rfind('/');
                        if (slashPos != std::string::npos) {
                            alias = alias.substr(slashPos + 1);
                        }
                        for (auto& c : alias) {
                            if (c == '-')
                                c = '_';
                        }
                        funcName = alias + "." + sanitizeIdentifier(calleeFunc->name);
                    }
                }
            }

            for (size_t i = 0; i < data.args.size(); ++i) {
                const auto& arg = data.args[i];
                if (arg) {
                    std::string argStr = emitOperand(*arg, func);
                    bool interfaceWrapped = false;

                    // 引数の型変換（Struct -> Interfaceの暗黙キャスト）
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId targetLocalId = calleeFunc->arg_locals[i];
                        if (targetLocalId < calleeFunc->locals.size()) {
                            const auto& targetLocal = calleeFunc->locals[targetLocalId];
                            if (targetLocal.type && targetLocal.type->kind == TypeKind::Interface) {
                                auto sourceType = getOperandType(*arg, func);
                                if (sourceType && sourceType->kind == TypeKind::Struct) {
                                    std::string vtableName =
                                        sanitizeIdentifier(sourceType->name) + "_" +
                                        sanitizeIdentifier(targetLocal.type->name) + "_vtable";
                                    argStr = "{ data: " + argStr + ", vtable: " + vtableName + " }";
                                    interfaceWrapped = true;
                                }
                            }
                        }
                    }

                    // フォーマット関数の場合、char型引数を文字に変換
                    // 引数インデックス2以降が実際のフォーマット値
                    if (isFormatFunc && i >= 2) {
                        bool isCharArg = false;
                        // オペランドの型情報を直接チェック（Constant含む全種別対応）
                        if (arg->type && arg->type->kind == ast::TypeKind::Char) {
                            isCharArg = true;
                        }
                        // フォールバック: Copy/Moveの場合はローカル変数の型もチェック
                        if (!isCharArg && (arg->kind == mir::MirOperand::Copy ||
                                           arg->kind == mir::MirOperand::Move)) {
                            const auto& place = std::get<mir::MirPlace>(arg->data);
                            if (place.local < func.locals.size()) {
                                const auto& local = func.locals[place.local];
                                if (local.type && local.type->kind == ast::TypeKind::Char) {
                                    isCharArg = true;
                                }
                            }
                        }
                        if (isCharArg && !(argStr.size() >= 2 && argStr.front() == '"')) {
                            // char型は文字に変換（定数畳み込み済みの文字列リテラルはそのまま）
                            argStr = "String.fromCharCode(" + argStr + ")";
                        }
                    }

                    // H5: ランタイムスライスビルトインの値引数境界（callee情報が無いため名前で判定）。
                    // 32bit以下スロットへのpush/setに64bit型付き定数（uint[]リテラルの4000000000等）が
                    // 流入するとBigInt混在TypeErrorになるため、スロット幅へ正規化する
                    if (!calleeFunc && i == 1) {
                        auto at2 = getOperandType(*arg, func);
                        const bool a64 = at2 && (at2->kind == ast::TypeKind::Long ||
                                                 at2->kind == ast::TypeKind::ULong ||
                                                 at2->kind == ast::TypeKind::ISize ||
                                                 at2->kind == ast::TypeKind::USize);
                        if (a64 && (funcName.find("cm_slice_push_i8") == 0 ||
                                    funcName.find("cm_slice_push_i16") == 0 ||
                                    funcName.find("cm_slice_push_i32") == 0 ||
                                    funcName.find("cm_slice_push_f32") == 0 ||
                                    funcName.find("cm_slice_push_f64") == 0 ||
                                    funcName.find("cm_slice_set_") == 0)) {
                            argStr = "Number(__cm_big(" + argStr + "))";
                        } else if (funcName.find("cm_slice_push_i64") == 0) {
                            argStr = "__cm_big(" + argStr + ")";
                        }
                    }

                    // H5: 64bit（BigInt）と32bit以下（Number）の呼び出し境界変換。
                    // 型注釈上の不一致（64bit型付きリテラルを32bit引数へ渡す等）でJSの
                    // BigInt/Number混在TypeErrorになるため、仮引数型に合わせて明示変換する
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId ptl = calleeFunc->arg_locals[i];
                        if (ptl < calleeFunc->locals.size() && calleeFunc->locals[ptl].type) {
                            auto pk = calleeFunc->locals[ptl].type->kind;
                            auto ak = ast::TypeKind::Void;
                            if (auto at = getOperandType(*arg, func)) {
                                ak = at->kind;
                            }
                            auto is64 = [](ast::TypeKind k) {
                                return k == ast::TypeKind::Long || k == ast::TypeKind::ULong ||
                                       k == ast::TypeKind::ISize || k == ast::TypeKind::USize;
                            };
                            auto is_small_int = [](ast::TypeKind k) {
                                return k == ast::TypeKind::Tiny || k == ast::TypeKind::UTiny ||
                                       k == ast::TypeKind::Short || k == ast::TypeKind::UShort ||
                                       k == ast::TypeKind::Int || k == ast::TypeKind::UInt ||
                                       k == ast::TypeKind::Char || k == ast::TypeKind::Bool;
                            };
                            if (is_small_int(pk) && is64(ak)) {
                                const char* fn =
                                    (pk == ast::TypeKind::UInt || pk == ast::TypeKind::UShort ||
                                     pk == ast::TypeKind::UTiny)
                                        ? "BigInt.asUintN"
                                        : "BigInt.asIntN";
                                argStr =
                                    "Number(" + std::string(fn) + "(32, __cm_big(" + argStr + ")))";
                            } else if (is64(pk) && is_small_int(ak)) {
                                argStr = "__cm_big(" + argStr + ")";
                            }
                        }
                    }
                    // 構造体の実引数は値渡しとしてクローンする（H3: LLVM系との値セマンティクス統一。
                    // 従来は参照が渡り、呼び出し先での変更が呼び出し元へ漏れていた）
                    if (!interfaceWrapped &&
                        structArgNeedsClone(*arg, i, funcName, data.is_virtual, func)) {
                        argStr = "__cm_clone(" + argStr + ")";
                    }

                    args.push_back(argStr);
                }
            }

            // 組み込み関数のチェック
            std::string callExpr;
            if (isBuiltinFunction(funcName)) {
                callExpr = emitBuiltinCall(funcName, args);
            } else if (data.is_virtual && !args.empty()) {
                // 仮想ディスパッチ: receiver.vtable.method(receiver.data, ...)
                // args[0]がreceiverで、fat object {data, vtable}
                std::string receiver = args[0];
                std::string methodName;

                if (!data.method_name.empty()) {
                    methodName = data.method_name;
                } else {
                    // メソッド名を抽出（Interface__method形式から）
                    size_t sepPos = funcName.find("__");
                    methodName =
                        (sepPos != std::string::npos) ? funcName.substr(sepPos + 2) : funcName;
                    // サフィックスを除去（method_SType形式から）
                    size_t suffixPos = methodName.rfind("_S");
                    if (suffixPos != std::string::npos) {
                        methodName = methodName.substr(0, suffixPos);
                    }
                }

                // vtableから関数を取得し、dataを第一引数として呼び出す
                callExpr = receiver + ".vtable." + sanitizeIdentifier(methodName) + "(" + receiver +
                           ".data";
                for (size_t i = 1; i < args.size(); ++i) {
                    callExpr += ", " + args[i];
                }
                callExpr += ")";
            } else {
                std::string safeFuncName = sanitizeIdentifier(funcName);
                callExpr = safeFuncName + "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0)
                        callExpr += ", ";
                    callExpr += args[i];
                }
                callExpr += ")";
            }

            // await式の場合、callExprをawaitでラップ
            if (data.is_awaited) {
                callExpr = "await " + callExpr;
            }

            // 戻り値の格納
            bool skip_dest = false;
            if (data.destination && data.destination->projections.empty()) {
                if (!isLocalUsed(data.destination->local)) {
                    skip_dest = true;
                }
                // インライン値としてスキップ
                if (inline_values_.count(data.destination->local) > 0) {
                    skip_dest = true;
                }
            }
            if (data.destination && !skip_dest) {
                std::string dest = emitPlace(*data.destination, func);
                // declare_on_assign対応
                if (data.destination->projections.empty() &&
                    declare_on_assign_.count(data.destination->local) > 0 &&
                    declared_locals_.count(data.destination->local) == 0) {
                    emitter_.emitLine("let " + dest + " = " + callExpr + ";");
                    declared_locals_.insert(data.destination->local);
                } else {
                    emitter_.emitLine(dest + " = " + callExpr + ";");
                }
            } else {
                emitter_.emitLine(callExpr + ";");
            }

            // 次のブロック
            if (func.basic_blocks.size() > 1) {
                emitter_.emitLine("__block = " + std::to_string(data.success) + ";");
                emitter_.emitLine("continue __dispatch;");
            }
            break;
        }

        case mir::MirTerminator::Unreachable:
            emitter_.emitLine("throw new Error('Unreachable code');");
            break;
    }
}

// 線形フロー用の基本ブロック出力（switch/dispatchなし）
void JSCodeGen::emitLinearBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                                [[maybe_unused]] const mir::MirProgram& program) {
    // 文を直接出力
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }

    // 終端命令を線形フロー用に出力
    if (block.terminator) {
        emitLinearTerminator(*block.terminator, func, program);
    }
}

// 線形フロー用の終端命令出力
void JSCodeGen::emitLinearTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                                     [[maybe_unused]] const mir::MirProgram& program) {
    switch (term.kind) {
        case mir::MirTerminator::Return: {
            // 戻り値を返す
            if (func.return_local < func.locals.size()) {
                const auto& local = func.locals[func.return_local];
                if (local.type && local.type->kind == ast::TypeKind::Void) {
                    emitter_.emitLine("return;");
                } else {
                    auto it = inline_values_.find(func.return_local);
                    if (it != inline_values_.end()) {
                        emitter_.emitLine("return " + it->second + ";");
                    } else {
                        std::string retVar = getLocalVarName(func, func.return_local);
                        emitter_.emitLine("return " + retVar + ";");
                    }
                }
            } else {
                emitter_.emitLine("return;");
            }
            break;
        }

        case mir::MirTerminator::Goto:
            // 線形フローでは次のブロックに自然にフォールスルー
            // 何も出力しない
            break;

        case mir::MirTerminator::Call: {
            const auto& data = std::get<mir::MirTerminator::CallData>(term.data);

            // 関数名取得
            std::string funcName;
            if (data.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(data.func->data);
            } else {
                funcName = emitOperand(*data.func, func);
            }

            // 引数
            std::vector<std::string> args;
            bool isFormatFunc = (funcName == "cm_println_format" || funcName == "cm_print_format");

            // 呼び出し先関数を取得
            const mir::MirFunction* calleeFunc = nullptr;
            auto it_func = function_map_.find(funcName);
            if (it_func != function_map_.end()) {
                calleeFunc = it_func->second;
                if (calleeFunc->is_extern) {
                    if (isBuiltinFunction(calleeFunc->name)) {
                        // ランタイムビルトイン（cm_sb_*等のextern "C"宣言）はJS FFI名へマップしない
                        // （mapExternJsNameは全アンダースコアをドット化するためcm.sb.create等に化ける）
                        funcName = calleeFunc->name;
                    } else if (calleeFunc->package_name == "js" ||
                               calleeFunc->package_name.empty()) {
                        funcName = mapExternJsName(calleeFunc->name);
                    } else if (calleeFunc->package_name != "libc") {
                        // 外部パッケージ: pkg.func() 形式に変換
                        std::string alias = calleeFunc->package_name;
                        size_t slashPos = alias.rfind('/');
                        if (slashPos != std::string::npos) {
                            alias = alias.substr(slashPos + 1);
                        }
                        for (auto& c : alias) {
                            if (c == '-')
                                c = '_';
                        }
                        funcName = alias + "." + sanitizeIdentifier(calleeFunc->name);
                    }
                }
            }

            for (size_t i = 0; i < data.args.size(); ++i) {
                const auto& arg = data.args[i];
                if (arg) {
                    std::string argStr = emitOperand(*arg, func);
                    bool interfaceWrapped = false;

                    // 引数の型変換（Struct -> Interfaceの暗黙キャスト）
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId targetLocalId = calleeFunc->arg_locals[i];
                        if (targetLocalId < calleeFunc->locals.size()) {
                            const auto& targetLocal = calleeFunc->locals[targetLocalId];

                            bool isTargetInterface =
                                targetLocal.type &&
                                (targetLocal.type->kind == TypeKind::Interface ||
                                 (targetLocal.type->kind == TypeKind::Struct &&
                                  interface_names_.count(targetLocal.type->name)));

                            if (isTargetInterface) {
                                auto sourceType = getOperandType(*arg, func);
                                if (sourceType && sourceType->kind == TypeKind::Struct) {
                                    std::string vtableName =
                                        sanitizeIdentifier(sourceType->name) + "_" +
                                        sanitizeIdentifier(targetLocal.type->name) + "_vtable";
                                    argStr = "{ data: " + argStr + ", vtable: " + vtableName + " }";
                                    interfaceWrapped = true;
                                }
                            }
                        }
                    }

                    // フォーマット関数の場合、char型引数を文字に変換
                    if (isFormatFunc && i >= 2) {
                        bool isCharArg = false;
                        // オペランドの型情報を直接チェック（Constant含む全種別対応）
                        if (arg->type && arg->type->kind == ast::TypeKind::Char) {
                            isCharArg = true;
                        }
                        // フォールバック: Copy/Moveの場合はローカル変数の型もチェック
                        if (!isCharArg && (arg->kind == mir::MirOperand::Copy ||
                                           arg->kind == mir::MirOperand::Move)) {
                            const auto& place = std::get<mir::MirPlace>(arg->data);
                            if (place.local < func.locals.size()) {
                                const auto& local = func.locals[place.local];
                                if (local.type && local.type->kind == ast::TypeKind::Char) {
                                    isCharArg = true;
                                }
                            }
                        }
                        if (isCharArg && !(argStr.size() >= 2 && argStr.front() == '"')) {
                            // 定数畳み込み済みの文字列リテラルはそのまま使う
                            argStr = "String.fromCharCode(" + argStr + ")";
                        }

                        // ユニオン型（タグ付き {field0, field1}）はペイロードへアンラップ
                        {
                            hir::TypePtr argType = getOperandType(*arg, func);
                            if (argType && argType->kind == ast::TypeKind::Union) {
                                argStr =
                                    "((u) => (u !== null && typeof u === \"object\" && "
                                    "u.field0 !== undefined) ? u.field1 : u)(" +
                                    argStr + ")";
                            }
                        }
                    }

                    // H5: ランタイムスライスビルトインの値引数境界（callee情報が無いため名前で判定）。
                    // 32bit以下スロットへのpush/setに64bit型付き定数（uint[]リテラルの4000000000等）が
                    // 流入するとBigInt混在TypeErrorになるため、スロット幅へ正規化する
                    if (!calleeFunc && i == 1) {
                        auto at2 = getOperandType(*arg, func);
                        const bool a64 = at2 && (at2->kind == ast::TypeKind::Long ||
                                                 at2->kind == ast::TypeKind::ULong ||
                                                 at2->kind == ast::TypeKind::ISize ||
                                                 at2->kind == ast::TypeKind::USize);
                        if (a64 && (funcName.find("cm_slice_push_i8") == 0 ||
                                    funcName.find("cm_slice_push_i16") == 0 ||
                                    funcName.find("cm_slice_push_i32") == 0 ||
                                    funcName.find("cm_slice_push_f32") == 0 ||
                                    funcName.find("cm_slice_push_f64") == 0 ||
                                    funcName.find("cm_slice_set_") == 0)) {
                            argStr = "Number(__cm_big(" + argStr + "))";
                        } else if (funcName.find("cm_slice_push_i64") == 0) {
                            argStr = "__cm_big(" + argStr + ")";
                        }
                    }

                    // H5: 64bit（BigInt）と32bit以下（Number）の呼び出し境界変換。
                    // 型注釈上の不一致（64bit型付きリテラルを32bit引数へ渡す等）でJSの
                    // BigInt/Number混在TypeErrorになるため、仮引数型に合わせて明示変換する
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId ptl = calleeFunc->arg_locals[i];
                        if (ptl < calleeFunc->locals.size() && calleeFunc->locals[ptl].type) {
                            auto pk = calleeFunc->locals[ptl].type->kind;
                            auto ak = ast::TypeKind::Void;
                            if (auto at = getOperandType(*arg, func)) {
                                ak = at->kind;
                            }
                            auto is64 = [](ast::TypeKind k) {
                                return k == ast::TypeKind::Long || k == ast::TypeKind::ULong ||
                                       k == ast::TypeKind::ISize || k == ast::TypeKind::USize;
                            };
                            auto is_small_int = [](ast::TypeKind k) {
                                return k == ast::TypeKind::Tiny || k == ast::TypeKind::UTiny ||
                                       k == ast::TypeKind::Short || k == ast::TypeKind::UShort ||
                                       k == ast::TypeKind::Int || k == ast::TypeKind::UInt ||
                                       k == ast::TypeKind::Char || k == ast::TypeKind::Bool;
                            };
                            if (is_small_int(pk) && is64(ak)) {
                                const char* fn =
                                    (pk == ast::TypeKind::UInt || pk == ast::TypeKind::UShort ||
                                     pk == ast::TypeKind::UTiny)
                                        ? "BigInt.asUintN"
                                        : "BigInt.asIntN";
                                argStr =
                                    "Number(" + std::string(fn) + "(32, __cm_big(" + argStr + ")))";
                            } else if (is64(pk) && is_small_int(ak)) {
                                argStr = "__cm_big(" + argStr + ")";
                            }
                        }
                    }
                    // 構造体の実引数は値渡しとしてクローンする（H3: LLVM系との値セマンティクス統一。
                    // 従来は参照が渡り、呼び出し先での変更が呼び出し元へ漏れていた）
                    if (!interfaceWrapped &&
                        structArgNeedsClone(*arg, i, funcName, data.is_virtual, func)) {
                        argStr = "__cm_clone(" + argStr + ")";
                    }

                    args.push_back(argStr);
                }
            }

            // 組み込み関数のチェック
            std::string callExpr;
            if (isBuiltinFunction(funcName)) {
                callExpr = emitBuiltinCall(funcName, args);
            } else if (data.is_virtual && !args.empty()) {
                // 仮想ディスパッチ: receiver.vtable.method(receiver.data, ...)
                // args[0]がreceiverで、fat object {data, vtable}
                std::string receiver = args[0];
                std::string methodName;

                if (!data.method_name.empty()) {
                    methodName = data.method_name;
                } else {
                    // メソッド名を抽出（Interface__method形式から）
                    size_t sepPos = funcName.find("__");
                    methodName =
                        (sepPos != std::string::npos) ? funcName.substr(sepPos + 2) : funcName;
                    // サフィックスを除去（method_SType形式から）
                    size_t suffixPos = methodName.rfind("_S");
                    if (suffixPos != std::string::npos) {
                        methodName = methodName.substr(0, suffixPos);
                    }
                }

                // vtableから関数を取得し、dataを第一引数として呼び出す
                callExpr = receiver + ".vtable." + sanitizeIdentifier(methodName) + "(" + receiver +
                           ".data";
                for (size_t i = 1; i < args.size(); ++i) {
                    callExpr += ", " + args[i];
                }
                callExpr += ")";
            } else {
                std::string safeFuncName = sanitizeIdentifier(funcName);
                callExpr = safeFuncName + "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0)
                        callExpr += ", ";
                    callExpr += args[i];
                }
                callExpr += ")";
            }

            // await式の場合、callExprをawaitでラップ
            if (data.is_awaited) {
                callExpr = "await " + callExpr;
            }

            // 戻り値の格納
            bool skip_dest = false;
            if (data.destination && data.destination->projections.empty()) {
                if (!isLocalUsed(data.destination->local)) {
                    skip_dest = true;
                }
                // インライン値としてスキップ
                if (inline_values_.count(data.destination->local) > 0) {
                    skip_dest = true;
                }
            }
            if (data.destination && !skip_dest) {
                std::string dest = emitPlace(*data.destination, func);
                // declare_on_assign対応
                if (data.destination->projections.empty() &&
                    declare_on_assign_.count(data.destination->local) > 0 &&
                    declared_locals_.count(data.destination->local) == 0) {
                    emitter_.emitLine("let " + dest + " = " + callExpr + ";");
                    declared_locals_.insert(data.destination->local);
                } else {
                    emitter_.emitLine(dest + " = " + callExpr + ";");
                }
            } else {
                emitter_.emitLine(callExpr + ";");
            }
            // 線形フローでは次のブロックに自然にフォールスルー
            break;
        }

        case mir::MirTerminator::SwitchInt:
            // 線形フローにはSwitchIntは含まれないはず
            // フォールバックとしてエラーを投げる
            emitter_.emitLine("throw new Error('Unexpected SwitchInt in linear flow');");
            break;

        case mir::MirTerminator::Unreachable:
            emitter_.emitLine("throw new Error('Unreachable code');");
            break;
    }
}

}  // namespace cm::codegen::js
