#pragma once

#include "mir_nodes.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace cm::mir {

// ============================================================
// MIRプリンター - デバッグ出力用
// ============================================================
class MirPrinter {
   public:
    // MIRプログラムを出力
    void print(const MirProgram& program, std::ostream& out = std::cout) {
        out << "===== MIR Program: " << program.filename << " =====\n\n";

        for (const auto& func : program.functions) {
            print_function(*func, out);
            out << "\n";
        }
    }

    // MIR関数を出力
    void print_function(const MirFunction& func, std::ostream& out = std::cout) {
        out << "fn " << func.name << "(";

        // 引数を出力
        bool first = true;
        for (LocalId arg_id : func.arg_locals) {
            if (!first)
                out << ", ";
            first = false;

            const auto& local = func.locals[arg_id];
            out << "_" << arg_id << ": " << type_to_string(local.type);
        }

        out << ") -> " << type_to_string(func.locals[func.return_local].type) << " {\n";

        // ローカル変数を出力
        out << "    // Locals:\n";
        for (const auto& local : func.locals) {
            out << "    // _" << local.id << ": " << type_to_string(local.type);
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
            print_block(*block, func, out);
        }

        out << "}\n";
    }

   private:
    // 基本ブロックを出力
    void print_block(const BasicBlock& block, const MirFunction& func, std::ostream& out) {
        // ブロックヘッダ
        out << "    bb" << block.id << ": {\n";

        // predecessorsを表示（デバッグ用）
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
            out << "        ";
            print_statement(*stmt, func, out);
            out << ";\n";
        }

        // 終端命令を出力
        if (block.terminator) {
            out << "        ";
            print_terminator(*block.terminator, out);
            out << ";\n";
        }

        out << "    }\n";
    }

    // 文を出力
    void print_statement(const MirStatement& stmt, const MirFunction& func, std::ostream& out) {
        switch (stmt.kind) {
            case MirStatement::Assign: {
                auto& data = std::get<MirStatement::AssignData>(stmt.data);
                print_place(data.place, func, out);
                out << " = ";
                if (data.rvalue) {
                    print_rvalue(*data.rvalue, func, out);
                }
                break;
            }
            case MirStatement::StorageLive: {
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                out << "StorageLive(_" << data.local << ")";
                break;
            }
            case MirStatement::StorageDead: {
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                out << "StorageDead(_" << data.local << ")";
                break;
            }
            case MirStatement::Nop:
                out << "nop";
                break;
        }
    }

    // 終端命令を出力
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
                print_operand(*data.discriminant, out);
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
                    print_place(*data.destination, MirFunction{}, out);
                    out << " = ";
                }
                out << "call ";
                print_operand(*data.func, out);
                out << "(";

                bool first = true;
                for (const auto& arg : data.args) {
                    if (!first)
                        out << ", ";
                    first = false;
                    print_operand(*arg, out);
                }

                out << ") -> bb" << data.success;
                if (data.unwind) {
                    out << " unwind bb" << *data.unwind;
                }
                break;
            }
        }
    }

    // Placeを出力
    void print_place(const MirPlace& place, const MirFunction& func, std::ostream& out) {
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

    // Operandを出力
    void print_operand(const MirOperand& op, std::ostream& out) {
        switch (op.kind) {
            case MirOperand::Move:
                out << "move ";
                if (auto* place = std::get_if<MirPlace>(&op.data)) {
                    print_place(*place, MirFunction{}, out);
                }
                break;
            case MirOperand::Copy:
                if (auto* place = std::get_if<MirPlace>(&op.data)) {
                    print_place(*place, MirFunction{}, out);
                }
                break;
            case MirOperand::Constant:
                if (auto* constant = std::get_if<MirConstant>(&op.data)) {
                    print_constant(*constant, out);
                }
                break;
        }
    }

    // Rvalueを出力
    void print_rvalue(const MirRvalue& rv, const MirFunction& func, std::ostream& out) {
        switch (rv.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rv.data);
                if (data.operand) {
                    print_operand(*data.operand, out);
                }
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rv.data);
                print_operand(*data.lhs, out);
                out << " " << binary_op_to_string(data.op) << " ";
                print_operand(*data.rhs, out);
                break;
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rv.data);
                out << unary_op_to_string(data.op);
                print_operand(*data.operand, out);
                break;
            }
            case MirRvalue::Ref: {
                auto& data = std::get<MirRvalue::RefData>(rv.data);
                out << (data.borrow == BorrowKind::Shared ? "&" : "&mut ");
                print_place(data.place, func, out);
                break;
            }
            case MirRvalue::Aggregate: {
                auto& data = std::get<MirRvalue::AggregateData>(rv.data);
                switch (data.kind.type) {
                    case AggregateKind::Array:
                        out << "[";
                        break;
                    case AggregateKind::Tuple:
                        out << "(";
                        break;
                    case AggregateKind::Struct:
                        out << data.kind.name << "{";
                        break;
                }

                bool first = true;
                for (const auto& op : data.operands) {
                    if (!first)
                        out << ", ";
                    first = false;
                    print_operand(*op, out);
                }

                switch (data.kind.type) {
                    case AggregateKind::Array:
                        out << "]";
                        break;
                    case AggregateKind::Tuple:
                        out << ")";
                        break;
                    case AggregateKind::Struct:
                        out << "}";
                        break;
                }
                break;
            }
            case MirRvalue::Cast: {
                auto& data = std::get<MirRvalue::CastData>(rv.data);
                print_operand(*data.operand, out);
                out << " as " << type_to_string(data.target_type);
                break;
            }
        }
    }

    // 定数を出力
    void print_constant(const MirConstant& constant, std::ostream& out) {
        if (std::holds_alternative<std::monostate>(constant.value)) {
            out << "()";
        } else if (auto* b = std::get_if<bool>(&constant.value)) {
            out << (*b ? "true" : "false");
        } else if (auto* i = std::get_if<int64_t>(&constant.value)) {
            out << "const " << *i;
        } else if (auto* d = std::get_if<double>(&constant.value)) {
            out << "const " << *d;
        } else if (auto* c = std::get_if<char>(&constant.value)) {
            out << "const '" << *c << "'";
        } else if (auto* s = std::get_if<std::string>(&constant.value)) {
            out << "const \"" << *s << "\"";
        }
    }

    // 二項演算子を文字列に
    std::string binary_op_to_string(MirBinaryOp op) {
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
                return "?";
        }
    }

    // 単項演算子を文字列に
    std::string unary_op_to_string(MirUnaryOp op) {
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

    // 型を文字列に（簡略版）
    std::string type_to_string(hir::TypePtr type) {
        if (!type)
            return "?";

        // 現時点では簡略化して型名を返す
        // TODO: 実際の型システムに合わせて実装
        if (type->name == "int")
            return "int";
        if (type->name == "uint")
            return "uint";
        if (type->name == "float")
            return "float";
        if (type->name == "double")
            return "double";
        if (type->name == "bool")
            return "bool";
        if (type->name == "char")
            return "char";
        if (type->name == "void")
            return "void";
        if (type->name == "string")
            return "string";

        return type->name.empty() ? "T" : type->name;
    }
};

}  // namespace cm::mir