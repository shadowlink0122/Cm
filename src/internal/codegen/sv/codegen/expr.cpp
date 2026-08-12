// ============================================================
// SVコード生成: 式出力（Place・オペランド・右辺値）と式ツリー構築
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace cm::codegen::sv {

// === Place（左辺値）生成 ===

std::string SVCodeGen::emitPlace(const mir::MirPlace& place, const mir::MirFunction& func) {
    // ローカル変数名を取得
    std::string name;
    if (place.local < func.locals.size()) {
        name = func.locals[place.local].name;
        if (name.empty()) {
            name = "_" + std::to_string(place.local);
        }
    } else {
        name = "_" + std::to_string(place.local);
    }

    // self. プレフィックスを除去（SVでは不要）
    if (name.find("self.") == 0) {
        name = name.substr(5);
    }

    // importモジュール由来のグローバル参照は名前空間を剥がす（core::pc等）。
    // 宣言側（analyzeのポート/内部シグナル生成）はstrip_namespace済みの名前で出力するため、
    // 参照側が修飾名のままだとVerilatorで「Package/class for '::' not found」になる
    if (name.find("::") != std::string::npos) {
        name = strip_namespace(name);
    }

    // フィールド/インデックスアクセスの投影を適用
    hir::TypePtr current_type =
        (place.local < func.locals.size()) ? func.locals[place.local].type : nullptr;
    bool first_projection = true;
    for (const auto& proj : place.projections) {
        if (proj.kind == mir::ProjectionKind::Field) {
            // IOインスタンスのフィールドアクセス（io.field）はポート名へ写像する
            auto io_it = io_instance_fields_.find(name);
            if (first_projection && io_it != io_instance_fields_.end() &&
                proj.field_id < io_it->second.size()) {
                name = io_it->second[proj.field_id];
            } else {
                // データ構造体（typedef struct packed）のフィールドはメンバ名でアクセスする。
                // [index] はpacked structではビット選択になり誤った値を読むため使用しない
                const mir::MirStruct* struct_def = nullptr;
                if (current_type && current_type->kind == hir::TypeKind::Struct) {
                    auto struct_it = struct_defs_.find(current_type->name);
                    if (struct_it != struct_defs_.end()) {
                        struct_def = struct_it->second;
                    }
                }
                if (struct_def && proj.field_id < struct_def->fields.size()) {
                    name += "." + struct_def->fields[proj.field_id].name;
                } else {
                    name += "[" + std::to_string(proj.field_id) + "]";
                }
            }
        } else if (proj.kind == mir::ProjectionKind::Index) {
            // 配列インデックス: index_localの変数名で添字アクセス
            if (proj.index_local < func.locals.size()) {
                std::string idx_name;
                // 添字が単一定義テンポラリなら式ツリーをスプライスする（Phase 2）。
                // 文字列スライスの算術式に埋め込まれる場合があるため、原子でない式は括弧で囲む
                auto idx_tree = temp_trees_.find(proj.index_local);
                if (idx_tree != temp_trees_.end()) {
                    idx_name = idx_tree->second->to_string();
                    if (idx_tree->second->kind() != SVExpr::Kind::Atom) {
                        idx_name = "(" + idx_name + ")";
                    }
                } else {
                    idx_name = func.locals[proj.index_local].name;
                }
                // self. プレフィックスを除去
                if (idx_name.find("self.") == 0)
                    idx_name = idx_name.substr(5);

                if (current_type && current_type->kind == hir::TypeKind::String) {
                    int L = 0;
                    std::string base_name = name;
                    auto bracket_pos = base_name.find('[');
                    if (bracket_pos != std::string::npos) {
                        base_name = base_name.substr(0, bracket_pos);
                    }
                    auto it = global_string_lengths_.find(base_name);
                    if (it != global_string_lengths_.end()) {
                        L = it->second;
                    }
                    if (L == 0) {
                        L = getBitWidth(current_type) / 8;
                    }
                    if (L > 0) {
                        name =
                            name + "[(" + std::to_string(L - 1) + " - " + idx_name + ") * 8 +: 8]";
                    } else {
                        name += "[" + idx_name + "]";
                    }
                } else {
                    name += "[" + idx_name + "]";
                }
            }
        }
        // 次のイテレーションのために型を更新
        if (current_type) {
            if (proj.kind == mir::ProjectionKind::Index) {
                if (current_type->kind == hir::TypeKind::Array) {
                    current_type = current_type->element_type;
                } else if (current_type->kind == hir::TypeKind::String) {
                    current_type = nullptr;
                }
            } else if (proj.kind == mir::ProjectionKind::Field) {
                // 構造体フィールドの型を追跡する（ネストした構造体のメンバアクセスに必要）
                const mir::MirStruct* struct_def = nullptr;
                if (current_type->kind == hir::TypeKind::Struct) {
                    auto struct_it = struct_defs_.find(current_type->name);
                    if (struct_it != struct_defs_.end()) {
                        struct_def = struct_it->second;
                    }
                }
                if (struct_def && proj.field_id < struct_def->fields.size()) {
                    current_type = struct_def->fields[proj.field_id].type;
                } else {
                    current_type = nullptr;
                }
            }
        }
        first_projection = false;
    }

    return name;
}

// === オペランド生成 ===

std::string SVCodeGen::emitOperand(const mir::MirOperand& operand, const mir::MirFunction& func,
                                   int target_width) {
    switch (operand.kind) {
        case mir::MirOperand::Move:
        case mir::MirOperand::Copy: {
            // data は variant<MirPlace, MirConstant, string>
            const auto& place = std::get<mir::MirPlace>(operand.data);
            std::string result;
            // 単一定義テンポラリは式ツリーをスプライスする（Phase 2）。
            // 本関数の呼び出し元は if条件・配列添字・呼び出し引数などの
            // 自己区切りコンテキスト、または括弧を自前で管理するツリー経路のため、括弧なしの式文字列で安全
            bool spliced = false;
            if (place.projections.empty()) {
                auto it = temp_trees_.find(place.local);
                if (it != temp_trees_.end()) {
                    result = it->second->to_string();
                    spliced = true;
                }
            }
            if (!spliced) {
                result = emitPlace(place, func);
            }
            // target_width が指定されており、変数のビット幅が狭い場合はキャストを挿入
            // (int(32bit) + ushort(16bit) の混合演算での WIDTHEXPAND 警告防止)
            if (target_width > 0 && operand.type) {
                int var_width = getBitWidth(operand.type);
                if (var_width > 0 && var_width < target_width) {
                    result = std::to_string(target_width) + "'(" + result + ")";
                }
            }
            return result;
        }
        case mir::MirOperand::Constant: {
            const auto& constant = std::get<mir::MirConstant>(operand.data);
            return emitConstant(constant, operand.type, target_width);
        }
        default:
            return "/* unknown operand */";
    }
}

// === 右辺値生成 ===

// === 式ツリー構築（式ツリー化 Phase 1）===

// コンパイラ生成テンポラリの命名規約（_tNNN）に一致するか
static bool is_compiler_temp_name(const std::string& name) {
    return name.size() > 2 && name[0] == '_' && name[1] == 't' &&
           std::isdigit(static_cast<unsigned char>(name[2]));
}

// 関数内で1回だけ代入されるコンパイラ生成テンポラリを収集する。
// これらは定義時に式ツリーを記録し、使用箇所で構造的にインライン展開できる
void SVCodeGen::collectSingleDefTemps(const mir::MirFunction& func) {
    single_def_temps_.clear();
    temp_trees_.clear();

    std::unordered_map<mir::LocalId, int> def_counts;
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (ad.place.projections.empty()) {
                def_counts[ad.place.local]++;
            }
        }
        // Callの戻り先も定義としてカウント（ツリー化対象からは自然に外れる）
        if (block->terminator &&
            std::holds_alternative<mir::MirTerminator::CallData>(block->terminator->data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(block->terminator->data);
            if (cd.destination && cd.destination->projections.empty()) {
                def_counts[cd.destination->local]++;
            }
        }
    }

    for (const auto& [local, count] : def_counts) {
        if (count != 1 || local >= func.locals.size()) {
            continue;
        }
        if (is_compiler_temp_name(func.locals[local].name)) {
            single_def_temps_.insert(local);
        }
    }
}

