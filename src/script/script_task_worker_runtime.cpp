#include "script_task_worker_runtime.h"

#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_fatal_codec.h"
#include "app_runtime/script_task_worker_inbox.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <tuple>
#include <utility>

namespace jellyframe {
namespace {

bool is_canonical_frame_version(std::uint8_t version) { return version == 9 || version == 10; }

template<class T, class = void>
struct HasBackgroundResourceResolver : std::false_type {};

template<class T>
struct HasBackgroundResourceResolver<T, std::void_t<decltype(
    std::declval<T&>().background_resource_resolver)>> : std::true_type {};

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
template<class T> struct FunctionArguments;
template<class R, class... Args> struct FunctionArguments<R (*)(Args...)> {
    using Tuple = std::tuple<Args...>;
};
using ImageResolveResult = std::remove_reference_t<std::remove_cv_t<
    std::tuple_element_t<3, typename FunctionArguments<ImageHandleResolveCallback>::Tuple>>>;

template<class Result>
bool set_image_resolve_result(Result& result, const ResolvedImageResource& resolved) {
    if constexpr (std::is_same<Result, std::uint32_t>::value) {
        result = resolved.image_handle;
        return result != 0;
    } else {
        result = resolved;
        return result.image_handle != 0;
    }
}

template<class Options>
void configure_background_resource_resolver(Options& options, const StyleResolver* resolver) {
    if constexpr (HasBackgroundResourceResolver<Options>::value)
        options.background_resource_resolver = resolver;
}
#endif

ScriptTaskAppFrameCodecOptions frame_options_from(const ScriptTaskWorkerRuntimeOptions& options) {
    ScriptTaskAppFrameCodecOptions result = options.frame_codec;
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    if (is_canonical_frame_version(result.version) && options.image_provider)
        result.image_provider_id = options.image_provider->id();
#endif
    if (result.max_commands == 0) result.max_commands = std::max<std::size_t>(1, options.budgets.max_display_commands);
    if (result.max_input_targets == 0) result.max_input_targets = std::max<std::size_t>(1, options.budgets.max_display_commands);
    if (result.max_payload_bytes == 0) result.max_payload_bytes = std::max<std::size_t>(1, options.budgets.max_resource_bytes);
    if (result.max_text_bytes == 0) result.max_text_bytes = result.max_payload_bytes;
    if (result.max_groups == 0) result.max_groups = std::max<std::size_t>(1, options.budgets.max_layers);
    if (result.max_group_depth == 0) result.max_group_depth = 32;
    return result;
}

ScriptTaskInputCodecOptions input_options_from(const ScriptTaskWorkerRuntimeOptions& options) {
    ScriptTaskInputCodecOptions result = options.input_codec;
    if (result.max_payload_bytes == 0) result.max_payload_bytes = std::max<std::size_t>(1, options.budgets.max_resource_bytes);
    if (result.max_text_bytes == 0) result.max_text_bytes = result.max_payload_bytes;
    return result;
}

} // namespace

struct ScriptTaskWorkerRuntime::State {
public:
    explicit State(ScriptAppSession session,
                                     ScriptTaskWorkerRuntimeOptions options = {});
    ~State();

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ScriptTaskWorkerRuntimeInitStatus initialize(std::string_view html,
                                                 std::string_view css);
    ScriptTaskWorkerRuntimeInitStatus initialize(ScriptTaskSupervisor& supervisor,
                                                 std::string_view html,
                                                 std::string_view css);
    bool initialized() const { return initialized_; }
    bool stopped() const { return stopped_; }
    bool fatal() const { return fatal_record_.reason != ScriptTaskWorkerRuntimeFatalReason::None; }
    ScriptTaskWorkerRuntimeFatalRecord fatal_record() const { return fatal_record_; }

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

public:
    class ActiveSupervisorScope {
    public:
        ActiveSupervisorScope(State& runtime, ScriptTaskSupervisor& supervisor);
        ~ActiveSupervisorScope();

    private:
        State& runtime_;
        ScriptTaskSupervisor* previous_ = nullptr;
    };

