#include "monomorphization_utils.hpp"

#include "../../common/debug.hpp"

namespace cm::mir {

// ヘルパー関数：MirOperandのクローン
MirOperandPtr clone_operand(const MirOperandPtr& op) {
    if (!op)
        return nullptr;

    auto result = std::make_unique<MirOperand>();
    result->kind = op->kind;
    result->data = op->data;  // variant内の値をコピー（参照型がないのでOK）
    return result;
}

// ヘルパー関数：MirRvalueのクローン
MirRvaluePtr clone_rvalue(const MirRvaluePtr& rv) {
    if (!rv)
        return nullptr;

    auto result = std::make_unique<MirRvalue>();
    result->kind = rv->kind;

    switch (rv->kind) {
        case MirRvalue::Use: {
            auto& use_data = std::get<MirRvalue::UseData>(rv->data);
            result->data = MirRvalue::UseData{clone_operand(use_data.operand)};
            break;
        }
        case MirRvalue::BinaryOp: {
            auto& bin_data = std::get<MirRvalue::BinaryOpData>(rv->data);
            result->data =
                MirRvalue::BinaryOpData{bin_data.op, clone_operand(bin_data.lhs),
                                        clone_operand(bin_data.rhs), bin_data.result_type};
            break;
        }
        case MirRvalue::UnaryOp: {
            auto& un_data = std::get<MirRvalue::UnaryOpData>(rv->data);
            result->data = MirRvalue::UnaryOpData{un_data.op, clone_operand(un_data.operand)};
            break;
        }
        case MirRvalue::Ref: {
            auto& ref_data = std::get<MirRvalue::RefData>(rv->data);
            result->data = ref_data;
            break;
        }
        case MirRvalue::Aggregate: {
            auto& agg_data = std::get<MirRvalue::AggregateData>(rv->data);
            std::vector<MirOperandPtr> cloned_ops;
            for (const auto& op : agg_data.operands) {
                cloned_ops.push_back(clone_operand(op));
            }
            result->data = MirRvalue::AggregateData{agg_data.kind, std::move(cloned_ops)};
            break;
        }
        case MirRvalue::Cast: {
            auto& cast_data = std::get<MirRvalue::CastData>(rv->data);
            result->data =
                MirRvalue::CastData{clone_operand(cast_data.operand), cast_data.target_type};
            break;
        }
        case MirRvalue::FormatConvert: {
            auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rv->data);
            result->data =
                MirRvalue::FormatConvertData{clone_operand(fmt_data.operand), fmt_data.format_spec};
            break;
        }
    }

    return result;
}

// ヘルパー関数：MirStatementのクローン
MirStatementPtr clone_statement(const MirStatementPtr& stmt) {
    if (!stmt)
        return nullptr;

    auto result = std::make_unique<MirStatement>();
    result->kind = stmt->kind;
    result->span = stmt->span;

    switch (stmt->kind) {
        case MirStatement::Assign: {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
            result->data =
                MirStatement::AssignData{assign_data.place, clone_rvalue(assign_data.rvalue)};
            break;
        }
        case MirStatement::StorageLive:
        case MirStatement::StorageDead: {
            auto& storage_data = std::get<MirStatement::StorageData>(stmt->data);
            result->data = storage_data;
            break;
        }
        case MirStatement::Nop:
            result->data = std::monostate{};
            break;
        case MirStatement::Asm:
            // インラインアセンブリはモノモーフィゼーションでは変更不要
            // AsmDataはコピー不可（unique_ptrを含む）なので、Nopとして処理
            // 実際のasm文は元のMIRで保持される
            result->kind = MirStatement::Nop;
            result->data = std::monostate{};
            break;
    }

    return result;
}

