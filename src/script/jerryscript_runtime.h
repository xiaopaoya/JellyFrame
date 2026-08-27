#pragma once

#include "script/script_runtime.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

struct ScriptRuntimeAccess;
struct ScriptAnimationFrameCallback;
struct ScriptAudioElement;
struct ScriptEventListener;
struct ScriptGeolocationRequest;
struct ScriptCanvasGradient;
struct ScriptDialogState;
struct ScriptNodeBinding;
struct ScriptLocalStorageBinding;
struct ScriptServiceCallback;
struct ScriptTimer;
struct ScriptXmlHttpRequest;
class JerryScriptRuntime final : public ScriptRuntime {
public:
    explicit JerryScriptRuntime(ScriptRuntimeOptions options = {});
    explicit JerryScriptRuntime(const HostBudgets& budgets);
    ~JerryScriptRuntime() override;

    JerryScriptRuntime(const JerryScriptRuntime&) = delete;
    JerryScriptRuntime& operator=(const JerryScriptRuntime&) = delete;

    void bind_document(Node& document) override;
    void bind_app_services(AppRuntimeHost& host, NetworkFetchMock& network) override;
    void bind_location_service(AppRuntimeHost& host, AppLocationSnapshotMock& location) override;
    void bind_host_data_snapshot(const AppHostDataSnapshot& snapshot,
                                 const AppHostDataAccessPolicy& policy) override;
    void bind_local_storage(AppLocalStorageShadow& storage) override;
    void bind_audio_host(ScriptAudioHost host) override;
    void bind_script_service_gateway(ScriptServiceRequestSubmitCallback submit,
                                     void* user,
                                     ScriptServiceRequestCancelCallback cancel = nullptr) override;
    void clear_script_service_gateway() override;
    bool dispatch_script_service_completion(std::uint32_t request_id,
                                            std::uint32_t client_token,
                                            std::uint8_t status,
                                            std::uint32_t error_code,
                                            const std::vector<std::uint8_t>& payload) override;
    void bind_canvas_2d(Canvas2DRegistry& canvas) override;
    void clear_app_services() override;
    void clear_canvas_2d() override;
    // Records numeric client rects only for elements that requested measurement.
    void capture_layout_snapshot(const LayoutBox& root, int client_offset_x = 0, int client_offset_y = 0) override;
    ScriptEvaluationResult eval(std::string_view source, std::string_view source_name = {}) override;
    ScriptCallbackFailure take_script_callback_failure() override;
    bool script_callback_failed() const override { return callback_failure_.failed(); }
    bool execution_watchdog_supported() const override;
    bool take_execution_watchdog_interrupt() override;
    void set_host_time_ms(std::uint64_t now_ms) override;
    void set_system_state(ScriptSystemState state) override;
    ScriptSystemState system_state() const override;
    bool dispatch_visibility_change() override;
    // Hosts use this to route Escape/back through the dialog cancel policy.
    // Returns true when an active modal consumed the request, even if cancel was prevented.
    bool request_modal_cancel() override;
    Node* active_modal_dialog() const override;
    bool dispatch_audio_event(std::uint32_t audio_id, ScriptAudioEventKind kind) override;
    std::size_t pump_timers(std::uint64_t now_ms, std::size_t max_callbacks = 32) override;
    std::size_t pump_animation_frame(std::uint64_t now_ms, std::size_t max_callbacks = 4) override;
    bool handle_host_completion(const HostServiceCompletion& completion) override;
    bool handle_system_event(const AppSystemEvent& event) override;
    bool has_pending_timers() const override;
    bool has_pending_animation_frames() const override;
    std::uint64_t next_timer_due_ms() const override;
    std::size_t detached_node_count() const override;
    ScriptRuntimeStatistics statistics() const override;

private:
    friend struct ScriptRuntimeAccess;

    bool initialized_ = false;
    std::uint32_t next_timer_id_ = 1;
    std::uint32_t next_animation_frame_id_ = 1;
    std::uint32_t next_audio_id_ = 1;
    std::uint64_t current_time_ms_ = 0;
    DomOwner detached_nodes_;
    std::vector<std::unique_ptr<ScriptEventListener>> event_listeners_;
    std::vector<std::unique_ptr<ScriptTimer>> timers_;
    std::vector<std::unique_ptr<ScriptAnimationFrameCallback>> animation_frame_callbacks_;
    std::vector<std::unique_ptr<ScriptXmlHttpRequest>> xml_http_requests_;
    std::vector<std::unique_ptr<ScriptAudioElement>> audio_elements_;
    std::vector<std::unique_ptr<ScriptGeolocationRequest>> geolocation_requests_;
    std::vector<std::unique_ptr<ScriptServiceCallback>> service_callbacks_;
    std::vector<std::unique_ptr<ScriptDialogState>> dialog_states_;
    std::vector<ScriptNodeBinding*> node_bindings_;
    std::vector<ScriptNodeBinding*> layout_snapshot_bindings_;
    std::vector<ScriptLocalStorageBinding*> local_storage_bindings_;
    std::vector<Node*> observed_nodes_;
    ScriptRuntimeOptions options_;
    AppRuntimeHost* app_host_ = nullptr;
    NetworkFetchMock* network_fetch_ = nullptr;
    AppLocationSnapshotMock* location_snapshot_ = nullptr;
    const AppHostDataSnapshot* host_data_snapshot_ = nullptr;
    std::unique_ptr<AppHostDataAccessPolicy> host_data_access_policy_;
    AppLocalStorageShadow* local_storage_ = nullptr;
    Canvas2DRegistry* canvas_2d_ = nullptr;
    ScriptAudioHost audio_host_;
    ScriptServiceRequestSubmitCallback service_request_submit_ = nullptr;
    ScriptServiceRequestCancelCallback service_request_cancel_ = nullptr;
    void* service_request_user_ = nullptr;
    std::uint32_t next_service_request_id_ = 1;
    std::uint32_t next_service_client_token_ = 1;
    std::uint32_t service_client_token_ = 0;
    std::uint32_t bound_service_app_instance_id_ = 0;
    AppRuntimeHost* retired_service_host_ = nullptr;
    NetworkFetchMock* retired_network_fetch_ = nullptr;
    AppLocationSnapshotMock* retired_location_snapshot_ = nullptr;
    std::uint32_t retired_service_app_instance_id_ = 0;
    std::uint32_t retired_service_client_token_ = 0;
    Node* bound_document_ = nullptr;
    Node* active_modal_dialog_ = nullptr;
    ScriptSystemState system_state_;
    std::string route_fragment_;
    std::vector<std::string> route_history_;
    std::size_t route_history_index_ = 0;
    std::uint32_t execution_watchdog_depth_ = 0;
    std::uint32_t execution_watchdog_remaining_ = 0;
    bool execution_watchdog_interrupted_ = false;
    bool execution_watchdog_interrupt_pending_ = false;
    ScriptCallbackFailure callback_failure_;