// オペランドを式ツリーに変換する。
// 単一定義テンポラリへの参照は記録済みツリーをスプライスする
SVExprPtr SVCodeGen::buildOperandTree(const mir::MirOperand& op, const mir::MirFunction& func,
                                      int target_width) {
    if (op.kind == mir::MirOperand::Copy || op.kind == mir::MirOperand::Move) {
        if (const auto* place = std::get_if<mir::MirPlace>(&op.data)) {
            if (place->projections.empty()) {
                auto it = temp_trees_.find(place->local);
                if (it != temp_trees_.end()) {
                    // 幅拡張キャストが必要な場合はキャスト構文で原子化する
                    if (target_width > 0 && op.type) {
                        int var_width = getBitWidth(op.type);
                        if (var_width > 0 && var_width < target_width) {
                            return SVExpr::atom(std::to_string(target_width) + "'(" +
                                                it->second->to_string() + ")");
                        }
                    }
                    return it->second;
                }
            }
        }
    }
    // それ以外は既存のテキスト生成を原子として利用する（信号名・リテラル・投影付きplace等の出力ロジックを共有）
    return SVExpr::atom(emitOperand(op, func, target_width));
}

std::string SVCodeGen::emitRvalue(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                                  int target_width) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
            if (use_data.operand) {
                return emitOperand(*use_data.operand, func, target_width);
            }
            return "0";
        }

        case mir::MirRvalue::BinaryOp:
        case mir::MirRvalue::UnaryOp:
        case mir::MirRvalue::Cast:
            // 式ツリー経由で生成する（優先順位括弧はプリンタが構造から決定）
            return buildRvalueTree(rvalue, func, target_width)->to_string();

        case mir::MirRvalue::Ref: {
            // 借用はSVでは参照先の信号そのもの（連接lowering等で発生する）
            const auto& ref_data = std::get<mir::MirRvalue::RefData>(rvalue.data);
            return emitPlace(ref_data.place, func);
        }

        default: {
            // 静かなコメント化は「合法だが意味の違うSV」を生むため明示エラーにする（SV007）
            static const char* kRvalueNames[] = {"Use",       "BinaryOp", "UnaryOp",      "Ref",
                                                 "Aggregate", "Cast",     "FormatConvert"};
            int k = static_cast<int>(rvalue.kind);
            std::string kind_name = (k >= 0 && k <= 6) ? kRvalueNames[k] : std::to_string(k);
            throw std::runtime_error(
                i18n::msgf(i18n::MsgId::SvSv007UnsupportedExpressionOnThe, kind_name));
        }
    }
}

