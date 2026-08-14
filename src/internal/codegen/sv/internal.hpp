#pragma once

// ============================================================
// SVバックエンド内部ヘルパー
// ============================================================
// SVコード生成の翻訳単位（codegen/analyze/validation/testbench）間で共有する小さなヘルパー。外部公開APIではない

#include "internal/base/format/text.hpp"
#include "internal/mir/analysis/dominators.hpp"
#include "internal/mir/nodes.hpp"

#include <cctype>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::codegen::sv {

using cm::text::contains_identifier;
using cm::text::replace_all;
using cm::text::strip_namespace;

// SV出力用の型名変換: Cmの修飾名（Outer::Inner）を一意なSV識別子（Outer__Inner）へ写像する。
// 最終セグメントへ落とすと異なるCm型（A::InnerとB::Inner）が同一SV型名へ衝突するため全セグメントを保持する。
// 実名にA__Bが存在して写像後も衝突する場合はtypedef出力側（analyzeDeclarations）が明示エラーにする
inline std::string sv_type_name(const std::string& name) {
    return replace_all(name, "::", "__");
}

// 符号付き整数型であるか判定
inline bool is_signed_type(const hir::TypePtr& type) {
    if (!type)
        return false;
    switch (type->kind) {
        case hir::TypeKind::Tiny:
        case hir::TypeKind::Short:
        case hir::TypeKind::Int:
        case hir::TypeKind::Long:
        case hir::TypeKind::ISize:
            return true;
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            return type->element_type && is_signed_type(type->element_type);
        default:
            return false;
    }
}

// オペランドの型を解決する
// （operand.typeが未設定の場合はローカル変数宣言の型を参照する）
inline hir::TypePtr resolve_operand_type(const mir::MirOperand& op, const mir::MirFunction& func) {
    if (op.type)
        return op.type;
    if (op.kind == mir::MirOperand::Copy || op.kind == mir::MirOperand::Move) {
        if (const auto* place = std::get_if<mir::MirPlace>(&op.data)) {
            if (place->projections.empty() && place->local < func.locals.size()) {
                return func.locals[place->local].type;
            }
        }
    }
    return nullptr;
}

// ブロックのターミネータが遷移しうる後続ブロックを列挙する
inline std::vector<size_t> terminator_targets(const mir::BasicBlock& bb) {
    std::vector<size_t> succs;
    if (!bb.terminator)
        return succs;
    switch (bb.terminator->kind) {
        case mir::MirTerminator::Goto:
            succs.push_back(std::get<mir::MirTerminator::GotoData>(bb.terminator->data).target);
            break;
        case mir::MirTerminator::SwitchInt: {
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
            for (const auto& [val, target] : sd.targets) {
                succs.push_back(target);
            }
            succs.push_back(sd.otherwise);
            break;
        }
        case mir::MirTerminator::Call: {
            const auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
            succs.push_back(cd.success);
            break;
        }
        default:
            break;
    }
    return succs;
}

// 関数内の全ループヘッダとそのラッチ（後方エッジの始点）を一括計算する。
// 後方エッジ = ヘッダが支配するブロックからヘッダへ入るエッジ。
// DominatorTreeの構築はO(ブロック数^2)級のため、関数ごとに1回だけ呼ぶこと
inline std::unordered_map<size_t, std::vector<size_t>> compute_loop_latches(
    const mir::MirFunction& func) {
    std::unordered_map<size_t, std::vector<size_t>> latches;
    if (func.basic_blocks.empty()) {
        return latches;
    }
    mir::DominatorTree domtree(func);
    for (size_t p = 0; p < func.basic_blocks.size(); ++p) {
        if (!func.basic_blocks[p])
            continue;
        for (size_t succ : terminator_targets(*func.basic_blocks[p])) {
            if (domtree.dominates(succ, p)) {
                latches[succ].push_back(p);
            }
        }
    }
    return latches;
}

// start が header の自然ループに属するか判定する。
// 「header を通らずにいずれかのラッチへ到達できる」ことが条件。
// （単純な到達可能性では、外側ループのバックエッジ経由でループ外からもヘッダに戻れてしまい誤判定する）
inline bool in_natural_loop(const mir::MirFunction& func, size_t start, size_t header,
                            const std::vector<size_t>& latches) {
    std::set<size_t> latch_set(latches.begin(), latches.end());
    std::set<size_t> seen;
    std::vector<size_t> work = {start};
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid == header)
            continue;  // ヘッダは通過しない
        if (latch_set.count(bid))
            return true;
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!seen.insert(bid).second)
            continue;
        for (size_t succ : terminator_targets(*func.basic_blocks[bid])) {
            work.push_back(succ);
        }
    }
    return false;
}

// 固定幅の整数型であるか判定（サイズキャスト出力の対象判定用）
inline bool is_integer_type(const hir::TypePtr& type) {
    if (!type)
        return false;
    switch (type->kind) {
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            return true;
        default:
            return false;
    }
}

// #[sv::tri(oe: "...", out: "...")] / #[sv::sync(clk: "...", src: "...", stages: N)]の key:value 引数を取り出す簡易パーサ
inline std::map<std::string, std::string> parseSvAttrKV(const std::string& attr,
                                                        const std::string& name) {
    std::map<std::string, std::string> kv;
    const std::string prefix = name + "(";
    if (attr.rfind(prefix, 0) != 0 || attr.empty() || attr.back() != ')') {
        return kv;
    }
    std::string body = attr.substr(prefix.size(), attr.size() - prefix.size() - 1);
    std::string cur;
    bool in_str = false;
    auto flush = [&]() {
        auto colon = cur.find(':');
        if (colon != std::string::npos) {
            kv[cur.substr(0, colon)] = cur.substr(colon + 1);
        }
        cur.clear();
    };
    for (char c : body) {
        if (c == '"') {
            in_str = !in_str;
            continue;
        }
        if (c == ',' && !in_str) {
            flush();
            continue;
        }
        if (c == ' ' && !in_str && cur.empty()) {
            continue;
        }
        cur += c;
    }
    flush();
    return kv;
}

}  // namespace cm::codegen::sv
