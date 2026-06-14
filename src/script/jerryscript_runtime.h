#pragma once

#include "core/dom.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace wearweb {

struct ScriptRuntimeAccess;
struct ScriptEventListener;
struct ScriptTimer;

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
    void set_host_time_ms(std::uint64_t now_ms);
    std::size_t pump_timers(std::uint64_t now_ms, std::size_t max_callbacks = 32);
    bool has_pending_timers() const;
    std::uint64_t next_timer_due_ms() const;

private:
    friend struct ScriptRuntimeAccess;

    bool initialized_ = false;
    std::uint32_t next_timer_id_ = 1;
    std::uint64_t current_time_ms_ = 0;
    std::vector<std::unique_ptr<Node>> detached_nodes_;
    std::vector<std::unique_ptr<ScriptEventListener>> event_listeners_;
    std::vector<std::unique_ptr<ScriptTimer>> timers_;

    Node& adopt_detached_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> release_detached_node(Node& node);
    void add_script_event_listener(Node& node,
                                   std::string type,
                                   std::uint32_t callback_value,
                                   EventListenerOptions options);
    void remove_script_event_listener(Node& node, std::string type, std::uint32_t callback_value);
    void clear_script_event_listeners();
    std::uint32_t add_timer(std::uint32_t callback_value, std::uint32_t delay_ms, bool repeat);
    void clear_timer(std::uint32_t id);
    void clear_timers();
};

} // namespace wearweb