    static bool submit_service_request(void* user,
                                       std::uint8_t kind,
                                       std::uint32_t request_id,
                                       std::uint32_t client_token,
                                       std::uint32_t input_handle,
                                       std::uint8_t priority,
                                       std::uint32_t timeout_ms);
    static bool cancel_service_request(void* user,
                                        std::uint32_t request_id,
                                        std::uint32_t client_token);
    bool submit_service_request_impl(std::uint8_t kind,
                                     std::uint32_t request_id,
                                     std::uint32_t client_token,
                                     std::uint32_t input_handle,
                                     std::uint8_t priority,
                                     std::uint32_t timeout_ms);
    bool cancel_service_request_impl(std::uint32_t request_id,
                                     std::uint32_t client_token);
    void set_fatal(ScriptTaskWorkerRuntimeFatalReason reason, std::uint32_t diagnostic_code = 0);
    bool consume_callback_failure();
    bool rebuild_pipeline();
    bool has_dirty_document() const;
    bool supervisor_matches(const ScriptTaskSupervisor& supervisor) const;
    static bool retain_owner(void* context) noexcept {
        auto* state = static_cast<State*>(context);
        if (state->stopped_ || state->active_operations_ >= state->options_.max_active_operations)
            return false;
        ++state->active_operations_;
        return true;
    }
    static void release_owner(void* context) noexcept {
        static_cast<State*>(context)->release_operation();
    }
    static void observe_mutation(void* context, const Node& node,
                                 DomDirtyFlags flags, std::uint64_t generation) {
        auto* state = static_cast<State*>(context);
        if (!state->stopped_ && state->options_.mutation_observer)
            state->options_.mutation_observer(state->options_.mutation_observer_user,
                                               node, flags, generation);
    }
    ScriptTaskWorkerRuntimeInitStatus initialization_admission() const {
        if (stopped_) return ScriptTaskWorkerRuntimeInitStatus::Stopped;
        if (fatal()) return ScriptTaskWorkerRuntimeInitStatus::Fatal;
        if (active_operations_ != 0) return ScriptTaskWorkerRuntimeInitStatus::Busy;
        if (initialized_) return ScriptTaskWorkerRuntimeInitStatus::AlreadyInitialized;
        return ScriptTaskWorkerRuntimeInitStatus::Accepted;
    }
    void release_operation();
    void drain();
    std::size_t active_operations_ = 0;
    bool owner_alive_ = true;
    bool supervisor_validated_ = false;

