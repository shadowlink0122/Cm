#include "diag_emitter.hpp"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace cm {

DiagnosticEmitter::DiagnosticEmitter(const std::string& code, const std::string& input_file,
                                     const SourceMap* source_map)
    : loc_mgr_(code, input_file), source_map_(source_map) {}

std::string DiagnosticEmitter::severity_label(Severity severity) {
    switch (severity) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        case Severity::Note:
            return "note";
    }
    return "error";
}

void DiagnosticEmitter::ensure_file_contents() {
    if (contents_loaded_) {
        return;
    }
    contents_loaded_ = true;
    if (!source_map_ || source_map_->empty()) {
        return;
    }
    // source_mapが参照する元ファイルとインポートチェーン中のファイルを収集して読み込む
    std::set<std::string> files_to_load;
    for (const auto& entry : *source_map_) {
        if (!entry.original_file.empty() && entry.original_file != "<unknown>" &&
            entry.original_file != "<generated>") {
            files_to_load.insert(entry.original_file);
        }
        if (!entry.import_chain.empty()) {
            std::string remaining = entry.import_chain;
            const std::string delimiter = " -> ";
            size_t pos;
            while ((pos = remaining.find(delimiter)) != std::string::npos) {
                std::string part = remaining.substr(0, pos);
                if (!part.empty() && part != "<unknown>" && part != "<generated>") {
                    files_to_load.insert(part);
                }
                remaining = remaining.substr(pos + delimiter.length());
            }
            if (!remaining.empty() && remaining != "<unknown>" && remaining != "<generated>") {
                files_to_load.insert(remaining);
            }
        }
    }
    for (const auto& file : files_to_load) {
        std::ifstream ifs(file);
        if (ifs) {
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            file_contents_[file] = buffer.str();
        }
    }
}

void DiagnosticEmitter::emit(const Diagnostic& diag, const std::string& label) {
    const std::string effective_label = label.empty() ? severity_label(diag.severity) : label;
    if (source_map_ && !source_map_->empty()) {
        ensure_file_contents();
        std::cerr << loc_mgr_.format_error_with_source_map(diag.span, diag.message, *source_map_,
                                                           file_contents_, effective_label);
    } else {
        std::cerr << loc_mgr_.format_error_location(diag.span,
                                                    effective_label + ": " + diag.message);
    }
}

void DiagnosticEmitter::emit_all(const std::vector<Diagnostic>& diags) {
    for (const auto& diag : diags) {
        emit(diag);
    }
}

}  // namespace cm
