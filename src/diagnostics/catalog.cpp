// ============================================================
// DiagnosticCatalog 実装
// ============================================================

#include "catalog.hpp"

#include "definitions/errors.hpp"
#include "definitions/lints.hpp"
#include "definitions/warnings.hpp"

namespace cm {
namespace diagnostics {

std::string format_message(const std::string& tmpl, const std::vector<std::string>& args) {
    std::string result = tmpl;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string placeholder = "{" + std::to_string(i) + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), args[i]);
            pos += args[i].length();
        }
    }
    return result;
}

void DiagnosticCatalog::register_defaults() {
    definitions::register_errors(*this);
    definitions::register_warnings(*this);
    definitions::register_lints(*this);
}

}  // namespace diagnostics
}  // namespace cm
