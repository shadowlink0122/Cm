#pragma once

// ============================================================
// HIR型不変条件の監査（typed-hir-single-source 第1段）
// 「型検査後のHIRは全HirExpr.typeが非null・非error」の不変条件を機械的に検証する。
// CM_HIR_TYPE_AUDIT=1 でHIR lowering直後に走査し、違反をノード種別ごとに集計してstderrへ報告する。
// CM_HIR_TYPE_AUDIT=2 は違反ごとの文脈（関数名・種別・位置）も出力する
// ============================================================

#include "internal/hir/nodes.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace cm::hir {

struct TypeAuditResult {
    size_t total_exprs = 0;  // 走査したHirExprの総数
    size_t null_types = 0;   // type == nullptr の違反数
    size_t error_types = 0;  // type->is_error() の違反数
    // ノード種別ごとの違反数（"kind:関数名" 単位の集計は verbose 出力で行う）
    std::map<std::string, size_t> violations_by_kind;
    // 違反サンプル（"関数名 kind=種別 offset=span開始"）
    std::vector<std::string> samples;

    bool ok() const { return null_types == 0 && error_types == 0; }
};

// HIRプログラム全体を走査して型不変条件の違反を集計する
TypeAuditResult audit_types(const HirProgram& program);

// 集計結果をstderrへ報告する（verbose時はサンプルも出力）
void report_type_audit(const TypeAuditResult& result, const std::string& source_name, bool verbose);

}  // namespace cm::hir
