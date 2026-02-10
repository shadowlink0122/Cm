#include "codegen.hpp"
#include "types.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <variant>

namespace cm::codegen::js {

using ast::TypeKind;

std::string JSCodeGen::emitRvalue(const mir::MirRvalue& rvalue, const mir::MirFunction& func) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            const auto& data = std::get<mir::MirRvalue::UseData>(rvalue.data);
            // 代入時の構造体Copyはクローンを適用
            return emitOperandWithClone(*data.operand, func);
        }

        case mir::MirRvalue::BinaryOp: {
            const auto& data = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            std::string lhs = emitOperand(*data.lhs, func);
            std::string rhs = emitOperand(*data.rhs, func);
            std::string op = emitBinaryOp(data.op);

            // ポインタ演算: result_typeがPointerの場合（加算・減算）
            if (data.result_type && data.result_type->kind == TypeKind::Pointer) {
                // ポインタ加算: ptr + n → __cm_ptr_add(ptr, n)
                if (data.op == mir::MirBinaryOp::Add) {
                    return "__cm_ptr_add(" + lhs + ", " + rhs + ")";
                }
                // ポインタ減算: ptr - n → __cm_ptr_sub(ptr, n)
                if (data.op == mir::MirBinaryOp::Sub) {
                    return "__cm_ptr_sub(" + lhs + ", " + rhs + ")";
                }
            }

            // ポインタ比較: 両オペランドがPointerの場合のみ.__idx比較
            auto lhsType = getOperandType(*data.lhs, func);
            auto rhsType = getOperandType(*data.rhs, func);
            if (lhsType && lhsType->kind == TypeKind::Pointer && rhsType &&
                rhsType->kind == TypeKind::Pointer) {
                if (data.op == mir::MirBinaryOp::Lt || data.op == mir::MirBinaryOp::Gt ||
                    data.op == mir::MirBinaryOp::Le || data.op == mir::MirBinaryOp::Ge ||
                    data.op == mir::MirBinaryOp::Eq || data.op == mir::MirBinaryOp::Ne) {
                    return "(" + lhs + ".__idx " + op + " " + rhs + ".__idx)";
                }
            }

            // 配列・構造体の比較には深い比較を使用
            if (data.op == mir::MirBinaryOp::Eq || data.op == mir::MirBinaryOp::Ne) {
                if (lhsType &&
                    (lhsType->kind == TypeKind::Array || lhsType->kind == TypeKind::Struct)) {
                    std::string check = "__cm_deep_equal(" + lhs + ", " + rhs + ")";
                    if (data.op == mir::MirBinaryOp::Ne) {
                        return "!" + check;
                    }
                    return check;
                }
            }

            // 整数除算の場合はMath.truncを使用
            if (data.op == mir::MirBinaryOp::Div && data.result_type) {
                if (data.result_type->is_integer()) {
                    return "Math.trunc(" + lhs + " " + op + " " + rhs + ")";
                }
            }

            return "(" + lhs + " " + op + " " + rhs + ")";
        }

        case mir::MirRvalue::UnaryOp: {
            const auto& data = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            std::string operand = emitOperand(*data.operand, func);
            std::string op = emitUnaryOp(data.op);
            return op + operand;
        }

        case mir::MirRvalue::Aggregate: {
            const auto& data = std::get<mir::MirRvalue::AggregateData>(rvalue.data);
            switch (data.kind.type) {
                case mir::AggregateKind::Array: {
                    std::string result = "[";
                    for (size_t i = 0; i < data.operands.size(); ++i) {
                        if (i > 0)
                            result += ", ";
                        result += emitOperand(*data.operands[i], func);
                    }
                    return result + "]";
                }
                case mir::AggregateKind::Struct: {
                    // 構造体のフィールドを検索
                    auto it = struct_map_.find(data.kind.name);
                    if (it != struct_map_.end() && it->second) {
                        std::string result = "{ ";
                        for (size_t i = 0;
                             i < data.operands.size() && i < it->second->fields.size(); ++i) {
                            if (i > 0)
                                result += ", ";
                            result +=
                                formatStructFieldKey(*it->second, it->second->fields[i].name) +
                                ": " + emitOperand(*data.operands[i], func);
                        }
                        return result + " }";
                    }
                    return "{}";
                }
                case mir::AggregateKind::Tuple: {
                    std::string result = "[";
                    for (size_t i = 0; i < data.operands.size(); ++i) {
                        if (i > 0)
                            result += ", ";
                        result += emitOperand(*data.operands[i], func);
                    }
                    return result + "]";
                }
            }
            break;
        }

        case mir::MirRvalue::Ref: {
            const auto& data = std::get<mir::MirRvalue::RefData>(rvalue.data);
            // 配列要素へのRef（&arr[i]）→ ポインタオブジェクト {__arr, __idx}
            if (!data.place.projections.empty()) {
                const auto& lastProj = data.place.projections.back();
                if (lastProj.kind == mir::ProjectionKind::Index) {
                    // 配列のベースを取得（indexプロジェクション前まで）
                    std::string base = getLocalVarName(func, data.place.local);
                    if (boxed_locals_.count(data.place.local)) {
                        base += "[0]";
                    }
                    // index前のプロジェクションを適用
                    for (size_t i = 0; i < data.place.projections.size() - 1; ++i) {
                        const auto& proj = data.place.projections[i];
                        if (proj.kind == mir::ProjectionKind::Field) {
                            hir::TypePtr currentType = nullptr;
                            if (data.place.local < func.locals.size()) {
                                currentType = func.locals[data.place.local].type;
                            }
                            if (currentType && currentType->kind == TypeKind::Struct) {
                                auto it = struct_map_.find(currentType->name);
                                if (it != struct_map_.end() && it->second &&
                                    proj.field_id < it->second->fields.size()) {
                                    base += "." + sanitizeIdentifier(
                                                      it->second->fields[proj.field_id].name);
                                }
                            }
                        } else if (proj.kind == mir::ProjectionKind::Deref) {
                            // 構造体ポインタのDerefはno-op
                        }
                    }
                    // インデックス値
                    std::string idxStr = getLocalVarName(func, lastProj.index_local);
                    return "{__arr: " + base + ", __idx: " + idxStr + "}";
                }
            }
            if (boxed_locals_.count(data.place.local)) {
                // boxed変数へのRef → {__arr: boxed_wrapper, __idx: 0}
                // boxed変数は[value]形式なので.__arr[.__idx] = [value][0] = valueで正しく動作
                return "{__arr: " + getLocalVarName(func, data.place.local) + ", __idx: 0}";
            }
            return getLocalVarName(func, data.place.local);
        }

        case mir::MirRvalue::Cast: {
            const auto& data = std::get<mir::MirRvalue::CastData>(rvalue.data);
            std::string operand = emitOperand(*data.operand, func);

            // 型変換
            if (data.target_type) {
                if (data.target_type->is_integer()) {
                    return "Math.trunc(" + operand + ")";
                } else if (data.target_type->kind == TypeKind::Bool) {
                    return "Boolean(" + operand + ")";
                } else if (data.target_type->kind == TypeKind::String) {
                    return "String(" + operand + ")";
                } else if (data.target_type->kind == TypeKind::Interface) {
                    // インターフェースへのキャスト: {data, vtable} オブジェクトを作成
                    hir::TypePtr sourceType = getOperandType(*data.operand, func);
                    if (sourceType && sourceType->kind == TypeKind::Struct) {
                        std::string vtableName = sanitizeIdentifier(sourceType->name) + "_" +
                                                 sanitizeIdentifier(data.target_type->name) +
                                                 "_vtable";
                        return "{ data: " + operand + ", vtable: " + vtableName + " }";
                    } else if (sourceType && sourceType->kind == TypeKind::Interface) {
                        // Interface -> Interface
                        // すでにあるvtableを使うか、動的解決が必要だが、現在はそのまま返す
                        return operand;
                    }
                    // ソース型が不明またはプリミティブの場合（基本的にはStruct -> Interfaceを想定）
                    // プリミティブの実装（impl int for Interface等）の場合も考慮が必要だが、
                    // 現状はStructのみ対応
                }
            }
            return operand;
        }

        case mir::MirRvalue::FormatConvert: {
            const auto& data = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
            std::string operand = emitOperand(*data.operand, func);

            // char型の場合、format_specが空なら'c'を使用
            std::string spec = data.format_spec;
            if (spec.empty() && data.operand) {
                // オペランドの型をチェック
                if (data.operand->kind == mir::MirOperand::Copy ||
                    data.operand->kind == mir::MirOperand::Move) {
                    const auto& place = std::get<mir::MirPlace>(data.operand->data);
                    if (place.local < func.locals.size()) {
                        const auto& local = func.locals[place.local];
                        if (local.type && local.type->kind == ast::TypeKind::Char) {
                            spec = "c";  // char型は文字として出力
                        }
                    }
                }
            }

            return "__cm_format(" + operand + ", \"" + spec + "\")";
        }
    }
    return "undefined";
}

