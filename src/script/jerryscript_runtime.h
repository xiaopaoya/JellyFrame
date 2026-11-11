#pragma once
#include "script/script_runtime.h"
#include <memory>
namespace jellyframe {
class JerryScriptRuntimeState;
class JerryScriptRuntime final : public ScriptRuntime {
public:
    explicit JerryScriptRuntime(ScriptRuntimeOptions options = {});
    explicit JerryScriptRuntime(const HostBudgets& budgets);
    ~JerryScriptRuntime() override;
    JerryScriptRuntime(const JerryScriptRuntime&) = delete;
    JerryScriptRuntime& operator=(const JerryScriptRuntime&) = delete;
    void request_stop() noexcept override;
    bool stop_requested() const noexcept override;
    bool bind_document(Node& document) override;
    void set_dom_mutation_observer(ScriptDomMutationCallback callback, void* user) override;
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
    bool script_callback_failed() const override;
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
    std::shared_ptr<JerryScriptRuntimeState> state_;
};
}
