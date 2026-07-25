#pragma once

#include "render_core/diagnostics.h"
#include "render_core/dom.h"

#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

using ScriptLoadCallback = bool (*)(std::string_view src, std::string& output, void* context);

struct DocumentScript {
    std::string source;
    std::string name;
    bool external = false;
};

struct DocumentScriptCollectionOptions {
    std::size_t max_scripts = 64;
    std::size_t max_total_source_bytes = 512 * 1024;
    DiagnosticSink* diagnostics = nullptr;
};

std::vector<DocumentScript> collect_classic_scripts(const Node& document);
std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context,
                                                    DiagnosticSink* diagnostics = nullptr);
std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context,
                                                    const DocumentScriptCollectionOptions& options);

} // namespace jellyframe