// rvalueを式ツリーに変換する。二項・単項演算はノードとして構築し、オペランド位置の単一定義テンポラリは構造的にインライン展開される。
// その他のrvalue（キャスト・Use等）は既存のテキスト生成を原子として扱う
SVExprPtr SVCodeGen::buildRvalueTree(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                                     int target_width) {
    switch (rvalue.kind) {
        case mir::MirRvalue::BinaryOp: {
            const auto& bin = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            // ビット幅推論はemitRvalueのテキスト経路と同一ロジック
            int lhs_tw = 0, rhs_tw = 0;
            if (bin.lhs && bin.rhs) {
                if (bin.lhs->kind == mir::MirOperand::Constant &&
                    bin.rhs->kind != mir::MirOperand::Constant && bin.rhs->type) {
                    lhs_tw = getBitWidth(bin.rhs->type);
                }
                if (bin.rhs->kind == mir::MirOperand::Constant &&
                    bin.lhs->kind != mir::MirOperand::Constant && bin.lhs->type) {
                    rhs_tw = getBitWidth(bin.lhs->type);
                }
            }
            if (target_width > 0) {
                if (lhs_tw == 0 && bin.lhs && bin.lhs->kind != mir::MirOperand::Constant)
                    lhs_tw = target_width;
                if (rhs_tw == 0 && bin.rhs && bin.rhs->kind != mir::MirOperand::Constant)
                    rhs_tw = target_width;
            }
            SVExprPtr lhs = bin.lhs ? buildOperandTree(*bin.lhs, func, lhs_tw) : SVExpr::atom("0");
            SVExprPtr rhs = bin.rhs ? buildOperandTree(*bin.rhs, func, rhs_tw) : SVExpr::atom("0");

            // 混合ビット幅の拡張キャスト（キャスト構文は自己完結のため原子化）
            int lhs_w = 0, rhs_w = 0;
            if (bin.lhs && bin.lhs->type)
                lhs_w = getBitWidth(bin.lhs->type);
            if (bin.rhs && bin.rhs->type)
                rhs_w = getBitWidth(bin.rhs->type);
            if (lhs_w > 0 && rhs_w > 0 && lhs_w != rhs_w) {
                int wider = std::max(lhs_w, rhs_w);
                if (lhs_w < rhs_w && bin.lhs->kind != mir::MirOperand::Constant) {
                    lhs = SVExpr::atom(std::to_string(wider) + "'(" + lhs->to_string() + ")");
                } else if (rhs_w < lhs_w && bin.rhs->kind != mir::MirOperand::Constant) {
                    rhs = SVExpr::atom(std::to_string(wider) + "'(" + rhs->to_string() + ")");
                }
            }

            std::string op;
            switch (bin.op) {
                case mir::MirBinaryOp::Add:
                    op = "+";
                    break;
                case mir::MirBinaryOp::Sub:
                    op = "-";
                    break;
                case mir::MirBinaryOp::Mul:
                    op = "*";
                    break;
                case mir::MirBinaryOp::Div:
                    op = "/";
                    break;
                case mir::MirBinaryOp::Mod:
                    op = "%";
                    break;
                case mir::MirBinaryOp::BitAnd:
                    op = "&";
                    break;
                case mir::MirBinaryOp::BitOr:
                    op = "|";
                    break;
                case mir::MirBinaryOp::BitXor:
                    op = "^";
                    break;
                case mir::MirBinaryOp::Shl:
                    op = "<<";
                    break;
                case mir::MirBinaryOp::Shr:
                    op = (bin.lhs && is_signed_type(resolve_operand_type(*bin.lhs, func))) ? ">>>"
                                                                                           : ">>";
                    break;
                case mir::MirBinaryOp::Eq:
                    op = "==";
                    break;
                case mir::MirBinaryOp::Ne:
                    op = "!=";
                    break;
                case mir::MirBinaryOp::Lt:
                    op = "<";
                    break;
                case mir::MirBinaryOp::Le:
                    op = "<=";
                    break;
                case mir::MirBinaryOp::Gt:
                    op = ">";
                    break;
                case mir::MirBinaryOp::Ge:
                    op = ">=";
                    break;
                case mir::MirBinaryOp::And:
                    op = "&&";
                    break;
                case mir::MirBinaryOp::Or:
                    op = "||";
                    break;
                default:
                    return SVExpr::atom(emitRvalue(rvalue, func, target_width));
            }
            return SVExpr::binary(op, std::move(lhs), std::move(rhs));
        }

        case mir::MirRvalue::UnaryOp: {
            const auto& unary = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            SVExprPtr operand =
                unary.operand ? buildOperandTree(*unary.operand, func) : SVExpr::atom("0");
            switch (unary.op) {
                case mir::MirUnaryOp::Neg:
                    return SVExpr::unary("-u", std::move(operand));
                case mir::MirUnaryOp::Not:
                case mir::MirUnaryOp::BitNot:
                    return SVExpr::unary("~", std::move(operand));
                default:
                    return operand;
            }
        }

        case mir::MirRvalue::Cast: {
            const auto& cast = std::get<mir::MirRvalue::CastData>(rvalue.data);
            if (!cast.operand) {
                return SVExpr::atom("0");
            }
            SVExprPtr operand_tree = buildOperandTree(*cast.operand, func);
            // 型名キャスト type'(expr)（SV-N8）: enum典型（typedef enum logic）・packed struct への
            // asキャストは、幅キャストでなくSVの型名キャストとして出力する（ビット→enum/構造体の再解釈を明示する）
            if (cast.target_type && !cast.target_type->name.empty()) {
                const std::string& tname = cast.target_type->name;
                const bool is_enum_typedef = enum_typedef_names_.count(tname) > 0;
                const bool is_packed_struct = cast.target_type->kind == hir::TypeKind::Struct &&
                                              struct_defs_.count(tname) > 0;
                if (is_enum_typedef || is_packed_struct) {
                    return SVExpr::atom(tname + "'(" + operand_tree->to_string() + ")");
                }
            }
            // 整数型への幅変更キャストはSVのサイズキャストとして明示的に出力する。
            // 出力しないと式の途中の縮小キャスト（例: (a + 300) as utiny）の切り捨てが失われ、計算結果そのものが変わってしまう
            int cast_w = is_integer_type(cast.target_type) ? getBitWidth(cast.target_type) : 0;
            if (cast_w > 0) {
                hir::TypePtr source_type = resolve_operand_type(*cast.operand, func);
                int source_w = is_integer_type(source_type) ? getBitWidth(source_type) : 0;
                bool need_size_cast = (source_w != cast_w);
                bool need_sign_cast =
                    source_type && is_signed_type(source_type) != is_signed_type(cast.target_type);
                if (need_size_cast || need_sign_cast) {
                    // キャスト構文は自己完結のため原子として扱う
                    std::string result = operand_tree->to_string();
                    if (need_size_cast) {
                        result = std::to_string(cast_w) + "'(" + result + ")";
                    }
                    if (need_sign_cast) {
                        result = (is_signed_type(cast.target_type) ? "$signed(" : "$unsigned(") +
                                 result + ")";
                    }
                    return SVExpr::atom(result);
                }
            }
            // ラッパー不要ならツリーをそのまま返す（構造・括弧情報を保持）
            return operand_tree;
        }

        default:
            // Use等は既存のテキスト生成に委譲する。
            // Useオペランドがツリー化済みテンポラリの場合はスプライスする
            if (rvalue.kind == mir::MirRvalue::Use) {
                const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
                if (use_data.operand) {
                    return buildOperandTree(*use_data.operand, func, target_width);
                }
            }
            return SVExpr::atom(emitRvalue(rvalue, func, target_width));
    }
}

}  // namespace cm::codegen::sv
