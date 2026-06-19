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

std::vector<DocumentScript> collect_classic_scripts(const Node& document);
std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context,
                                                    DiagnosticSink* diagnostics = nullptr);

} // namespace jellyframe