std::string JSCodeGen::emitOperand(const mir::MirOperand& operand, const mir::MirFunction& func) {
    switch (operand.kind) {
        case mir::MirOperand::Move:
        case mir::MirOperand::Copy: {
            const auto& place = std::get<mir::MirPlace>(operand.data);
            if (place.projections.empty()) {
                auto it = inline_values_.find(place.local);
                if (it != inline_values_.end()) {
                    return it->second;
                }
            }
            return emitPlace(place, func);
        }
        case mir::MirOperand::Constant: {
            const auto& constant = std::get<mir::MirConstant>(operand.data);
            return emitConstant(constant);
        }
        case mir::MirOperand::FunctionRef: {
            const auto& funcName = std::get<std::string>(operand.data);
            return sanitizeIdentifier(funcName);
        }
    }
    return "undefined";
}

// ラムダ関数参照時にキャプチャ変数をバインドする
std::string JSCodeGen::emitLambdaRef(const std::string& funcName, const mir::MirFunction& func,
                                     const std::vector<mir::LocalId>& capturedLocals) {
    std::string safeName = sanitizeIdentifier(funcName);
    if (capturedLocals.empty()) {
        return safeName;
    }
    // キャプチャ変数をbindでバインド
    std::string boundCall = safeName + ".bind(null";
    for (auto capturedId : capturedLocals) {
        boundCall += ", " + getLocalVarName(func, capturedId);
    }
    boundCall += ")";
    return boundCall;
}

