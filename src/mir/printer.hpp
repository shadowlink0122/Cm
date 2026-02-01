#pragma once

#include "nodes.hpp"

#include <sstream>
#include <unordered_set>

namespace cm::mir {

// ============================================================
// MIRプリンター - 安全でロバストなデバッグ出力
// ============================================================
// 特徴:
// - 循環参照検出による無限ループ防止
// - 深度制限による再帰オーバーフロー防止
// - stringstreamによる一括出力でブロッキング回避
// ============================================================

class MirPrinter {
   public:
    struct Options {
        bool show_types = true;   // 型情報を表示
        bool show_spans = false;  // ソース位置を表示
        bool verbose = false;     // 詳細モード
        int max_type_depth = 5;   // 型の再帰表示深度
    };

    MirPrinter() : options_{} {}
    explicit MirPrinter(Options opts) : options_(opts) {}

    // MIRプログラムを文字列に変換
    std::string to_string(const MirProgram& program) {
        std::stringstream ss;
        print(program, ss);
        return ss.str();
    }

    // MIRプログラムを出力
    void print(const MirProgram& program, std::ostream& out) {
        std::stringstream ss;
        ss << "===== MIR Program: " << program.filename << " =====\n\n";

        for (const auto& func : program.functions) {
            if (func) {
                print_function(*func, ss);
                ss << "\n";
            }
        }

        // 一括出力
        out << ss.str();
        out.flush();
    }

   private:
    Options options_;
    // 循環参照検出用
    mutable std::unordered_set<const void*> visited_types_;

    // ========================================
    // 安全な型文字列変換
    // ========================================
    std::string safe_type_to_string(hir::TypePtr type, int depth = 0) const {
        if (!type)
            return "?";
        if (depth > options_.max_type_depth)
            return "...";

        // 循環参照チェック
        const void* ptr = type.get();
        if (visited_types_.count(ptr) > 0) {
            return "<cyclic>";
        }
        visited_types_.insert(ptr);

        std::string result;
        switch (type->kind) {
            case hir::TypeKind::Void:
                result = "void";
                break;
            case hir::TypeKind::Int:
                result = "int";
                break;
            case hir::TypeKind::Tiny:
                result = "tiny";
                break;
            case hir::TypeKind::Short:
                result = "short";
                break;
            case hir::TypeKind::Long:
                result = "long";
                break;
            case hir::TypeKind::UInt:
                result = "uint";
                break;
            case hir::TypeKind::UTiny:
                result = "utiny";
                break;
            case hir::TypeKind::UShort:
                result = "ushort";
                break;
            case hir::TypeKind::ULong:
                result = "ulong";
                break;
            case hir::TypeKind::ISize:
                result = "isize";
                break;
            case hir::TypeKind::USize:
                result = "usize";
                break;
            case hir::TypeKind::Float:
                result = "float";
                break;
            case hir::TypeKind::Double:
                result = "double";
                break;
            case hir::TypeKind::UFloat:
                result = "ufloat";
                break;
            case hir::TypeKind::UDouble:
                result = "udouble";
                break;
            case hir::TypeKind::Bool:
                result = "bool";
                break;
            case hir::TypeKind::Char:
                result = "char";
                break;
            case hir::TypeKind::String:
                result = "string";
                break;
            case hir::TypeKind::CString:
                result = "cstring";
                break;
            case hir::TypeKind::Pointer:
                result = "*" + safe_type_to_string(type->element_type, depth + 1);
                break;
            case hir::TypeKind::Reference:
                result = "&" + safe_type_to_string(type->element_type, depth + 1);
                break;
            case hir::TypeKind::Array:
                result = safe_type_to_string(type->element_type, depth + 1) + "[]";
                break;
            case hir::TypeKind::Struct:
                result = type->name.empty() ? "<struct>" : type->name;
                break;
            case hir::TypeKind::Union:
                result = type->name.empty() ? "<union>" : type->name;
                break;
            case hir::TypeKind::Interface:
                result = type->name.empty() ? "<interface>" : type->name;
                break;
            case hir::TypeKind::Function:
                result = "fn";
                break;
            case hir::TypeKind::Generic:
                result = type->name.empty() ? "T" : type->name;
                break;
            case hir::TypeKind::Inferred:
                result = "_";
                break;
            case hir::TypeKind::Error:
                result = "<error>";
                break;
            case hir::TypeKind::LiteralUnion:
                result = "<literal_union>";
                break;
            case hir::TypeKind::TypeAlias:
                result = type->name.empty() ? "<alias>" : type->name;
                break;
            default:
                result = "<unknown>";
                break;
        }

        visited_types_.erase(ptr);
        return result;
    }