    ScriptAppSession session_;
    ScriptTaskWorkerRuntimeOptions options_;
    bool initialized_ = false;
    bool stopped_ = false;
    DomOwner document_owner_;
    std::unique_ptr<ScriptRuntime> runtime_;
    std::unique_ptr<StyleResolver> style_resolver_;
    RenderObjectPtr render_tree_;
    LayoutBoxPtr layout_tree_;
    LayerNodePtr layer_tree_;
    std::unique_ptr<InputController> input_controller_;
    ScriptTaskAppFramePublisher frame_publisher_;
    ScriptTaskWorkerRuntimeFatalRecord fatal_record_;
    ScriptTaskWorkerRuntimeTelemetry telemetry_;
    // Tracks rebuilt output after DOM dirty flags are cleared for Core capture.
    bool pending_frame_publication_ = false;
    ScriptTaskSupervisor* active_supervisor_ = nullptr;
    std::uint32_t service_request_sequence_ = 1;
    bool service_gateway_available_ = false;
    bool fatal_published_ = false;
    std::uint32_t fatal_packet_sequence_ = 1;
};


ScriptTaskWorkerRuntime::State::ActiveSupervisorScope::ActiveSupervisorScope(
    State& runtime,
    ScriptTaskSupervisor& supervisor)
    : runtime_(runtime), previous_(runtime.active_supervisor_) {
    runtime_.active_supervisor_ = &supervisor;
}

ScriptTaskWorkerRuntime::State::ActiveSupervisorScope::~ActiveSupervisorScope() {
    runtime_.active_supervisor_ = previous_;
}

ScriptTaskWorkerRuntime::State::State(ScriptAppSession session,
                                                 ScriptTaskWorkerRuntimeOptions options)
    : session_(session),
      options_(std::move(options)),
      frame_publisher_(frame_options_from(options_)) {
    options_.frame_codec = frame_options_from(options_);
    options_.input_codec = input_options_from(options_);
    if (options_.service_request_codec.max_payload_bytes == 0) {
        options_.service_request_codec.max_payload_bytes = std::max<std::size_t>(64, options_.budgets.max_resource_bytes);
    }
    fatal_record_.session = session_;
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    if (is_canonical_frame_version(options_.frame_codec.version) && options_.image_provider)
        options_.frame_codec.image_provider_id = options_.image_provider->id();
#endif
}

ScriptTaskWorkerRuntime::State::~State() {
    stop();
}

void ScriptTaskWorkerRuntime::State::stop() {
    if (stopped_) return;
    stopped_ = true;
    initialized_ = false;
    service_gateway_available_ = false;
    active_supervisor_ = nullptr;
    // A backend stop may itself release completion pins. Retain a local pin
    // until request_stop has returned, even though new admission is revoked.
    ++active_operations_;
    if (runtime_) runtime_->request_stop();
    --active_operations_;
    if (active_operations_ == 0) drain();
}

void ScriptTaskWorkerRuntime::State::drain() {
    runtime_.reset();
    input_controller_.reset();
    layer_tree_.reset();
    layout_tree_.reset();
    render_tree_.reset();
    style_resolver_.reset();
    document_owner_.set_root(nullptr);
}

void ScriptTaskWorkerRuntime::State::release_operation() {
    if (--active_operations_ != 0) return;
    if (stopped_) drain();
    if (!owner_alive_) delete this;
}

bool ScriptTaskWorkerRuntime::State::supervisor_matches(const ScriptTaskSupervisor& supervisor) const {
    if (!session_.valid() || !supervisor.accepts(session_)) return false;
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    if (is_canonical_frame_version(options_.frame_codec.version)) {
        const auto catalog = supervisor.text_catalog(session_);
        const auto images = supervisor.image_provider(session_);
        return options_.text_catalog && catalog == options_.text_catalog &&
            images == options_.image_provider && (!images || images->valid());
    }
#else
    if (is_canonical_frame_version(options_.frame_codec.version)) return false;
#endif
    return true;
}

bool ScriptTaskWorkerRuntime::State::publish_fatal(ScriptTaskSupervisor& supervisor) {
    if (stopped_ || !fatal()) {
        return false;
    }
    if (fatal_published_) {
        return true;
    }
    ++telemetry_.fatal_publish_attempts;
    const ScriptTaskFatalRecord record{
        fatal_record_.session,
        static_cast<std::uint8_t>(fatal_record_.reason),
        fatal_record_.diagnostic_code,
        fatal_record_.last_input_sequence,
        fatal_record_.last_frame_sequence,
        static_cast<std::uint64_t>(fatal_record_.internal_bytes),
        static_cast<std::uint32_t>(std::min<std::size_t>(
            fatal_record_.message_bytes, std::numeric_limits<std::uint32_t>::max())),
    };
    const ScriptTaskMailboxPostStatus posted = post_script_task_fatal(
        supervisor, record, fatal_packet_sequence_,
        {40});
    if (posted != ScriptTaskMailboxPostStatus::Accepted) {
        ++telemetry_.fatal_publish_rejections;
        return false;
    }
    fatal_published_ = true;
    fatal_packet_sequence_ = fatal_packet_sequence_ == std::numeric_limits<std::uint32_t>::max()
        ? 1
        : fatal_packet_sequence_ + 1;
    return true;
}

bool ScriptTaskWorkerRuntime::State::inject_c_safe_fatal_for_test(
    ScriptTaskSupervisor& supervisor,
    std::uint32_t diagnostic_code) {
    if (!initialized_ || stopped_ || fatal()) {
        return false;
    }
    ActiveSupervisorScope scope(*this, supervisor);
    set_fatal(ScriptTaskWorkerRuntimeFatalReason::ScriptFatal, diagnostic_code);
    return publish_fatal(supervisor);
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::State::initialize(std::string_view html,
                                                                       std::string_view css) {
    if (stopped_) return ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (fatal()) return ScriptTaskWorkerRuntimeInitStatus::Fatal;
    if (initialized_) return ScriptTaskWorkerRuntimeInitStatus::AlreadyInitialized;
    if (!session_.valid()) return ScriptTaskWorkerRuntimeInitStatus::InvalidSession;
    if (is_canonical_frame_version(options_.frame_codec.version)) {
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
        if (!options_.text_catalog ||
            (options_.image_provider && !options_.image_provider->valid())) {
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
            return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
        }
        if (!supervisor_validated_) {
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
            return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
        }
#else
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
#endif
    }
    if (html.empty() || html.size() > options_.budgets.max_resource_bytes ||
        css.size() > options_.budgets.max_resource_bytes || options_.viewport.width <= 0 ||
        options_.viewport.height <= 0 || !framebuffer_size_fits_budget(options_.viewport.width,
                                                                        options_.viewport.height,
                                                                        options_.budgets)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    }

    HtmlParser html_parser;
    HtmlParserOptions html_options = html_parser_options_from_budgets(options_.budgets);
    HtmlParseResult parsed = html_parser.parse_with_diagnostics(std::string(html), html_options);
    if (!parsed.document || parsed.diagnostics != HtmlParserDiagnosticNone) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::LoadFailure);
        return ScriptTaskWorkerRuntimeInitStatus::ParseRejected;
    }

