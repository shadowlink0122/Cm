#pragma once

#include "../../nodes.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <string>

namespace cm::mir::opt {

// ============================================================
// プログラム全体のデッドコード除去
// 未使用の関数・構造体を削除する
// ============================================================
class ProgramDeadCodeElimination {
   public:
    bool run(MirProgram& program);

   private:
    // 使用されている関数を収集
    void collect_used_functions(const MirProgram& program, std::set<std::string>& used);

    // 未使用関数を削除
    bool remove_unused_functions(MirProgram& program, const std::set<std::string>& used);

    // 使用されている構造体を収集
    void collect_used_structs(const MirProgram& program, std::set<std::string>& used,
                              const std::set<std::string>& used_functions);

    // 未使用構造体を削除
    bool remove_unused_structs(MirProgram& program, const std::set<std::string>& used);
};

}  // namespace cm::mir::opt