// 構造体Copyオペランド用のクローン付き出力
std::string JSCodeGen::emitOperandWithClone(const mir::MirOperand& operand,
                                            const mir::MirFunction& func) {
    if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
        const auto& place = std::get<mir::MirPlace>(operand.data);
        if (place.projections.empty()) {
            auto it = inline_values_.find(place.local);
            if (it != inline_values_.end()) {
                if (operand.kind == mir::MirOperand::Copy && place.local < func.locals.size()) {
                    const auto& local = func.locals[place.local];
                    if (local.type && local.type->kind == ast::TypeKind::Struct) {
                        return "__cm_clone(" + it->second + ")";
                    }
                }
                return it->second;
            }
        }
        if (operand.kind == mir::MirOperand::Copy) {
            std::string result = emitPlace(place, func);
            // 構造体の場合は深いコピーを作成
            if (place.local < func.locals.size() && place.projections.empty()) {
                const auto& local = func.locals[place.local];
                if (local.type && local.type->kind == ast::TypeKind::Struct) {
                    return "__cm_clone(" + result + ")";
                }
            }
            return result;
        }
    }
    return emitOperand(operand, func);
}

std::string JSCodeGen::emitPlace(const mir::MirPlace& place, const mir::MirFunction& func) {
    std::string result = getLocalVarName(func, place.local);

    // ボックス化された変数の場合、[0]アクセスを追加（ただしRefの場合はemitRvalueで処理されるのでここではRead/Writeアクセス）
    if (boxed_locals_.count(place.local)) {
        result += "[0]";
    }

    // 現在の型を追跡（ネストしたフィールドアクセス用）
    hir::TypePtr currentType = nullptr;
    if (place.local < func.locals.size()) {
        currentType = func.locals[place.local].type;
    }

    // プロジェクション
    for (size_t pi = 0; pi < place.projections.size(); ++pi) {
        const auto& proj = place.projections[pi];
        switch (proj.kind) {
            case mir::ProjectionKind::Field: {
                // 構造体のフィールド名を取得
                if (currentType && currentType->kind == TypeKind::Struct) {
                    auto it = struct_map_.find(currentType->name);
                    // ジェネリック構造体: base nameで見つからない場合、mangled nameで再検索
                    if (it == struct_map_.end() && !currentType->type_args.empty()) {
                        std::string mangled = ast::type_to_mangled_name(*currentType);
                        it = struct_map_.find(mangled);
                    }
                    if (it != struct_map_.end() && it->second) {
                        const auto* mirStruct = it->second;
                        if (proj.field_id < mirStruct->fields.size()) {
                            const auto& field_name = mirStruct->fields[proj.field_id].name;
                            if (mirStruct->is_css) {
                                result += "[" + formatStructFieldKey(*mirStruct, field_name) + "]";
                            } else {
                                result += "." + sanitizeIdentifier(field_name);
                            }
                            // 次のプロジェクション用に型を更新
                            currentType = mirStruct->fields[proj.field_id].type;
                            continue;
                        }
                    }
                }
                result += ".field" + std::to_string(proj.field_id);
                currentType = nullptr;  // 型不明
                break;
            }

            case mir::ProjectionKind::Index: {
                // インライン値がある場合はそれを使用（変数宣言がスキップされる場合があるため）
                std::string indexExpr;
                auto inlineIt = inline_values_.find(proj.index_local);
                if (inlineIt != inline_values_.end()) {
                    indexExpr = inlineIt->second;
                } else {
                    indexExpr = getLocalVarName(func, proj.index_local);
                }
                result += "[" + indexExpr + "]";
                // 配列のelement_typeに更新
                if (currentType && currentType->element_type) {
                    currentType = currentType->element_type;
                } else {
                    currentType = nullptr;
                }
                break;
            }

            case mir::ProjectionKind::Deref: {
                // ポインタ参照外し:
                if (boxed_locals_.count(place.local)) {
                    // ボックス化変数: emitPlaceの先頭で既に[0]が追加されているため追加不要
                } else if (currentType && currentType->kind == TypeKind::Pointer &&
                           currentType->element_type &&
                           currentType->element_type->kind != ast::TypeKind::Struct) {
                    // ポインタオブジェクト{__arr, __idx}のデリファレンス
                    std::string ptrExpr = result;
                    // 先読み: 次のプロジェクションがIndexの場合、複合変換
                    // ptr[Deref][Index(idx)] → ptr.__arr[ptr.__idx + idx]
                    if (pi + 1 < place.projections.size() &&
                        place.projections[pi + 1].kind == mir::ProjectionKind::Index) {
                        const auto& nextProj = place.projections[pi + 1];
                        std::string indexExpr;
                        auto inlineIt = inline_values_.find(nextProj.index_local);
                        if (inlineIt != inline_values_.end()) {
                            indexExpr = inlineIt->second;
                        } else {
                            indexExpr = getLocalVarName(func, nextProj.index_local);
                        }
                        result = ptrExpr + ".__arr[" + ptrExpr + ".__idx + " + indexExpr + "]";
                        // Indexプロジェクションを消費
                        ++pi;
                    } else {
                        result = ptrExpr + ".__arr[" + ptrExpr + ".__idx]";
                    }
                } else if (currentType && currentType->element_type &&
                           currentType->element_type->kind == ast::TypeKind::Struct) {
                    // 構造体ポインタ: JSオブジェクトは参照型なのでDerefはno-op
                } else {
                    // その他: ボックス配列の[0]でアクセス
                    result += "[0]";
                }
                // ポインタのelement_typeに更新
                if (currentType && currentType->element_type) {
                    currentType = currentType->element_type;
                } else {
                    currentType = nullptr;
                }
                break;
            }
        }
    }

    return result;
}

