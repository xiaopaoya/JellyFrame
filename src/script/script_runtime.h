#pragma once

#include "render_core/dom.h"
#include "render_core/dom_owner.h"
#include "render_core/host.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

class AppRuntimeHost;
struct AppHostDataSnapshot;
struct AppHostDataAccessPolicy;
class AppLocalStorageShadow;
class AppLocationSnapshotMock;
class Canvas2DRegistry;
class NetworkFetchMock;
struct AppSystemEvent;
struct HostServiceCompletion;
struct LayoutBox;

enum class ScriptEvaluationStatus {
    Ok,
    Exception,
    ExecutionBudgetExceeded,
};

struct ScriptEvaluationResult {
    bool ok = false;
    std::string value;
    std::string error;
    ScriptEvaluationStatus status = ScriptEvaluationStatus::Exception;
};

enum class ScriptCallbackFailureStatus : std::uint8_t {
    None,
    Exception,
    ExecutionBudgetExceeded,
};

struct ScriptCallbackFailure {
    ScriptCallbackFailureStatus status = ScriptCallbackFailureStatus::None;
    std::string message;

    bool failed() const { return status != ScriptCallbackFailureStatus::None; }
};

enum class ScriptAudioEventKind {
    Ended,
    Error,
};

using ScriptAudioPlayCallback = bool (*)(void* user,
                                         std::uint32_t audio_id,
                                         std::string_view src,
                                         double volume,
                                         std::string* error);

using ScriptServiceRequestSubmitCallback = bool (*)(void* user,
                                                     std::uint8_t kind,
                                                     std::uint32_t request_id,
                                                     std::uint32_t client_token,
                                                     std::uint32_t input_handle,
                                                     std::uint8_t priority,
                                                     std::uint32_t timeout_ms);

using ScriptServiceRequestCancelCallback = bool (*)(void* user,
                                                     std::uint32_t request_id,
                                                     std::uint32_t client_token);

struct ScriptAudioHost {
    ScriptAudioPlayCallback play = nullptr;
    void* user = nullptr;
};

// Framework-owned limits for the selected script backend and its DOM bindings.
// The selected backend must enforce every applicable limit without converting
// callbacks or values through an intermediate runtime representation.
struct ScriptRuntimeOptions {
    std::size_t max_timers = 64;
    std::size_t max_event_listeners = 512;
    std::size_t max_detached_nodes = 256;
    std::size_t max_xml_http_requests = 16;
    std::size_t max_animation_frame_callbacks = 16;
    std::size_t max_audio_elements = 8;
    std::size_t max_geolocation_requests = 4;
    std::size_t max_route_history_entries = 16;
    std::uint32_t max_execution_check_count = 0;
    std::uint32_t execution_check_interval = 16;
    std::size_t max_layout_snapshot_nodes = 32;
    std::size_t max_form_data_entries = 32;
    std::size_t max_form_data_bytes = 4096;
    std::size_t max_dom_nodes = 4096;
    std::size_t max_dom_depth = 64;
    std::size_t max_attributes_per_element = 64;
    std::size_t max_dom_string_bytes = 512 * 1024;
    std::size_t max_service_callbacks = 16;
};

ScriptRuntimeOptions script_runtime_options_from_host_budgets(const HostBudgets& budgets);

struct ScriptRuntimeStatistics {
    std::size_t timer_count = 0;
    std::size_t animation_frame_callback_count = 0;
    std::size_t event_listener_count = 0;
    std::size_t xml_http_request_count = 0;
    std::size_t audio_element_count = 0;
    std::size_t geolocation_request_count = 0;
    std::size_t service_callback_count = 0;
    DetachedDomStatistics detached_nodes;
};

struct ScriptSystemState {
    bool document_hidden = false;
    bool navigator_online = false;
};

// The host invokes this contract only at lifecycle and frame boundaries. JS
// callbacks, wrappers and values remain native to the selected backend.
class ScriptRuntime {
public:
    virtual ~ScriptRuntime() = default;

    virtual void bind_document(Node& document) = 0;
    virtual void bind_app_services(AppRuntimeHost& host, NetworkFetchMock& network) = 0;
    virtual void bind_location_service(AppRuntimeHost& host, AppLocationSnapshotMock& location) = 0;
    virtual void bind_host_data_snapshot(const AppHostDataSnapshot& snapshot,
                                         const AppHostDataAccessPolicy& policy) = 0;
    virtual void bind_local_storage(AppLocalStorageShadow& storage) = 0;
    virtual void bind_audio_host(ScriptAudioHost host) = 0;
    virtual void bind_script_service_gateway(ScriptServiceRequestSubmitCallback submit,
                                             void* user,
                                             ScriptServiceRequestCancelCallback cancel = nullptr) = 0;
    virtual void clear_script_service_gateway() = 0;
    virtual bool dispatch_script_service_completion(std::uint32_t request_id,
                                                    std::uint32_t client_token,
                                                    std::uint8_t status,
                                                    std::uint32_t error_code,
                                                    const std::vector<std::uint8_t>& payload) = 0;
    virtual void bind_canvas_2d(Canvas2DRegistry& canvas) = 0;
    virtual void clear_app_services() = 0;
    virtual void clear_canvas_2d() = 0;
    virtual void capture_layout_snapshot(const LayoutBox& root,
                                         int client_offset_x = 0,
                                         int client_offset_y = 0) = 0;
    virtual ScriptEvaluationResult eval(std::string_view source,
                                        std::string_view source_name = {}) = 0;
    virtual ScriptCallbackFailure take_script_callback_failure() = 0;
    virtual bool script_callback_failed() const = 0;
    virtual bool execution_watchdog_supported() const = 0;
    virtual bool take_execution_watchdog_interrupt() = 0;
    virtual void set_host_time_ms(std::uint64_t now_ms) = 0;
    virtual void set_system_state(ScriptSystemState state) = 0;
    virtual ScriptSystemState system_state() const = 0;
    virtual bool dispatch_visibility_change() = 0;
    virtual bool request_modal_cancel() = 0;
    virtual Node* active_modal_dialog() const = 0;
    virtual bool dispatch_audio_event(std::uint32_t audio_id, ScriptAudioEventKind kind) = 0;
    virtual std::size_t pump_timers(std::uint64_t now_ms, std::size_t max_callbacks = 32) = 0;
    virtual std::size_t pump_animation_frame(std::uint64_t now_ms, std::size_t max_callbacks = 4) = 0;
    virtual bool handle_host_completion(const HostServiceCompletion& completion) = 0;
    virtual bool handle_system_event(const AppSystemEvent& event) = 0;
    virtual bool has_pending_timers() const = 0;
    virtual bool has_pending_animation_frames() const = 0;
    virtual std::uint64_t next_timer_due_ms() const = 0;
    virtual std::size_t detached_node_count() const = 0;
    virtual ScriptRuntimeStatistics statistics() const = 0;
};

// Exactly one backend is compiled into jellyframe_script. Factory selection is
// therefore fixed at configure time; it performs no per-call engine dispatch.
std::unique_ptr<ScriptRuntime> create_script_runtime(ScriptRuntimeOptions options = {});
std::unique_ptr<ScriptRuntime> create_script_runtime(const HostBudgets& budgets);
const char* selected_script_runtime_backend() noexcept;

} // namespace jellyframe