    // ========================================
    // 関数出力
    // ========================================
    void print_function(const MirFunction& func, std::ostream& out) {
        visited_types_.clear();  // 関数ごとにリセット

        out << "fn " << func.name << "(";

        // 引数を出力
        bool first = true;
        for (LocalId arg_id : func.arg_locals) {
            if (!first)
                out << ", ";
            first = false;

            if (arg_id < func.locals.size()) {
                const auto& local = func.locals[arg_id];
                out << "_" << arg_id << ": " << safe_type_to_string(local.type);
            } else {
                out << "_" << arg_id << ": ?";
            }
        }

        // 戻り値型
        std::string return_type = "?";
        if (func.return_local < func.locals.size()) {
            return_type = safe_type_to_string(func.locals[func.return_local].type);
        }
        out << ") -> " << return_type << " {\n";

        // ローカル変数を出力
        out << "    // Locals:\n";
        for (const auto& local : func.locals) {
            out << "    // _" << local.id << ": " << safe_type_to_string(local.type);
            if (!local.name.empty() && local.name != ("_" + std::to_string(local.id))) {
                out << " (" << local.name << ")";
            }
            if (!local.is_mutable) {
                out << " [const]";
            }
            out << "\n";
        }
        out << "\n";

        // 基本ブロックを出力
        for (const auto& block : func.basic_blocks) {
            if (block) {
                print_block(*block, func, out);
            }
        }

        out << "}\n";
    }

    // ========================================
    // 基本ブロック出力
    // ========================================
    void print_block(const BasicBlock& block, const MirFunction& /* func */, std::ostream& out) {
        out << "    bb" << block.id << ": {\n";

        // predecessors表示
        if (!block.predecessors.empty()) {
            out << "        // predecessors: [";
            bool first = true;
            for (BlockId pred : block.predecessors) {
                if (!first)
                    out << ", ";
                first = false;
                out << "bb" << pred;
            }
            out << "]\n";
        }

        // 文を出力
        for (const auto& stmt : block.statements) {
            if (stmt) {
                out << "        ";
                print_statement(*stmt, out);
                out << ";\n";
            }
        }

        // 終端命令を出力
        if (block.terminator) {
            out << "        ";
            print_terminator(*block.terminator, out);
            out << ";\n";
        }

        out << "    }\n";
    }

    // ========================================
    // 文出力
    // ========================================
    void print_statement(const MirStatement& stmt, std::ostream& out) {
        switch (stmt.kind) {
            case MirStatement::Assign: {
                auto& data = std::get<MirStatement::AssignData>(stmt.data);
                print_place(data.place, out);
                out << " = ";
                if (data.rvalue) {
                    print_rvalue(*data.rvalue, out);
                } else {
                    out << "?";
                }
                break;
            }
            case MirStatement::StorageLive: {
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                out << "storage_live(_" << data.local << ")";
                break;
            }
            case MirStatement::StorageDead: {
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                out << "storage_dead(_" << data.local << ")";
                break;
            }
            case MirStatement::Nop:
                out << "nop";
                break;
            case MirStatement::Asm: {
                auto& data = std::get<MirStatement::AsmData>(stmt.data);
                out << "asm";
                if (data.is_must)
                    out << "!";
                out << "(\"" << escape_string(data.code) << "\"";
                // オペランド
                if (!data.operands.empty()) {
                    out << ", operands=[";
                    bool first = true;
                    for (const auto& op : data.operands) {
                        if (!first)
                            out << ", ";
                        first = false;
                        out << op.constraint;
                        if (op.is_constant) {
                            out << ":" << op.const_value;
                        } else {
                            out << ":_" << op.local_id;
                        }
                    }
                    out << "]";
                }
                // clobbers
                if (!data.clobbers.empty()) {
                    out << ", clobbers=[";
                    bool first = true;
                    for (const auto& c : data.clobbers) {
                        if (!first)
                            out << ", ";
                        first = false;
                        out << "\"" << c << "\"";
                    }
                    out << "]";
                }
                out << ")";
                break;
            }
            default:
                out << "<unknown_stmt>";
                break;
        }
    }