    CssParser css_parser;
    Stylesheet stylesheet = css_parser.parse(
        std::string(css),
        css_parser_options_from_budgets(options_.budgets, options_.viewport.width, options_.viewport.height));
    document_owner_.set_root(std::move(parsed.document));
    options_.script.retain_owner = retain_owner;
    options_.script.release_owner = release_owner;
    options_.script.owner_context = this;
    runtime_ = create_script_runtime(options_.script);
    runtime_->set_dom_mutation_observer(observe_mutation, this);
    if (options_.script.max_execution_check_count != 0 &&
        !runtime_->execution_watchdog_supported()) {
        runtime_.reset();
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog);
        return ScriptTaskWorkerRuntimeInitStatus::Fatal;
    }
    if (!runtime_->bind_document(*document_owner_.root())) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::LoadFailure);
        return ScriptTaskWorkerRuntimeInitStatus::Fatal;
    }
    runtime_->bind_script_service_gateway(submit_service_request, this, cancel_service_request);
    service_gateway_available_ = true;
    style_resolver_ = std::make_unique<StyleResolver>(std::move(stylesheet));
    if (!rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    }
    if (stopped_) return ScriptTaskWorkerRuntimeInitStatus::Stopped;
    initialized_ = true;
    return ScriptTaskWorkerRuntimeInitStatus::Accepted;
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::State::initialize(
    ScriptTaskSupervisor& supervisor, std::string_view html, std::string_view css) {
    if (stopped_) return ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (fatal()) return ScriptTaskWorkerRuntimeInitStatus::Fatal;
    if (initialized_) return ScriptTaskWorkerRuntimeInitStatus::AlreadyInitialized;
    if (is_canonical_frame_version(options_.frame_codec.version)) {
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
        if (!options_.text_catalog || (options_.image_provider && !options_.image_provider->valid())) {
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
            return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
        }
#else
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
#endif
    }
    if (!supervisor_matches(supervisor)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return ScriptTaskWorkerRuntimeInitStatus::ResourceMismatch;
    }
    supervisor_validated_ = true;
    return initialize(html, css);
}