    bool can_adopt_detached_node() const;
    bool can_adopt_detached_node(const Node& node) const;
    bool can_append_new_dom_node(const Node& parent, const Node& node) const;
    bool can_insert_dom_node(const Node& parent, const Node& child) const;
    bool can_append_dom_text(const Node& parent, const std::vector<std::string>& values) const;
    bool can_set_dom_text_content(const Node& node, std::string_view value) const;
    bool can_set_dom_attribute(const Node& node, std::string_view name, std::string_view value) const;
    DomStatistics dom_statistics() const;
    Node* adopt_detached_node(std::unique_ptr<Node> node);
    std::unique_ptr<Node> release_detached_node(Node& node);
    void add_script_event_listener(Node& node,
                                   std::string type,
                                   std::uint32_t callback_value,
                                   EventListenerOptions options);
    void remove_script_event_listener(Node& node,
                                      std::string type,
                                      std::uint32_t callback_value,
                                      EventListenerOptions options);
    void set_script_event_handler(Node& node, std::string type, std::uint32_t callback_value);
    std::uint32_t get_script_event_handler(Node& node, const std::string& type) const;
    void add_window_event_listener(std::string type,
                                   std::uint32_t callback_value,
                                   std::uint32_t target_value,
                                   EventListenerOptions options);
    void remove_window_event_listener(std::string type,
                                      std::uint32_t callback_value,
                                      EventListenerOptions options);
    void set_window_event_handler(std::string type, std::uint32_t callback_value, std::uint32_t target_value);
    std::uint32_t get_window_event_handler(const std::string& type) const;
    void dispatch_window_event(const char* type);
    std::string location_hash() const;
    void set_location_hash(std::string value);
    std::size_t route_history_length() const;
    void push_route_history(std::string value);
    void replace_route_history(std::string value);
    bool traverse_route_history(int delta);
    void prune_inactive_script_event_listeners();
    std::size_t active_script_event_listener_count() const;
    void clear_script_event_listeners();
    std::uint32_t add_timer(std::uint32_t callback_value, std::uint32_t delay_ms, bool repeat);
    void clear_timer(std::uint32_t id);
    void clear_timers();
    std::uint32_t add_animation_frame_callback(std::uint32_t callback_value);
    void cancel_animation_frame_callback(std::uint32_t id);
    void clear_animation_frame_callbacks();
    ScriptXmlHttpRequest* create_xml_http_request();
    void clear_xml_http_requests();
    ScriptAudioElement* create_audio_element(std::string src);
    void clear_audio_elements();
    ScriptGeolocationRequest* create_geolocation_request(std::uint32_t job_id,
                                                         std::uint32_t success_callback,
                                                         std::uint32_t error_callback);
    void clear_geolocation_requests();
    ScriptCanvasGradient* create_canvas_gradient(std::uint32_t gradient_id);
    void clear_canvas_gradients();
    ScriptDialogState* dialog_state_for(Node& node, bool create);
    const ScriptDialogState* dialog_state_for(const Node& node) const;
    bool show_modal_dialog(Node& node);
    void close_dialog(Node& node, std::string return_value, bool update_return_value);
    void set_dialog_open(Node& node, bool open);
    std::string dialog_return_value(const Node& node) const;
    void set_dialog_return_value(Node& node, std::string value);
    void clear_dialog_states();
    ScriptNodeBinding* bind_script_node(Node& node);
    Node* resolve_script_node(const ScriptNodeBinding& binding) const;
    void forget_script_node_binding(ScriptNodeBinding& binding);
    void invalidate_script_node(Node& node);
    void clear_script_node_bindings();
    ScriptLocalStorageBinding* bind_script_local_storage(AppLocalStorageShadow& storage);
    AppLocalStorageShadow* resolve_script_local_storage(const ScriptLocalStorageBinding& binding) const;
    void forget_script_local_storage_binding(ScriptLocalStorageBinding& binding);
    void clear_script_local_storage_bindings();
    void install_script_service_gateway();
    bool submit_script_service_request(std::uint8_t kind,
                                       std::uint32_t input_handle,
                                       std::uint8_t priority,
                                       std::uint32_t timeout_ms,
                                       std::uint32_t callback_value,
                                       std::uint32_t& request_id);
    bool cancel_script_service_request(std::uint32_t request_id);
    void record_script_callback_failure(ScriptCallbackFailureStatus status, std::string message);
};

} // namespace jellyframe