    // ========================================
    // 終端命令出力
    // ========================================
    void print_terminator(const MirTerminator& term, std::ostream& out) {
        switch (term.kind) {
            case MirTerminator::Goto: {
                auto& data = std::get<MirTerminator::GotoData>(term.data);
                out << "goto -> bb" << data.target;
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& data = std::get<MirTerminator::SwitchIntData>(term.data);
                out << "switchInt(";
                if (data.discriminant) {
                    print_operand(*data.discriminant, out);
                }
                out << ") -> [";
                for (const auto& [value, target] : data.targets) {
                    out << value << ": bb" << target << ", ";
                }
                out << "otherwise: bb" << data.otherwise << "]";
                break;
            }
            case MirTerminator::Return:
                out << "return";
                break;
            case MirTerminator::Unreachable:
                out << "unreachable";
                break;
            case MirTerminator::Call: {
                auto& data = std::get<MirTerminator::CallData>(term.data);
                if (data.destination) {
                    print_place(*data.destination, out);
                    out << " = ";
                }
                out << "call ";
                if (data.func) {
                    print_operand(*data.func, out);
                }
                out << "(";
                bool first = true;
                for (const auto& arg : data.args) {
                    if (!first)
                        out << ", ";
                    first = false;
                    if (arg)
                        print_operand(*arg, out);
                }
                out << ") -> bb" << data.success;
                if (data.unwind) {
                    out << " unwind bb" << *data.unwind;
                }
                if (data.is_tail_call) {
                    out << " [tail]";
                }
                break;
            }
            default:
                out << "<unknown_term>";
                break;
        }
    }

    // ========================================
    // Place出力
    // ========================================
    void print_place(const MirPlace& place, std::ostream& out) {
        out << "_" << place.local;
        for (const auto& proj : place.projections) {
            switch (proj.kind) {
                case ProjectionKind::Field:
                    out << "." << proj.field_id;
                    break;
                case ProjectionKind::Index:
                    out << "[_" << proj.index_local << "]";
                    break;
                case ProjectionKind::Deref:
                    out << ".*";
                    break;
            }
        }
    }

    // ========================================
    // Operand出力
    // ========================================
    void print_operand(const MirOperand& op, std::ostream& out) {
        switch (op.kind) {
            case MirOperand::Move:
                out << "move(";
                if (std::holds_alternative<MirPlace>(op.data)) {
                    print_place(std::get<MirPlace>(op.data), out);
                }
                out << ")";
                break;
            case MirOperand::Copy:
                out << "copy(";
                if (std::holds_alternative<MirPlace>(op.data)) {
                    print_place(std::get<MirPlace>(op.data), out);
                }
                out << ")";
                break;
            case MirOperand::Constant:
                if (std::holds_alternative<MirConstant>(op.data)) {
                    print_constant(std::get<MirConstant>(op.data), out);
                } else {
                    out << "const ?";
                }
                break;
            case MirOperand::FunctionRef:
                if (std::holds_alternative<std::string>(op.data)) {
                    out << "fn:" << std::get<std::string>(op.data);
                } else {
                    out << "fn:?";
                }
                break;
        }
    }