void ScriptTaskWorkerRuntime::State::set_host_time_ms(std::uint64_t now_ms) {
    if (runtime_ != nullptr) {
        runtime_->set_host_time_ms(now_ms);
    }
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::State::eval(std::string_view source, std::string_view source_name) {
    if (!initialized_ || fatal()) {
        return {false, {}, "worker runtime is not active", ScriptEvaluationStatus::Exception};
    }
    if (is_canonical_frame_version(options_.frame_codec.version) && active_supervisor_ == nullptr) {
        return {false, {}, "canonical frame evaluation requires supervisor validation", ScriptEvaluationStatus::Exception};
    }
    ScriptEvaluationResult result = runtime_->eval(source, source_name);
    if (!result.ok) {
        set_fatal(result.status == ScriptEvaluationStatus::ExecutionBudgetExceeded
                      ? ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog
                      : ScriptTaskWorkerRuntimeFatalReason::ScriptException);
        (void) runtime_->take_script_callback_failure();
    } else if (stopped_) {
        return result;
    } else if (consume_callback_failure()) {
        // A callback failure is terminal even when the selected backend returned a
        // successful value for the surrounding worker operation.
    } else if (has_dirty_document() && !rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
    }
    return result;
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::State::eval_with_supervisor(
    ScriptTaskSupervisor& supervisor,
    std::string_view source,
    std::string_view source_name) {
    ActiveSupervisorScope scope(*this, supervisor);
    if (!supervisor_matches(supervisor)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        (void)publish_fatal(supervisor);
        return {false, {}, "worker session resource mismatch", ScriptEvaluationStatus::Exception};
    }
    ScriptEvaluationResult result = eval(source, source_name);
    if (fatal()) {
        (void)publish_fatal(supervisor);
    }
    return result;
}

ScriptTaskAppFramePublishResult ScriptTaskWorkerRuntime::State::publish_frame(ScriptTaskSupervisor& supervisor) {
    ScriptTaskAppFramePublishResult result;
    ++telemetry_.frame_publish_attempts;
    if (!initialized_ || fatal() || !layer_tree_ || !supervisor_matches(supervisor)) {
        if (initialized_ && !fatal() && !supervisor_matches(supervisor))
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        ++telemetry_.frame_publish_rejections;
        if (fatal()) (void)publish_fatal(supervisor);
        return result;
    }
    ScriptTaskAppFrame frame;
    if (is_canonical_frame_version(options_.frame_codec.version)) {
        const auto status = make_script_task_group_frame(*layer_tree_, options_.viewport,
                                                        options_.frame_codec, frame);
        if (status != ScriptTaskAppFrameCodecStatus::Accepted) {
            ++telemetry_.frame_publish_rejections;
            return result;
        }
    } else {
        frame = make_script_task_app_frame(
            *layer_tree_, options_.viewport, {}, options_.frame_codec.version >= 2);
    }
    result = frame_publisher_.publish(supervisor, session_, frame);
    if (result.accepted()) {
        ++telemetry_.published_frame_seq;
    } else {
        ++telemetry_.frame_publish_rejections;
    }
    if (result.accepted()) {
        clear_dirty_flags(*document_owner_.root());
        pending_frame_publication_ = false;
    }
    return result;
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::State::process_one(ScriptTaskSupervisor& supervisor) {
    ScriptTaskWorkerRuntimeStepResult result;
    const auto finish = [this, &supervisor](ScriptTaskWorkerRuntimeStepResult value) {
        if (fatal()) {
            value.fatal = true;
            (void)publish_fatal(supervisor);
        }
        return value;
    };
    if (!initialized_ || fatal()) {
        return finish(result);
    }
    if (!supervisor_matches(supervisor)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return finish(result);
    }
    ScriptTaskPacket packet;
    if (!supervisor.take_worker_packet(session_, packet)) {
        if (has_dirty_document()) {
            result.frame = publish_frame(supervisor);
            result.frame_published = result.frame.accepted();
        }
        return finish(result);
    }
    result.packet_consumed = true;
    if (packet.session != session_) {
        ++telemetry_.rejected_input_packets;
        result.input_status = ScriptTaskInputDispatchStatus::PacketRejected;
        return result;
    }
    ActiveSupervisorScope scope(*this, supervisor);
    if (packet.kind == ScriptTaskPacketKind::ServiceCompletion) {
        result.service_completion_consumed = true;
        const DomDirtyFlags before = subtree_dirty_flags(*document_owner_.root());
        ScriptTaskServiceCompletion completion;
        if (!decode_script_task_service_completion(packet.payload, completion)) {
            ++telemetry_.service_packets_rejected;
            return result;
        }
        result.service_request_id = completion.request_id;
        std::vector<std::uint8_t> payload;
        const ScriptTaskServicePayloadTakeStatus payload_status =
            take_script_task_service_payload(supervisor, session_, completion, payload);
        if (payload_status == ScriptTaskServicePayloadTakeStatus::LeaseRejected) {
            ++telemetry_.service_packets_rejected;
            completion.status = HostServiceStatus::Failed;
            completion.error_code = static_cast<std::uint32_t>(
                ScriptTaskServicePayloadErrorCode::LeaseRejected);
            completion.byte_count = 0;
        }
        result.service_status = completion.status;
        if (!runtime_->dispatch_script_service_completion(completion.request_id,
                                                           completion.client_token,
                                                           static_cast<std::uint8_t>(completion.status),
                                                           completion.error_code,
                                                           payload)) {
            ++telemetry_.service_packets_rejected;
            return result;
        }
        if (stopped_) return result;
        if (consume_callback_failure()) {
            return finish(result);
        }
        const DomDirtyFlags after = subtree_dirty_flags(*document_owner_.root());
        result.dom_mutated = before != after || after != DomDirtyNone;
        if (result.dom_mutated) {
            ++telemetry_.js_mutation_seq;
            const Node* hovered = input_controller_->hovered_node();
            const Node* active = input_controller_->active_node();
            const Node* focused = input_controller_->focused_node();
            style_resolver_->set_interaction_state(hovered, active, focused);
            if (!rebuild_pipeline()) {
                set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
                return finish(result);
            }
            result.frame = publish_frame(supervisor);
            result.frame_published = result.frame.accepted();
        }
        return finish(result);
    }
    if (packet.kind != ScriptTaskPacketKind::Input) {
        ++telemetry_.rejected_input_packets;
        return result;
    }
    ScriptTaskInputEvent input;
    if (decode_script_task_input(packet.payload, options_.input_codec, input) != ScriptTaskInputCodecStatus::Accepted) {
        ++telemetry_.rejected_input_packets;
        return result;
    }
    ++telemetry_.input_packets;
    telemetry_.input_packet_seq = packet.sequence;
    const DomDirtyFlags before = subtree_dirty_flags(*document_owner_.root());
    result.input_status = dispatch_script_task_input(*input_controller_, input).status;
    result.input_accepted = result.input_status == ScriptTaskInputDispatchStatus::Accepted;
    if (stopped_) return result;
    if (consume_callback_failure()) {
        return finish(result);
    }
    const DomDirtyFlags after = subtree_dirty_flags(*document_owner_.root());
    result.dom_mutated = before != after || after != DomDirtyNone;
    if (result.dom_mutated) {
        ++telemetry_.js_mutation_seq;
        const Node* hovered = input_controller_->hovered_node();
        const Node* active = input_controller_->active_node();
        const Node* focused = input_controller_->focused_node();
        style_resolver_->set_interaction_state(hovered, active, focused);
        if (!rebuild_pipeline()) {
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
            return finish(result);
        }
        result.frame = publish_frame(supervisor);
        result.frame_published = result.frame.accepted();
    }
    return finish(result);
}

bool ScriptTaskWorkerRuntime::State::submit_service_request(void* user,
                                                     std::uint8_t kind,
                                                     std::uint32_t request_id,
                                                     std::uint32_t client_token,
                                                     std::uint32_t input_handle,
                                                     std::uint8_t priority,
                                                     std::uint32_t timeout_ms) {
    auto* runtime = static_cast<State*>(user);
    return runtime != nullptr && runtime->submit_service_request_impl(
        kind, request_id, client_token, input_handle, priority, timeout_ms);
}

bool ScriptTaskWorkerRuntime::State::cancel_service_request(void* user,
                                                      std::uint32_t request_id,
                                                      std::uint32_t client_token) {
    auto* runtime = static_cast<State*>(user);
    return runtime != nullptr && runtime->cancel_service_request_impl(request_id, client_token);
}

bool ScriptTaskWorkerRuntime::State::submit_service_request_impl(std::uint8_t kind,
                                                          std::uint32_t request_id,
                                                          std::uint32_t client_token,
                                                          std::uint32_t input_handle,
                                                          std::uint8_t priority,
                                                          std::uint32_t timeout_ms) {
    if (stopped_ || active_supervisor_ == nullptr || request_id == 0 || client_token == 0) {
        return false;
    }
    ScriptTaskServiceRequest request;
    request.kind = static_cast<HostServiceJobKind>(kind);
    request.request_id = request_id;
    request.client_token = client_token;
    request.input_handle = input_handle;
    request.priority = priority;
    request.timeout_ms = timeout_ms;
    const ScriptTaskServiceRequestPostResult posted = post_script_task_service_request(
        *active_supervisor_, session_, service_request_sequence_, request,
        options_.service_request_codec);
    if (!posted.accepted()) {
        return false;
    }
    service_request_sequence_ = service_request_sequence_ == std::numeric_limits<std::uint32_t>::max()
        ? 1
        : service_request_sequence_ + 1;
    return true;
}

bool ScriptTaskWorkerRuntime::State::cancel_service_request_impl(std::uint32_t request_id,
                                                          std::uint32_t client_token) {
    if (stopped_ || active_supervisor_ == nullptr || request_id == 0 || client_token == 0) {
        return false;
    }
    const ScriptTaskServiceRequestPostResult posted = post_script_task_service_cancel(
        *active_supervisor_, session_, service_request_sequence_,
        {request_id, client_token}, options_.service_request_codec);
    if (!posted.accepted()) {
        return false;
    }
    service_request_sequence_ = service_request_sequence_ == std::numeric_limits<std::uint32_t>::max()
        ? 1
        : service_request_sequence_ + 1;
    return true;
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::State::pump_callbacks(
    std::uint64_t now_ms,
    ScriptTaskSupervisor& supervisor,
    std::size_t max_timer_callbacks,
    std::size_t max_animation_callbacks) {
    ScriptTaskWorkerRuntimeStepResult result;
    const auto finish = [this, &supervisor](ScriptTaskWorkerRuntimeStepResult value) {
        if (fatal()) {
            value.fatal = true;
            (void)publish_fatal(supervisor);
        }
        return value;
    };
    if (!initialized_ || fatal()) {
        return finish(result);
    }
    if (!supervisor_matches(supervisor)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return finish(result);
    }

    // Timer callbacks can issue service requests too. Keep the same
    // supervisor scope as input and completion processing while they run.
    ActiveSupervisorScope scope(*this, supervisor);
    const DomDirtyFlags before = subtree_dirty_flags(*document_owner_.root());
    telemetry_.timer_callbacks += runtime_->pump_timers(now_ms, max_timer_callbacks);
    if (stopped_) return result;
    if (consume_callback_failure()) {
        return finish(result);
    }
    telemetry_.animation_frame_callbacks += runtime_->pump_animation_frame(now_ms, max_animation_callbacks);
    if (stopped_) return result;
    if (consume_callback_failure()) {
        return finish(result);
    }
    const DomDirtyFlags after = subtree_dirty_flags(*document_owner_.root());
    result.dom_mutated = before != after || after != DomDirtyNone;
    if (!result.dom_mutated) {
        return finish(result);
    }

    ++telemetry_.js_mutation_seq;
    const Node* hovered = input_controller_->hovered_node();
    const Node* active = input_controller_->active_node();
    const Node* focused = input_controller_->focused_node();
    style_resolver_->set_interaction_state(hovered, active, focused);
    if (!rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return finish(result);
    }
    result.frame = publish_frame(supervisor);
    result.frame_published = result.frame.accepted();
    return finish(result);
}

void ScriptTaskWorkerRuntime::State::set_fatal(ScriptTaskWorkerRuntimeFatalReason reason,
                                        std::uint32_t diagnostic_code) {
    if (fatal()) return;
    fatal_record_.reason = reason;
    fatal_record_.diagnostic_code = diagnostic_code;
    fatal_record_.last_input_sequence = telemetry_.input_packet_seq;
    fatal_record_.last_frame_sequence = telemetry_.published_frame_seq;
    fatal_record_.message_bytes = 0;
}

bool ScriptTaskWorkerRuntime::State::consume_callback_failure() {
    const ScriptCallbackFailure failure = runtime_->take_script_callback_failure();
    if (!failure.failed()) {
        return false;
    }
    set_fatal(failure.status == ScriptCallbackFailureStatus::ExecutionBudgetExceeded
                  ? ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog
                  : failure.status == ScriptCallbackFailureStatus::ResourceLimit
                      ? ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded
                      : ScriptTaskWorkerRuntimeFatalReason::ScriptException);
    fatal_record_.message_bytes = std::min(failure.message.size(), options_.budgets.max_resource_bytes);
    return true;
}

bool ScriptTaskWorkerRuntime::State::rebuild_pipeline() {
    if (!document_owner_.root() || !style_resolver_) return false;
    RenderTreeOptions render_options = render_tree_options_from_budgets(options_.budgets);
    render_options.viewport_width = options_.viewport.width;
    render_options.viewport_height = options_.viewport.height;
    RenderTreeBuilder render_builder(*style_resolver_, render_options);
    RenderObjectPtr render_tree = render_builder.build(*document_owner_.root());
    if (!render_tree) return false;
    LayoutEngineOptions layout_options = layout_engine_options_from_budgets(options_.budgets);
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    layout_options.text_system = is_canonical_frame_version(options_.frame_codec.version)
        ? &options_.text_catalog->text_system() : options_.text_system;
#else
    layout_options.text_system = options_.text_system;
#endif
    LayoutEngine layout_engine(*style_resolver_, {}, layout_options);
    LayoutBoxPtr layout_tree = layout_engine.layout(*render_tree, options_.viewport.width, options_.viewport.height);
    if (!layout_tree) return false;
    // Text layout caches were just rebuilt from this DOM snapshot. Core's layer
    // builder rejects caches while their source nodes still carry dirty flags.
    clear_dirty_flags(*document_owner_.root());
    LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(options_.budgets);
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    layer_options.text_system = is_canonical_frame_version(options_.frame_codec.version)
        ? &options_.text_catalog->text_system() : options_.text_system;
    if (is_canonical_frame_version(options_.frame_codec.version)) {
        configure_background_resource_resolver(layer_options, style_resolver_.get());
        if constexpr (HasBackgroundResourceResolver<LayerTreeBuilderOptions>::value) {
          layer_options.image_resolver = {[](const Node& node, ImageResolveKind kind,
                                             std::uint16_t resource_id, ImageResolveResult& result,
                                             void* context) {
            if (context == nullptr) return false;
            auto* runtime = static_cast<State*>(context);
            if (!runtime->options_.image_provider) return false;
            std::string_view url;
            if (kind == ImageResolveKind::Content) {
                url = node.attribute("src");
            } else if (kind == ImageResolveKind::Background && resource_id != 0) {
                const std::string* background_url = runtime->style_resolver_->background_image_resource_url(resource_id);
                if (background_url != nullptr) url = *background_url;
            }
            ResolvedImageResource resolved;
            return !url.empty() && runtime->options_.image_provider->resolve(url, resolved) &&
                set_image_resolve_result(result, resolved);
          }, this};
        }
    }
#else
    layer_options.text_system = options_.text_system;
#endif
    LayerTreeBuilder layer_builder(layer_options);
    LayerNodePtr layer_tree = layer_builder.build(*layout_tree);
    if (!layer_tree) return false;

    const Node* hovered = input_controller_ != nullptr ? input_controller_->hovered_node() : nullptr;
    const Node* active = input_controller_ != nullptr ? input_controller_->active_node() : nullptr;
    const Node* focused = input_controller_ != nullptr ? input_controller_->focused_node() : nullptr;
    // Rebuilding the layer tree must restore the existing focus silently;
    // autofocus is an initialization action, not a layout-change event.
    const auto interaction_hints = style_resolver_->interaction_invalidation_hints();
    std::unique_ptr<InputController> input_controller = std::make_unique<InputController>(
        *layer_tree, InteractionInvalidationOptions{
            interaction_hints.hover, interaction_hints.active, interaction_hints.focus},
        input_controller_ == nullptr);
    if (input_controller_) {
        input_controller->set_interaction_state(hovered, active, focused);
    }
    render_tree_ = std::move(render_tree);
    layout_tree_ = std::move(layout_tree);
    layer_tree_ = std::move(layer_tree);
    input_controller_ = std::move(input_controller);
    pending_frame_publication_ = true;
    return true;
}

bool ScriptTaskWorkerRuntime::State::has_dirty_document() const {
    return pending_frame_publication_ ||
           (document_owner_.root() != nullptr &&
            subtree_dirty_flags(*document_owner_.root()) != DomDirtyNone);
}

// Pins refer only to the session, so a callback may destroy the public facade.
class ScriptTaskWorkerRuntime::OperationPin {
public:
    explicit OperationPin(State* state) : state_(state != nullptr && !state->stopped_ &&
        state->active_operations_ < state->options_.max_active_operations ? state : nullptr) {
        if (state_) ++state_->active_operations_;
    }
    ~OperationPin() { if (state_) state_->release_operation(); }
    explicit operator bool() const { return state_ != nullptr; }
    State* operator->() const { return state_; }
    OperationPin(const OperationPin&) = delete;
    OperationPin& operator=(const OperationPin&) = delete;
private:
    State* state_;
};

ScriptTaskWorkerRuntime::ScriptTaskWorkerRuntime(ScriptAppSession session,
    ScriptTaskWorkerRuntimeOptions options) : state_(new State(session, std::move(options))) {}
ScriptTaskWorkerRuntime::~ScriptTaskWorkerRuntime() {
    State* state = state_;
    state_ = nullptr;
    if (!state) return;
    state->owner_alive_ = false;
    state->stop();
    if (state->active_operations_ == 0) delete state;
}
void ScriptTaskWorkerRuntime::stop() { if (state_) state_->stop(); }
bool ScriptTaskWorkerRuntime::initialized() const { return state_ && state_->initialized_; }
bool ScriptTaskWorkerRuntime::stopped() const { return !state_ || state_->stopped_; }
bool ScriptTaskWorkerRuntime::fatal() const { return state_ && state_->fatal(); }
ScriptTaskWorkerRuntimeFatalRecord ScriptTaskWorkerRuntime::fatal_record() const {
    return state_ ? state_->fatal_record_ : ScriptTaskWorkerRuntimeFatalRecord{};
}
ScriptTaskWorkerRuntimeTelemetry ScriptTaskWorkerRuntime::telemetry() const {
    return state_ ? state_->telemetry_ : ScriptTaskWorkerRuntimeTelemetry{};
}
bool ScriptTaskWorkerRuntime::service_gateway_available() const {
    return state_ && state_->service_gateway_available_;
}
void ScriptTaskWorkerRuntime::set_ui_accepted_frame_sequence(std::uint32_t sequence) {
    OperationPin pin(state_);
    if (pin) pin->telemetry_.ui_accepted_frame_seq = sequence;
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::initialize(std::string_view html, std::string_view css) {
    const auto admission = state_ ? state_->initialization_admission()
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (admission != ScriptTaskWorkerRuntimeInitStatus::Accepted) return admission;
    OperationPin pin(state_);
    if (!pin) return state_ && !state_->stopped_
        ? ScriptTaskWorkerRuntimeInitStatus::BudgetRejected
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    return pin->initialize(html, css);
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::initialize(ScriptTaskSupervisor& supervisor, std::string_view html, std::string_view css) {
    const auto admission = state_ ? state_->initialization_admission()
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (admission != ScriptTaskWorkerRuntimeInitStatus::Accepted) return admission;
    OperationPin pin(state_);
    if (!pin) return state_ && !state_->stopped_
        ? ScriptTaskWorkerRuntimeInitStatus::BudgetRejected
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    return pin->initialize(supervisor, html, css);
}

void ScriptTaskWorkerRuntime::set_host_time_ms(std::uint64_t now_ms) {
    OperationPin pin(state_);
    if (!pin) return;
    return pin->set_host_time_ms(now_ms);
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval(std::string_view source, std::string_view source_name) {
    OperationPin pin(state_);
    if (!pin) return {false, {}, "worker stopped or operation limit", ScriptEvaluationStatus::Exception};
    return pin->eval(source, source_name);
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval_with_supervisor(ScriptTaskSupervisor& supervisor, std::string_view source, std::string_view source_name) {
    OperationPin pin(state_);
    if (!pin) return {false, {}, "worker stopped or operation limit", ScriptEvaluationStatus::Exception};
    return pin->eval_with_supervisor(supervisor, source, source_name);
}

ScriptTaskAppFramePublishResult ScriptTaskWorkerRuntime::publish_frame(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return {};
    return pin->publish_frame(supervisor);
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::process_one(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return {};
    return pin->process_one(supervisor);
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::pump_callbacks(std::uint64_t now_ms, ScriptTaskSupervisor& supervisor, std::size_t max_timer_callbacks, std::size_t max_animation_callbacks) {
    OperationPin pin(state_);
    if (!pin) return {};
    return pin->pump_callbacks(now_ms, supervisor, max_timer_callbacks, max_animation_callbacks);
}

bool ScriptTaskWorkerRuntime::publish_fatal(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return false;
    return pin->publish_fatal(supervisor);
}

bool ScriptTaskWorkerRuntime::inject_c_safe_fatal_for_test(ScriptTaskSupervisor& supervisor, std::uint32_t diagnostic_code) {
    OperationPin pin(state_);
    if (!pin) return false;
    return pin->inject_c_safe_fatal_for_test(supervisor, diagnostic_code);
}

} // namespace jellyframe
