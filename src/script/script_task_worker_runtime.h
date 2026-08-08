#pragma once

#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_input_dispatch.h"
#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_request_codec.h"
#include "render_core/budget.h"
#include "script/jerryscript_runtime.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

enum class ScriptTaskWorkerRuntimeInitStatus : std::uint8_t {
    Accepted,
    InvalidSession,
    ParseRejected,
    BudgetRejected,
    Stopped,
};

enum class ScriptTaskWorkerRuntimeFatalReason : std::uint8_t {
    None,
    ScriptException,
    ScriptWatchdog,
    ScriptFatal,
    BudgetExceeded,
    LoadFailure,
    ServiceUnsupported,
};

struct ScriptTaskWorkerRuntimeFatalRecord {
    ScriptAppSession session;
    ScriptTaskWorkerRuntimeFatalReason reason = ScriptTaskWorkerRuntimeFatalReason::None;
    std::uint32_t diagnostic_code = 0;
    std::uint32_t last_input_sequence = 0;
    std::uint32_t last_frame_sequence = 0;
    std::size_t internal_bytes = 0;
    std::size_t message_bytes = 0;
};

struct ScriptTaskWorkerRuntimeOptions {
    HostBudgets budgets;
    JerryScriptRuntimeOptions script;
    Rect viewport;
    ScriptTaskAppFrameCodecOptions frame_codec;
    ScriptTaskInputCodecOptions input_codec;
    ScriptTaskServiceRequestCodecOptions service_request_codec;
};

struct ScriptTaskWorkerRuntimeTelemetry {
    std::uint32_t input_packet_seq = 0;
    std::uint32_t js_mutation_seq = 0;
    std::uint32_t published_frame_seq = 0;
    std::uint32_t ui_accepted_frame_seq = 0;
    std::size_t input_packets = 0;
    std::size_t rejected_input_packets = 0;
    std::size_t frame_publish_attempts = 0;
    std::size_t frame_publish_rejections = 0;
    std::size_t service_packets_rejected = 0;
    std::size_t timer_callbacks = 0;
    std::size_t animation_frame_callbacks = 0;
    std::size_t fatal_publish_attempts = 0;
    std::size_t fatal_publish_rejections = 0;
};

struct ScriptTaskWorkerRuntimeStepResult {
    bool packet_consumed = false;
    bool input_accepted = false;
    bool service_completion_consumed = false;
    bool dom_mutated = false;
    bool frame_published = false;
    bool fatal = false;
    ScriptTaskInputDispatchStatus input_status = ScriptTaskInputDispatchStatus::PacketRejected;
    std::uint32_t service_request_id = 0;
    HostServiceStatus service_status = HostServiceStatus::Failed;
    ScriptTaskAppFramePublishResult frame;
};

// Worker-task-only runtime. The object owns all DOM, JerryScript and pipeline
// objects. Its public surface contains only value packets and scalar telemetry;
// no Node, realm, layer or renderer address can escape to the port adapter.
class ScriptTaskWorkerRuntime final {
public:
    explicit ScriptTaskWorkerRuntime(ScriptAppSession session,
                                     ScriptTaskWorkerRuntimeOptions options = {});
    ~ScriptTaskWorkerRuntime();

    ScriptTaskWorkerRuntime(const ScriptTaskWorkerRuntime&) = delete;
    ScriptTaskWorkerRuntime& operator=(const ScriptTaskWorkerRuntime&) = delete;

    ScriptTaskWorkerRuntimeInitStatus initialize(std::string_view html,
                                                 std::string_view css);
    bool initialized() const { return initialized_; }
    bool stopped() const { return stopped_; }
    bool fatal() const { return fatal_record_.reason != ScriptTaskWorkerRuntimeFatalReason::None; }
    ScriptTaskWorkerRuntimeFatalRecord fatal_record() const { return fatal_record_; }

    ScriptEvaluationResult eval(std::string_view source, std::string_view source_name = {});
    ScriptEvaluationResult eval_with_supervisor(ScriptTaskSupervisor& supervisor,
                                                std::string_view source,
                                                std::string_view source_name = {});