    // ========================================
    // Rvalue出力
    // ========================================
    void print_rvalue(const MirRvalue& rv, std::ostream& out) {
        switch (rv.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rv.data);
                if (data.operand)
                    print_operand(*data.operand, out);
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rv.data);
                if (data.lhs)
                    print_operand(*data.lhs, out);
                out << " " << binary_op_to_string(data.op) << " ";
                if (data.rhs)
                    print_operand(*data.rhs, out);
                break;
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rv.data);
                out << unary_op_to_string(data.op);
                if (data.operand)
                    print_operand(*data.operand, out);
                break;
            }
            case MirRvalue::Ref: {
                auto& data = std::get<MirRvalue::RefData>(rv.data);
                out << (data.borrow == BorrowKind::Mutable ? "&mut " : "&");
                print_place(data.place, out);
                break;
            }
            case MirRvalue::Aggregate: {
                auto& data = std::get<MirRvalue::AggregateData>(rv.data);
                out << aggregate_kind_to_string(data.kind) << "{";
                bool first = true;
                for (const auto& op : data.operands) {
                    if (!first)
                        out << ", ";
                    first = false;
                    if (op)
                        print_operand(*op, out);
                }
                out << "}";
                break;
            }
            case MirRvalue::Cast: {
                auto& data = std::get<MirRvalue::CastData>(rv.data);
                if (data.operand)
                    print_operand(*data.operand, out);
                out << " as " << safe_type_to_string(data.target_type);
                break;
            }
            case MirRvalue::FormatConvert: {
                auto& data = std::get<MirRvalue::FormatConvertData>(rv.data);
                out << "format(";
                if (data.operand)
                    print_operand(*data.operand, out);
                out << ", \"" << data.format_spec << "\")";
                break;
            }
            default:
                out << "<unknown_rvalue>";
                break;
        }
    }

    // ========================================
    // 定数出力
    // ========================================
    void print_constant(const MirConstant& c, std::ostream& out) {
        out << "const ";
        std::visit(
            [&out](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    out << "()";
                } else if constexpr (std::is_same_v<T, bool>) {
                    out << (arg ? "true" : "false");
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, double>) {
                    out << arg;
                } else if constexpr (std::is_same_v<T, char>) {
                    out << "'" << arg << "'";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    out << "\"" << arg << "\"";
                }
            },
            c.value);
    }

    // ========================================
    // ヘルパー関数
    // ========================================
    static std::string binary_op_to_string(MirBinaryOp op) {
        switch (op) {
            case MirBinaryOp::Add:
                return "+";
            case MirBinaryOp::Sub:
                return "-";
            case MirBinaryOp::Mul:
                return "*";
            case MirBinaryOp::Div:
                return "/";
            case MirBinaryOp::Mod:
                return "%";
            case MirBinaryOp::BitAnd:
                return "&";
            case MirBinaryOp::BitOr:
                return "|";
            case MirBinaryOp::BitXor:
                return "^";
            case MirBinaryOp::Shl:
                return "<<";
            case MirBinaryOp::Shr:
                return ">>";
            case MirBinaryOp::Eq:
                return "==";
            case MirBinaryOp::Ne:
                return "!=";
            case MirBinaryOp::Lt:
                return "<";
            case MirBinaryOp::Le:
                return "<=";
            case MirBinaryOp::Gt:
                return ">";
            case MirBinaryOp::Ge:
                return ">=";
            case MirBinaryOp::And:
                return "&&";
            case MirBinaryOp::Or:
                return "||";
            default:
                return "?op";
        }
    }

    static std::string unary_op_to_string(MirUnaryOp op) {
        switch (op) {
            case MirUnaryOp::Neg:
                return "-";
            case MirUnaryOp::Not:
                return "!";
            case MirUnaryOp::BitNot:
                return "~";
            default:
                return "?";
        }
    }

    static std::string aggregate_kind_to_string(const AggregateKind& kind) {
        switch (kind.type) {
            case AggregateKind::Array:
                return "array";
            case AggregateKind::Tuple:
                return "tuple";
            case AggregateKind::Struct:
                return kind.name.empty() ? "struct" : kind.name;
            default:
                return "aggregate";
        }
    }

    static std::string escape_string(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '\n':
                    result += "\\n";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                default:
                    result += c;
                    break;
            }
        }
        return result;
    }
};

}  // namespace cm::mir
