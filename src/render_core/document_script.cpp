#include "render_core/document_script.h"

#include <cctype>
#include <vector>

namespace jellyframe {
namespace {

void append_text_descendants(const Node& node, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            output.append(current->text);
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

bool ascii_equal_case_insensitive(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
        std::tolower(static_cast<unsigned char>(right));
}

bool ascii_equals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!ascii_equal_case_insensitive(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

std::string trim_ascii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool is_classic_script_type(std::string_view raw_type) {
    const std::string trimmed = trim_ascii(raw_type);
    const std::size_t semicolon = trimmed.find(';');
    const std::string type = semicolon == std::string::npos
        ? trimmed
        : trim_ascii(std::string_view(trimmed).substr(0, semicolon));
    return type.empty() ||
        ascii_equals(type, "text/javascript") ||
        ascii_equals(type, "application/javascript") ||
        ascii_equals(type, "text/ecmascript") ||
        ascii_equals(type, "application/ecmascript") ||
        ascii_equals(type, "classic");
}

struct ScriptCollectionState {
    const DocumentScriptCollectionOptions& options;
    std::vector<DocumentScript> scripts;
    std::size_t source_bytes = 0;
    bool limit_reported = false;
};

void report_collection_limit(ScriptCollectionState& state, std::string_view detail) {
    if (state.limit_reported) {
        return;
    }
    report_diagnostic(state.options.diagnostics,
                      DiagnosticStage::Script,
                      DiagnosticSeverity::Warning,
                      "script-document-resource-limit",
                      "Document script collection reached its resource budget; later scripts were skipped",
                      detail);
    state.limit_reported = true;
}

bool append_script(ScriptCollectionState& state, DocumentScript script) {
    if (state.scripts.size() >= state.options.max_scripts) {
        report_collection_limit(state, "script count limit at " + script.name);
        return false;
    }
    if (state.source_bytes > state.options.max_total_source_bytes ||
        script.source.size() > state.options.max_total_source_bytes - state.source_bytes) {
        report_collection_limit(state, "script byte limit at " + script.name);
        return false;
    }
    state.source_bytes += script.source.size();
    state.scripts.push_back(std::move(script));
    return true;
}

void collect_scripts(const Node& node,
                     ScriptCollectionState& state,
                     ScriptLoadCallback load_script,
                     void* context,
                     DiagnosticSink* diagnostics) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "script") {
            if (!is_classic_script_type(current->attribute("type"))) {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::Script,
                                  DiagnosticSeverity::Warning,
                                  "script-type-unsupported",
                                  "Non-classic script type was skipped",
                                  current->attribute("type"));
                continue;
            }
            const std::string& src = current->attribute("src");
            if (!src.empty()) {
                if (load_script == nullptr) {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Script,
                                      DiagnosticSeverity::Warning,
                                      "script-loader-missing",
                                      "External script was skipped because no script loader is available",
                                      src);
                    continue;
                }
                std::string source;
                if (load_script(src, source, context) && !source.empty()) {
                    if (!append_script(state, DocumentScript{std::move(source), src, true})) {
                        break;
                    }
                } else {
                    report_diagnostic(diagnostics,
                                      DiagnosticStage::Script,
                                      DiagnosticSeverity::Warning,
                                      "script-load-failed",
                                      "External script could not be loaded or was empty",
                                      src);
                }
                continue;
            }
            std::string source;
            append_text_descendants(*current, source);
            if (!source.empty()) {
                if (!append_script(state, DocumentScript{std::move(source), "(inline script)", false})) {
                    break;
                }
            }
            continue;
        }

        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

} // namespace

std::vector<DocumentScript> collect_classic_scripts(const Node& document) {
    return collect_classic_scripts(document, nullptr, nullptr, nullptr);
}

std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context,
                                                    DiagnosticSink* diagnostics) {
    DocumentScriptCollectionOptions options;
    options.diagnostics = diagnostics;
    return collect_classic_scripts(document, load_script, context, options);
}

std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context,
                                                    const DocumentScriptCollectionOptions& options) {
    ScriptCollectionState state{options, {}, 0, false};
    collect_scripts(document, state, load_script, context, options.diagnostics);
    return std::move(state.scripts);
}

} // namespace jellyframe
