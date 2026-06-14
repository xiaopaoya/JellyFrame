#pragma once

#include "core/dom.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wearweb {

struct ScriptRuntimeAccess;

struct ScriptEvaluationResult {
    bool ok = false;
    std::string value;
    std::string error;
};

class JerryScriptRuntime {
public:
    JerryScriptRuntime();
    ~JerryScriptRuntime();

    JerryScriptRuntime(const JerryScriptRuntime&) = delete;
    JerryScriptRuntime& operator=(const JerryScriptRuntime&) = delete;

    void bind_document(Node& document);
    ScriptEvaluationResult eval(std::string_view source, std::string_view source_name = {});

private:
    friend struct ScriptRuntimeAccess;

    bool initialized_ = false;
    std::vector<std::unique_ptr<Node>> detached_nodes_;

    Node& adopt_detached_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> release_detached_node(Node& node);
};

} // namespace wearweb
