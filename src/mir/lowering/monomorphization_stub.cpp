// Temporary stub implementation for monomorphization
// TODO: Fix the actual implementation and remove this stub

#include "monomorphization.hpp"

namespace cm::mir {

void Monomorphization::scan_function_calls(
    MirFunction*, const std::string&,
    const std::unordered_map<std::string, const hir::HirFunction*>&,
    std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>&) {
    // Stub: do nothing for now
}

void Monomorphization::generate_specializations(
    MirProgram&, const std::unordered_map<std::string, const hir::HirFunction*>&,
    const std::unordered_map<std::string,
                             std::vector<std::tuple<std::string, size_t, std::string>>>&) {
    // Stub: do nothing for now
}

void Monomorphization::cleanup_generic_functions(
    MirProgram&,
    const std::unordered_map<std::string,
                             std::vector<std::tuple<std::string, size_t, std::string>>>&) {
    // Stub: do nothing for now
}

MirFunctionPtr Monomorphization::generate_specialized_function(const hir::HirFunction&,
                                                               const std::string&, size_t) {
    // Stub: return nullptr for now
    return nullptr;
}

}  // namespace cm::mir