    // Publishes the complete initial/current frame. The frame is copied into
    // supervisor-owned sealed storage before the worker returns.
    ScriptTaskAppFramePublishResult publish_frame(ScriptTaskSupervisor& supervisor);

    // Consumes at most one input or copied service-completion packet. A fatal
    // result also attempts to publish the value-only fatal record.
    ScriptTaskWorkerRuntimeStepResult process_one(ScriptTaskSupervisor& supervisor);

    // Runs due callbacks inside the worker-owned realm. Callback side effects
    // are observed and published through the same value-frame path as input.
    // A fatal result also attempts to publish the value-only fatal record.
    ScriptTaskWorkerRuntimeStepResult pump_callbacks(std::uint64_t now_ms,
                                                     ScriptTaskSupervisor& supervisor,
                                                     std::size_t max_timer_callbacks = 32,
                                                     std::size_t max_animation_callbacks = 4);

    void set_ui_accepted_frame_sequence(std::uint32_t sequence) {
        telemetry_.ui_accepted_frame_seq = sequence;
    }
    ScriptTaskWorkerRuntimeTelemetry telemetry() const { return telemetry_; }
    bool service_gateway_available() const { return service_gateway_available_; }

    // Releases only worker-owned VM/DOM/pipeline state. The supervisor must
    // invalidate the session and retire service/frame leases separately.
    void stop();
    // Publishes the current fatal record once as a value packet. Retrying is
    // safe when the supervisor fatal mailbox is temporarily full.
    bool publish_fatal(ScriptTaskSupervisor& supervisor);

    // Test-only C-safe fatal injection. This deliberately simulates the
    // recoverable boundary used by a VM fatal without aborting, resetting a
    // task, or unwinding across C++ frames. It publishes only the bounded
    // supervisor-owned fatal value record.
    bool inject_c_safe_fatal_for_test(ScriptTaskSupervisor& supervisor,
                                      std::uint32_t diagnostic_code = 0);

private:
    class ActiveSupervisorScope {
    public:
        ActiveSupervisorScope(ScriptTaskWorkerRuntime& runtime, ScriptTaskSupervisor& supervisor);
        ~ActiveSupervisorScope();

    private:
        ScriptTaskWorkerRuntime& runtime_;
        ScriptTaskSupervisor* previous_ = nullptr;
    };

    static bool submit_service_request(void* user,
                                       std::uint8_t kind,
                                       std::uint32_t request_id,
                                       std::uint32_t client_token,
                                       std::uint32_t request_handle,
                                       std::uint8_t priority,
                                       std::uint32_t timeout_ms);
    static bool cancel_service_request(void* user,
                                        std::uint32_t request_id,
                                        std::uint32_t client_token);
    bool submit_service_request_impl(std::uint8_t kind,
                                     std::uint32_t request_id,
                                     std::uint32_t client_token,
                                     std::uint32_t request_handle,
                                     std::uint8_t priority,
                                     std::uint32_t timeout_ms);
    bool cancel_service_request_impl(std::uint32_t request_id,
                                     std::uint32_t client_token);
    void set_fatal(ScriptTaskWorkerRuntimeFatalReason reason, std::uint32_t diagnostic_code = 0);
    bool consume_callback_failure();
    bool rebuild_pipeline();
    bool has_dirty_document() const;

    ScriptAppSession session_;
    ScriptTaskWorkerRuntimeOptions options_;
    bool initialized_ = false;
    bool stopped_ = false;
    DomOwner document_owner_;
    std::unique_ptr<JerryScriptRuntime> runtime_;
    std::unique_ptr<StyleResolver> style_resolver_;
    RenderObjectPtr render_tree_;
    LayoutBoxPtr layout_tree_;
    LayerNodePtr layer_tree_;
    std::unique_ptr<InputController> input_controller_;
    ScriptTaskAppFramePublisher frame_publisher_;
    ScriptTaskWorkerRuntimeFatalRecord fatal_record_;
    ScriptTaskWorkerRuntimeTelemetry telemetry_;
    ScriptTaskSupervisor* active_supervisor_ = nullptr;
    std::uint32_t service_request_sequence_ = 1;
    bool service_gateway_available_ = false;
    bool fatal_published_ = false;
    std::uint32_t fatal_packet_sequence_ = 1;
};

} // namespace jellyframe
