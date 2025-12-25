#pragma once
#include <string>

namespace cm {

enum class Target { Native, Wasm, JS, Web, Interpreter };

inline Target string_to_target(const std::string& s) {
    if (s == "wasm")
        return Target::Wasm;
    if (s == "js")
        return Target::JS;
    if (s == "web")
        return Target::Web;
    if (s == "intr")
        return Target::Interpreter;
    return Target::Native;
}

inline std::string target_to_string(Target t) {
    switch (t) {
        case Target::Wasm:
            return "wasm";
        case Target::JS:
            return "js";
        case Target::Web:
            return "web";
        case Target::Interpreter:
            return "intr";
        default:
            return "native";
    }
}

}  // namespace cm
