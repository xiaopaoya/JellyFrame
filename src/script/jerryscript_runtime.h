#pragma once

#include "core/dom.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wearweb {

struct ScriptRuntimeAccess;
struct ScriptEventListener;

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
    std::vector<std::unique_ptr<ScriptEventListener>> event_listeners_;

    Node& adopt_detached_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> release_detached_node(Node& node);
    void add_script_event_listener(Node& node,
                                   std::string type,
                                   std::uint32_t callback_value,
                                   EventListenerOptions options);
    void remove_script_event_listener(Node& node, std::string type, std::uint32_t callback_value);
    void clear_script_event_listeners();
};

} // namespace wearweb
