#include "base.hpp"
#include "manager.hpp"

namespace cm::mir::opt {

void OptimizationPipeline::add_standard_passes(int optimization_level) {
    // create_standard_passes 関数を使用して標準パスを作成
    auto standard_passes = create_standard_passes(optimization_level);

    // 各パスを追加
    for (auto& pass : standard_passes) {
        passes.push_back(std::move(pass));
    }
}

}  // namespace cm::mir::opt