std::string JSCodeGen::emitConstant(const mir::MirConstant& constant) {
    return std::visit(
        [](auto&& val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "undefined";
            } else if constexpr (std::is_same_v<T, bool>) {
                return val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream oss;
                oss << std::setprecision(17) << val;
                return oss.str();
            } else if constexpr (std::is_same_v<T, char>) {
                return "\"" + escapeString(std::string(1, val)) + "\"";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + escapeString(val) + "\"";
            } else {
                return "undefined";
            }
        },
        constant.value);
}

std::string JSCodeGen::emitBinaryOp(mir::MirBinaryOp op) {
    switch (op) {
        case mir::MirBinaryOp::Add:
            return "+";
        case mir::MirBinaryOp::Sub:
            return "-";
        case mir::MirBinaryOp::Mul:
            return "*";
        case mir::MirBinaryOp::Div:
            return "/";
        case mir::MirBinaryOp::Mod:
            return "%";
        case mir::MirBinaryOp::BitAnd:
            return "&";
        case mir::MirBinaryOp::BitOr:
            return "|";
        case mir::MirBinaryOp::BitXor:
            return "^";
        case mir::MirBinaryOp::Shl:
            return "<<";
        case mir::MirBinaryOp::Shr:
            return ">>";
        case mir::MirBinaryOp::Eq:
            return "===";
        case mir::MirBinaryOp::Ne:
            return "!==";
        case mir::MirBinaryOp::Lt:
            return "<";
        case mir::MirBinaryOp::Le:
            return "<=";
        case mir::MirBinaryOp::Gt:
            return ">";
        case mir::MirBinaryOp::Ge:
            return ">=";
        case mir::MirBinaryOp::And:
            return "&&";
        case mir::MirBinaryOp::Or:
            return "||";
    }
    return "?";
}