// ヘルパー関数：型置換マップを使ってMirTerminatorをクローンし、メソッド呼び出しを書き換え
MirTerminatorPtr clone_terminator_with_subst(
    const MirTerminatorPtr& term,
    const std::unordered_map<std::string, std::string>& type_name_subst) {
    if (!term)
        return nullptr;

    auto result = std::make_unique<MirTerminator>();
    result->kind = term->kind;
    result->span = term->span;

    switch (term->kind) {
        case MirTerminator::Goto: {
            auto& goto_data = std::get<MirTerminator::GotoData>(term->data);
            result->data = goto_data;
            break;
        }
        case MirTerminator::SwitchInt: {
            auto& switch_data = std::get<MirTerminator::SwitchIntData>(term->data);
            result->data = MirTerminator::SwitchIntData{clone_operand(switch_data.discriminant),
                                                        switch_data.targets, switch_data.otherwise};
            break;
        }
        case MirTerminator::Call: {
            auto& call_data = std::get<MirTerminator::CallData>(term->data);
            std::vector<MirOperandPtr> cloned_args;
            for (const auto& arg : call_data.args) {
                cloned_args.push_back(clone_operand(arg));
            }

            // 関数名を書き換え（T__method -> ConcreteType__method, Base__T__method ->
            // Base__int__method）
            auto cloned_func = clone_operand(call_data.func);
            if (cloned_func && cloned_func->kind == MirOperand::FunctionRef) {
                auto& func_name = std::get<std::string>(cloned_func->data);
                // 関数名内のすべての型パラメータを置換
                for (const auto& [type_param, concrete_type] : type_name_subst) {
                    // パターン1: 先頭の "TypeParam__method" -> "ConcreteType__method"
                    std::string prefix = type_param + "__";
                    if (func_name.find(prefix) == 0) {
                        func_name = concrete_type + func_name.substr(type_param.length());
                        debug_msg("MONO", "Rewriting method call (prefix): " + type_param +
                                              "__* -> " + func_name);
                        continue;
                    }
                    // パターン2: 途中の "__TypeParam__" -> "__ConcreteType__"
                    std::string mid_pattern = "__" + type_param + "__";
                    size_t pos = func_name.find(mid_pattern);
                    while (pos != std::string::npos) {
                        func_name = func_name.substr(0, pos) + "__" + concrete_type + "__" +
                                    func_name.substr(pos + mid_pattern.length());
                        debug_msg("MONO", "Rewriting method call (mid): __" + type_param +
                                              "__ -> __" + concrete_type + "__");
                        pos = func_name.find(mid_pattern, pos + concrete_type.length() + 4);
                    }
                }
            }

            result->data = MirTerminator::CallData{std::move(cloned_func), std::move(cloned_args),
                                                   call_data.destination,  call_data.success,
                                                   call_data.unwind,       call_data.interface_name,
                                                   call_data.method_name,  call_data.is_virtual};
            break;
        }
        case MirTerminator::Return:
        case MirTerminator::Unreachable:
            result->data = std::monostate{};
            break;
    }

    return result;
}

// 型から型名文字列を取得するヘルパー
std::string get_type_name(const hir::TypePtr& type) {
    if (!type)
        return "";

    // kindに基づいて名前を返す
    switch (type->kind) {
        case hir::TypeKind::Int:
            return "int";
        case hir::TypeKind::UInt:
            return "uint";
        case hir::TypeKind::Long:
            return "long";
        case hir::TypeKind::ULong:
            return "ulong";
        case hir::TypeKind::Short:
            return "short";
        case hir::TypeKind::UShort:
            return "ushort";
        case hir::TypeKind::Tiny:
            return "tiny";
        case hir::TypeKind::UTiny:
            return "utiny";
        case hir::TypeKind::Float:
            return "float";
        case hir::TypeKind::Double:
            return "double";
        case hir::TypeKind::Char:
            return "char";
        case hir::TypeKind::Bool:
            return "bool";
        case hir::TypeKind::String:
            return "string";
        case hir::TypeKind::Void:
            return "void";
        case hir::TypeKind::Pointer: {
            // ポインタ型: ptr_xxx 形式を返す
            std::string result = "ptr_";
            if (type->element_type) {
                result += get_type_name(type->element_type);
            } else {
                result += "void";
            }
            return result;
        }
        case hir::TypeKind::Struct:
        case hir::TypeKind::Interface:
        case hir::TypeKind::Generic: {
            // type_argsがある場合はマングリング名を生成（Base__TypeArg形式）
            if (!type->type_args.empty()) {
                std::string result = type->name;
                // 既にマングリング済みの名前（__を含む）の場合はそのまま返す
                if (result.find("__") != std::string::npos) {
                    return result;
                }
                // type_argsからマングリング名を生成
                for (const auto& arg : type->type_args) {
                    if (arg) {
                        result += "__" + get_type_name(arg);
                    }
                }
                return result;
            }
            return type->name;
        }
        default:
            return type->name.empty() ? "" : type->name;
    }
}

