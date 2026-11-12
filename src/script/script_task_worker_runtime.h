#pragma once

#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_input_dispatch.h"
#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_request_codec.h"
#include "render_core/budget.h"
#include "script/script_runtime.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

class TextSystem;

enum class ScriptTaskWorkerRuntimeInitStatus : std::uint8_t {
    Accepted,
    InvalidSession,
    InvalidConfiguration,
    ResourceMismatch,
    ParseRejected,
    BudgetRejected,
    Fatal,
    Stopped,
    AlreadyInitialized,
    Busy,
};

enum class ScriptTaskWorkerRuntimeFatalReason : std::uint8_t {
    None,
    ScriptException,
    ScriptWatchdog,
    ScriptFatal,
    BudgetExceeded,
    LoadFailure,
    ServiceUnsupported,
    ResourceMismatch,
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
    // Same-thread session admission; acquiring a pin never allocates.
    std::size_t max_active_operations = 64;
    ScriptRuntimeOptions script;
    Rect viewport;
    ScriptTaskAppFrameCodecOptions frame_codec;
    ScriptTaskInputCodecOptions input_codec;
    ScriptTaskServiceRequestCodecOptions service_request_codec;
    const TextSystem* text_system = nullptr;
    // Synchronous worker-thread preparation before document publication. A
    // refusal rejects initialization; user must outlive initialization.
    bool (*prepare_document)(Node&, void*) = nullptr;
    void* prepare_document_user = nullptr;
    // Synchronous worker-thread tooling observer. The caller keeps user alive
    // through the active call, including when the observer destroys this facade.
    ScriptDomMutationCallback mutation_observer = nullptr;
    void* mutation_observer_user = nullptr;
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    RuntimeFontCatalog::Handle text_catalog;
    RuntimeImageProvider::Handle image_provider;
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) && JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1
    // Bounds/statistics for Core's pass-local metadata memo. The worker binds
    // provider/context to image_provider before each synchronous layout call.
    // A supplied statistics pointer must outlive this worker and all rebuilds.
    NaturalImageSizingOptions natural_image_sizing;
#endif
#endif
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

// Worker-task-only runtime. The object owns all DOM, selected script backend and pipeline
// objects. Its public surface contains only value packets and scalar telemetry;
// no Node, realm, layer or renderer address can escape to the port adapter.
class ScriptTaskWorkerRuntime final {
public:
    explicit ScriptTaskWorkerRuntime(ScriptAppSession session,
                                     ScriptTaskWorkerRuntimeOptions options = {});
    ~ScriptTaskWorkerRuntime();

    ScriptTaskWorkerRuntime(const ScriptTaskWorkerRuntime&) = delete;
    ScriptTaskWorkerRuntime& operator=(const ScriptTaskWorkerRuntime&) = delete;

    // Initialization is single-admission: repeated calls return AlreadyInitialized;
    // reentry from any active worker/backend operation returns Busy. Neither
    // refusal replaces the existing document, VM, supervisor context or frames.
    ScriptTaskWorkerRuntimeInitStatus initialize(std::string_view html,
                                                 std::string_view css);
    ScriptTaskWorkerRuntimeInitStatus initialize(ScriptTaskSupervisor& supervisor,
                                                 std::string_view html,
                                                 std::string_view css);
    bool initialized() const;
    bool stopped() const;
    bool fatal() const;
    ScriptTaskWorkerRuntimeFatalRecord fatal_record() const;

    // Establish the worker's monotonic host clock before author scripts run.
    // The same value source must be passed to pump_callbacks later.
    void set_host_time_ms(std::uint64_t now_ms);

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

    void set_ui_accepted_frame_sequence(std::uint32_t sequence);
    ScriptTaskWorkerRuntimeTelemetry telemetry() const;
    bool service_gateway_available() const;

    // Revokes the session immediately; active calls/completion pins defer physical
    // VM/DOM/pipeline release. The supervisor must invalidate its session and
    // retire service/frame leases separately.
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
    struct State;
    class OperationPin;
    State* state_ = nullptr;
};

} // namespace jellyframe