std::string JSCodeGen::emitUnaryOp(mir::MirUnaryOp op) {
    switch (op) {
        case mir::MirUnaryOp::Neg:
            return "-";
        case mir::MirUnaryOp::Not:
            return "!";
        case mir::MirUnaryOp::BitNot:
            return "~";
    }
    return "?";
}

cm::hir::TypePtr JSCodeGen::getPlaceType(const mir::MirPlace& place, const mir::MirFunction& func) {
    cm::hir::TypePtr currentType = nullptr;
    if (place.local < func.locals.size()) {
        currentType = func.locals[place.local].type;
    }

    for (const auto& proj : place.projections) {
        if (!currentType)
            return nullptr;

        switch (proj.kind) {
            case mir::ProjectionKind::Field: {
                if (currentType->kind == TypeKind::Struct) {
                    auto it = struct_map_.find(currentType->name);
                    if (it != struct_map_.end() && it->second) {
                        if (proj.field_id < it->second->fields.size()) {
                            currentType = it->second->fields[proj.field_id].type;
                            continue;
                        }
                    }
                }
                currentType = nullptr;
                break;
            }
            case mir::ProjectionKind::Index: {
                if (currentType->element_type) {
                    currentType = currentType->element_type;
                } else {
                    currentType = nullptr;
                }
                break;
            }
            case mir::ProjectionKind::Deref: {
                if (currentType->element_type) {
                    currentType = currentType->element_type;
                } else {
                    currentType = nullptr;
                }
                break;
            }
        }
    }
    return currentType;
}

cm::hir::TypePtr JSCodeGen::getOperandType(const mir::MirOperand& operand,
                                           const mir::MirFunction& func) {
    if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
        const auto& place = std::get<mir::MirPlace>(operand.data);
        return getPlaceType(place, func);
    }

    return nullptr;
}

}  // namespace cm::codegen::js