// 型名から正しいTypePtr型を作成するヘルパー
hir::TypePtr make_type_from_name(const std::string& name) {
    // プリミティブ型にはnameも設定（モノモーフィック化後にtype->nameが必要）
    if (name == "int") {
        auto t = hir::make_int();
        t->name = "int";
        return t;
    }
    if (name == "uint") {
        auto t = hir::make_uint();
        t->name = "uint";
        return t;
    }
    if (name == "long") {
        auto t = hir::make_long();
        t->name = "long";
        return t;
    }
    if (name == "ulong") {
        auto t = hir::make_ulong();
        t->name = "ulong";
        return t;
    }
    if (name == "short") {
        auto t = hir::make_short();
        t->name = "short";
        return t;
    }
    if (name == "ushort") {
        auto t = hir::make_ushort();
        t->name = "ushort";
        return t;
    }
    if (name == "tiny") {
        auto t = hir::make_tiny();
        t->name = "tiny";
        return t;
    }
    if (name == "utiny") {
        auto t = hir::make_utiny();
        t->name = "utiny";
        return t;
    }
    if (name == "float") {
        auto t = hir::make_float();
        t->name = "float";
        return t;
    }
    if (name == "double") {
        auto t = hir::make_double();
        t->name = "double";
        return t;
    }
    if (name == "char") {
        auto t = hir::make_char();
        t->name = "char";
        return t;
    }
    if (name == "bool") {
        auto t = hir::make_bool();
        t->name = "bool";
        return t;
    }
    if (name == "string") {
        auto t = hir::make_string();
        t->name = "string";
        return t;
    }
    if (name == "void") {
        auto t = hir::make_void();
        t->name = "void";
        return t;
    }
    // ポインタ型（ptr_xxx形式）
    if (name.size() > 4 && name.substr(0, 4) == "ptr_") {
        std::string elem_name = name.substr(4);
        auto elem_type = make_type_from_name(elem_name);
        auto t = hir::make_pointer(elem_type);
        t->name = name;
        return t;
    }

    // ネストジェネリクス対応: Vector<int> や Vector<Vector<int>> 形式のパース
    auto lt_pos = name.find('<');
    if (lt_pos != std::string::npos) {
        // ベース型名を抽出
        std::string base_name = name.substr(0, lt_pos);

        // 型引数部分を抽出（最後の>を除く）
        auto gt_pos = name.rfind('>');
        if (gt_pos != std::string::npos && gt_pos > lt_pos) {
            std::string type_args_str = name.substr(lt_pos + 1, gt_pos - lt_pos - 1);

            // カンマで分割（ネストを考慮）
            std::vector<std::string> type_args;
            int depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < type_args_str.size(); ++i) {
                if (type_args_str[i] == '<') {
                    depth++;
                } else if (type_args_str[i] == '>') {
                    depth--;
                } else if (type_args_str[i] == ',' && depth == 0) {
                    std::string arg = type_args_str.substr(start, i - start);
                    // 前後の空白をトリム
                    while (!arg.empty() && arg.front() == ' ')
                        arg.erase(0, 1);
                    while (!arg.empty() && arg.back() == ' ')
                        arg.pop_back();
                    type_args.push_back(arg);
                    start = i + 1;
                }
            }
            // 最後の引数
            std::string last_arg = type_args_str.substr(start);
            while (!last_arg.empty() && last_arg.front() == ' ')
                last_arg.erase(0, 1);
            while (!last_arg.empty() && last_arg.back() == ' ')
                last_arg.pop_back();
            if (!last_arg.empty()) {
                type_args.push_back(last_arg);
            }

            // 再帰的に型を構築
            auto t = hir::make_named(base_name);
            for (const auto& arg : type_args) {
                t->type_args.push_back(make_type_from_name(arg));
            }
            // ネストジェネリクス対応: type->nameにマングリング済み名前を設定
            // これにより後の型置換でVector__intという正しい名前が使用される
            std::string mangled_name = base_name;
            for (const auto& arg : type_args) {
                // 再帰的に正規化（Vector<int> -> Vector__int）
                std::string normalized_arg = arg;
                auto lt = normalized_arg.find('<');
                if (lt != std::string::npos) {
                    // ネストジェネリクスの場合、再帰的にマングリング
                    auto type_ptr = make_type_from_name(arg);
                    if (type_ptr) {
                        normalized_arg = type_ptr->name;
                    }
                }
                mangled_name += "__" + normalized_arg;
            }
            t->name = mangled_name;
            return t;
        }
    }

    // ユーザー定義型（構造体など）
    return hir::make_named(name);
}

// 複数パラメータジェネリクス対応: 型引数文字列を分割
// "T, U" -> ["T", "U"], "int, string" -> ["int", "string"]
// ネストされた <> を考慮（例: "Pair<int, int>, V" -> ["Pair<int, int>", "V"]）
std::vector<std::string> split_type_args(const std::string& type_arg_str) {
    std::vector<std::string> result;
    if (type_arg_str.empty()) {
        return result;
    }

    std::string current;
    int depth = 0;  // ネストの深さ（<>のカウント）

    for (size_t i = 0; i < type_arg_str.size(); ++i) {
        char c = type_arg_str[i];

        if (c == '<') {
            depth++;
            current += c;
        } else if (c == '>') {
            depth--;
            current += c;
        } else if (c == ',' && depth == 0) {
            // トップレベルのカンマ → 分割
            // 空白をトリム
            size_t start = current.find_first_not_of(" \t");
            size_t end = current.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                result.push_back(current.substr(start, end - start + 1));
            } else if (!current.empty()) {
                result.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }

    // 最後の要素を追加
    if (!current.empty()) {
        size_t start = current.find_first_not_of(" \t");
        size_t end = current.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            result.push_back(current.substr(start, end - start + 1));
        } else {
            result.push_back(current);
        }
    }

    return result;
}

}  // namespace cm::mir
