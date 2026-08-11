// ============================================================
// MIR解析フェーズ: 関数解析ループ・enum/struct typedef・initialブロック
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cm::codegen::sv {

// 宣言生成フェーズ: 各関数のalwaysブロック化とenum/struct typedef・initialブロックの出力を行う
void SVCodeGen::analyzeDeclarations(const mir::MirProgram& program, SVModule& mod) {
    // 各関数を解析（import/export時の重複排除）
    std::set<std::string> emitted_function_names;
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        // 関数名のnamespace::フラット化
        std::string flat_name = func->name;
        flat_name = strip_namespace(flat_name);
        // 同名関数が既に出力済みならスキップ
        if (emitted_function_names.count(flat_name)) {
            continue;
        }
        emitted_function_names.insert(flat_name);
        analyzeFunction(*func, mod);
    }

    // enum → typedef enum logic 出力
    for (const auto& e : program.enums) {
        if (!e)
            continue;
        // Tagged Union（ペイロード付きenum）はSVでは直接変換しない
        if (e->is_tagged_union())
            continue;

        std::ostringstream ss;
        // ビット幅計算: 最大タグ値を表現できるビット数を算出（明示的なタグ値はメンバー数-1 より大きい場合があるため、メンバー数ではなく実際の値の最大から求める）
        int64_t max_tag = static_cast<int64_t>(e->members.size()) - 1;
        for (const auto& m : e->members) {
            if (m.tag_value > max_tag) {
                max_tag = m.tag_value;
            }
        }
        int bit_width = 1;
        int64_t val = max_tag;
        while (val > 1) {
            bit_width++;
            val >>= 1;
        }

        ss << "typedef enum logic";
        if (bit_width > 1) {
            ss << " [" << (bit_width - 1) << ":0]";
        }
        ss << " {\n";
        for (size_t i = 0; i < e->members.size(); ++i) {
            ss << "    " << e->members[i].name << " = " << bit_width << "'d"
               << e->members[i].tag_value;
            if (i + 1 < e->members.size())
                ss << ",";
            ss << "\n";
        }
        ss << "} " << e->name << ";";
        mod.type_declarations.push_back(ss.str());
    }

    // struct → typedef struct packed 出力（#[sv::packed]属性付きのみ）
    // extern struct はモジュール定義なので除外
    for (const auto& st : program.structs) {
        if (!st)
            continue;
        if (st->is_extern)
            continue;  // extern struct はtypedef出力しない
        // IO契約構造体（#[input]/#[output]/#[inout] フィールドを持つ）はモジュールのインターフェース宣言なのでデータ型として出力しない
        bool is_io_contract = false;
        for (const auto& f : st->fields) {
            for (const auto& a : f.attributes) {
                if (a == "input" || a == "output" || a == "inout") {
                    is_io_contract = true;
                    break;
                }
            }
            if (is_io_contract)
                break;
        }
        if (is_io_contract)
            continue;
        // #[sv::unpacked] 属性でpacked性を制御する（既定はpacked。SV-N8）
        bool is_unpacked = false;
        // #[sv::packed_union] は同一ビット幅の複数ビューを持つpacked unionとして出力する（SV-N6）
        bool is_packed_union = false;
        for (const auto& attr : st->attributes) {
            if (attr == "sv::unpacked" || attr == "verilog::unpacked") {
                is_unpacked = true;
            }
            if (attr == "sv::packed_union" || attr == "verilog::packed_union") {
                is_packed_union = true;
            }
        }
        if (is_packed_union) {
            // 全メンバのビット幅一致を検査する（不一致のpacked unionは合成不能）。
            // 幅はpacked struct参照を再帰合算して求める（program.structs内の宣言を辿る）
            std::function<int(const hir::TypePtr&)> bit_width_of =
                [&](const hir::TypePtr& t) -> int {
                if (!t) {
                    return -1;
                }
                switch (t->kind) {
                    case hir::TypeKind::Bool:
                    case hir::TypeKind::Bit:
                        return 1;
                    case hir::TypeKind::Tiny:
                    case hir::TypeKind::UTiny:
                        return 8;
                    case hir::TypeKind::Short:
                    case hir::TypeKind::UShort:
                        return 16;
                    case hir::TypeKind::Int:
                    case hir::TypeKind::UInt:
                        return 32;
                    case hir::TypeKind::Long:
                    case hir::TypeKind::ULong:
                    case hir::TypeKind::ISize:
                    case hir::TypeKind::USize:
                        return 64;
                    case hir::TypeKind::Array:
                        // bit[N]のみ幅確定（記号幅・非bit配列はpacked unionメンバとして非対応）
                        if (t->element_type && t->element_type->kind == hir::TypeKind::Bit &&
                            t->size_param_name.empty() && t->array_size) {
                            return static_cast<int>(*t->array_size);
                        }
                        return -1;
                    case hir::TypeKind::Struct: {
                        for (const auto& other : program.structs) {
                            if (other && other->name == t->name) {
                                int total = 0;
                                for (const auto& of : other->fields) {
                                    int w = bit_width_of(of.type);
                                    if (w < 0) {
                                        return -1;
                                    }
                                    total += w;
                                }
                                return total;
                            }
                        }
                        return -1;
                    }
                    default:
                        return -1;
                }
            };
            int union_width = -1;
            for (const auto& f : st->fields) {
                int w = bit_width_of(f.type);
                if (w < 0) {
                    throw std::runtime_error(i18n::msgf(
                        i18n::MsgId::SvSv009PackedUnionUnsupportedMember, st->name, f.name));
                }
                if (union_width < 0) {
                    union_width = w;
                } else if (w != union_width) {
                    throw std::runtime_error(
                        i18n::msgf(i18n::MsgId::SvSv009PackedUnionWidthMismatch, st->name, f.name,
                                   std::to_string(w), std::to_string(union_width)));
                }
            }
            std::ostringstream ss;
            ss << "typedef union packed {\n";
            for (const auto& f : st->fields) {
                ss << "    " << mapType(f.type) << " " << f.name << ";\n";
            }
            ss << "} " << st->name << ";";
            mod.type_declarations.push_back(ss.str());
            continue;
        }
        std::ostringstream ss;
        ss << (is_unpacked ? "typedef struct {\n" : "typedef struct packed {\n");
        for (const auto& f : st->fields) {
            ss << "    " << mapType(f.type) << " " << f.name << ";\n";
        }
        ss << "} " << st->name << ";";
        mod.type_declarations.push_back(ss.str());
    }

    // initial ブロックを処理
    for (const auto& init : program.initial_blocks) {
        if (!init)
            continue;
        std::ostringstream ss;
        ss << "initial begin\n";

        // HIR文をSVに変換
        for (const auto* stmt : init->hir_stmts) {
            if (stmt) {
                std::string sv_stmt = emitHirStmt(*stmt);
                if (!sv_stmt.empty()) {
                    ss << "    " << sv_stmt << "\n";
                }
            }
        }

        ss << "end\n";
        mod.initial_blocks.push_back(ss.str());
    }
}

}  // namespace cm::codegen::sv
