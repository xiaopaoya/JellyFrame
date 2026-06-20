#pragma once

#include "render_core/dom.h"
#include "render_core/dom_owner.h"
#include "render_core/host.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

class AppRuntimeHost;
class AppLocalStorageShadow;
class NetworkFetchMock;
struct HostServiceCompletion;
struct ScriptRuntimeAccess;
struct ScriptEventListener;
struct ScriptTimer;
struct ScriptXmlHttpRequest;

struct ScriptEvaluationResult {
    bool ok = false;
    std::string value;
    std::string error;
};

struct JerryScriptRuntimeOptions {
    std::size_t max_timers = 64;
    std::size_t max_event_listeners = 512;
    std::size_t max_detached_nodes = 256;
    std::size_t max_xml_http_requests = 16;
};

struct ScriptRuntimeStatistics {
    std::size_t timer_count = 0;
    std::size_t event_listener_count = 0;
    std::size_t xml_http_request_count = 0;
    DetachedDomStatistics detached_nodes;
};

class JerryScriptRuntime {
public:
    explicit JerryScriptRuntime(JerryScriptRuntimeOptions options = {});
    explicit JerryScriptRuntime(const HostBudgets& budgets);
    ~JerryScriptRuntime();

    JerryScriptRuntime(const JerryScriptRuntime&) = delete;
    JerryScriptRuntime& operator=(const JerryScriptRuntime&) = delete;

    void bind_document(Node& document);
    void bind_app_services(AppRuntimeHost& host, NetworkFetchMock& network);
    void bind_local_storage(AppLocalStorageShadow& storage);
    void clear_app_services();
    ScriptEvaluationResult eval(std::string_view source, std::string_view source_name = {});
    void set_host_time_ms(std::uint64_t now_ms);
    std::size_t pump_timers(std::uint64_t now_ms, std::size_t max_callbacks = 32);
    bool handle_host_completion(const HostServiceCompletion& completion);
    bool has_pending_timers() const;
    std::uint64_t next_timer_due_ms() const;
    std::size_t detached_node_count() const;
    ScriptRuntimeStatistics statistics() const;

private:
    friend struct ScriptRuntimeAccess;

    bool initialized_ = false;
    std::uint32_t next_timer_id_ = 1;
    std::uint64_t current_time_ms_ = 0;
    DomOwner detached_nodes_;
    std::vector<std::unique_ptr<ScriptEventListener>> event_listeners_;
    std::vector<std::unique_ptr<ScriptTimer>> timers_;
    std::vector<std::unique_ptr<ScriptXmlHttpRequest>> xml_http_requests_;
    JerryScriptRuntimeOptions options_;
    AppRuntimeHost* app_host_ = nullptr;
    NetworkFetchMock* network_fetch_ = nullptr;
    AppLocalStorageShadow* local_storage_ = nullptr;

    bool can_adopt_detached_node() const;
    Node* adopt_detached_node(std::unique_ptr<Node> node);
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
    ScriptXmlHttpRequest* create_xml_http_request();
    void clear_xml_http_requests();
};

} // namespace jellyframe
