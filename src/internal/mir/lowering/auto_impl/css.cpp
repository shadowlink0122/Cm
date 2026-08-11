// ============================================================
// 自動実装 - CSS生成メソッド（css/to_css/is_css）
// ============================================================

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/lowering.hpp"
#include "internal/syntax/ast/typekey.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// Debug自動実装: "StructName { field1: value1, field2: value2, ... }"
std::string MirLowering::to_kebab_case(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

}  // namespace cm::mir
