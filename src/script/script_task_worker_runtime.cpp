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
#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>

namespace jellyframe {
namespace {

bool uses_owned_frame_resources(std::uint8_t version) {
    return version == 9 || version == 10 || version == kRuntimeShapedFrameVersion ||
           version == kRuntimeScalarFrameVersion;
}

template<class T, class = void>
struct HasBackgroundResourceResolver : std::false_type {};

template<class T>
struct HasBackgroundResourceResolver<T, std::void_t<decltype(
    std::declval<T&>().background_resource_resolver)>> : std::true_type {};

#if JELLYFRAME_RUNTIME_IMAGE_API
template<class T> struct FunctionArguments;
template<class R, class... Args> struct FunctionArguments<R (*)(Args...)> {
    using Tuple = std::tuple<Args...>;
};
using ImageResolveResult = std::remove_reference_t<std::remove_cv_t<
    std::tuple_element_t<3, typename FunctionArguments<ImageHandleResolveCallback>::Tuple>>>;

template<class Result>
bool set_image_resolve_result(Result& result, const RuntimeResolvedImage& resolved) {
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
    if (uses_owned_frame_resources(result.version) && options.image_provider)
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

bool frame_at_or_after(std::uint32_t sequence, std::uint32_t minimum) {
    // Supervisor sequences wrap at 32 bits and reserve zero.
    return sequence != 0 && sequence - minimum < (std::uint32_t{1} << 31);
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
    void rebuild_after_mutation(ScriptTaskSupervisor& supervisor, ScriptTaskWorkerRuntimeStepResult& result);
    void release_pipeline();
    bool admits_input(std::uint32_t frame_sequence) const;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    void acknowledge_contextual_frame(std::uint32_t frame_sequence);
    bool select_contextual_input_frame(std::uint32_t frame_sequence, ScriptTaskInputKind kind);
    void record_contextual_publication(std::uint32_t frame_sequence);
#endif
    bool admits_recovered_gesture(const ScriptTaskInputEvent& input);
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
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    runtime_shaped::LayoutResult shaped_layout_;
    std::vector<std::uint32_t> shaped_positions_;
#endif
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    struct ContextualFrames {
        struct Published {
            std::uint32_t sequence = 0;
            runtime_shaped::LayerCapture capture;
        };
        runtime_shaped::LayerCapture draft;
        runtime_shaped::LayerCapture input;
        std::uint32_t input_frame_sequence = 0;
        // The WS147 port can have four queued leases, one frame being
        // presented and one already displayed. Input outside the retained
        // window is rejected even if the port acknowledges that sequence.
        std::array<Published, 6> published;
        std::size_t next_slot = 0;
    };
    std::unique_ptr<ContextualFrames> contextual_frames_;
#endif
    std::unique_ptr<InputController> input_controller_;
    ScriptTaskAppFramePublisher frame_publisher_;
    ScriptTaskWorkerRuntimeFatalRecord fatal_record_;
    ScriptTaskWorkerRuntimeTelemetry telemetry_;
    // Tracks rebuilt output after DOM dirty flags are cleared for Core capture.
    bool pending_frame_publication_ = false;
    bool recovering_frame_ = false;
    std::uint32_t recovery_frame_sequence_ = 0;
    bool recovery_pointer_down_ = false;
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
    release_pipeline();
    style_resolver_.reset();
    document_owner_.set_root(nullptr);
}

void ScriptTaskWorkerRuntime::State::release_pipeline() {
    input_controller_.reset();
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    contextual_frames_.reset();
#endif
    layer_tree_.reset();
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    shaped_layout_ = {};
    shaped_positions_.clear();
#endif
    layout_tree_.reset();
    render_tree_.reset();
}

void ScriptTaskWorkerRuntime::State::release_operation() {
    if (--active_operations_ != 0) return;
    if (stopped_) drain();
    if (!owner_alive_) delete this;
}

bool ScriptTaskWorkerRuntime::State::supervisor_matches(const ScriptTaskSupervisor& supervisor) const {
    if (!session_.valid() || !supervisor.accepts(session_)) return false;
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    if (uses_owned_frame_resources(options_.frame_codec.version)) {
        const auto catalog = supervisor.text_catalog(session_);
        const auto images = supervisor.image_provider(session_);
        if (!options_.text_catalog || catalog != options_.text_catalog ||
            images != options_.image_provider || (images && !images->valid())) return false;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API
        const auto shaped = supervisor.shaped_text_binding(session_);
        if (shaped != options_.shaped_text_binding || (shaped && !shaped->valid())) return false;
        if (options_.frame_codec.version == kRuntimeScalarFrameVersion && shaped) return false;
#endif
        return true;
    }
#else
    if (uses_owned_frame_resources(options_.frame_codec.version)) return false;
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
    if (options_.contextual_inline_input &&
        (!JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API ||
         options_.frame_codec.version != kRuntimeShapedFrameVersion)) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        return ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration;
    }
    if (uses_owned_frame_resources(options_.frame_codec.version)) {
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
        if (!options_.text_catalog ||
            (options_.image_provider && !options_.image_provider->valid()) ||
            (options_.frame_codec.version == kRuntimeScalarFrameVersion &&
             !script_task_app_frame_codec_supports_version(kRuntimeScalarFrameVersion))) {
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
    if (options_.frame_codec.version == kRuntimeShapedFrameVersion) {
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        if (!options_.shaped_text_binding || !options_.shaped_text_binding->valid() || options_.image_provider) {
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

    const auto prepare_document = options_.prepare_document;
    void* prepare_document_user = options_.prepare_document_user;
    options_.prepare_document = nullptr;
    options_.prepare_document_user = nullptr;
    if (prepare_document && !prepare_document(*parsed.document, prepare_document_user)) {
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
    if (uses_owned_frame_resources(options_.frame_codec.version)) {
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
    if (uses_owned_frame_resources(options_.frame_codec.version) && active_supervisor_ == nullptr) {
        return {false, {}, "owned-resource frame evaluation requires supervisor validation", ScriptEvaluationStatus::Exception};
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
    } else if (subtree_dirty_flags(*document_owner_.root()) != DomDirtyNone && !rebuild_pipeline()) {
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
    const auto* frame_tree = layer_tree_.get();
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    if (contextual_frames_) frame_tree = contextual_frames_->draft.layer_tree();
#endif
    if (!initialized_ || fatal() || !frame_tree || !supervisor_matches(supervisor)) {
        if (initialized_ && !fatal() && !supervisor_matches(supervisor))
            set_fatal(ScriptTaskWorkerRuntimeFatalReason::ResourceMismatch);
        ++telemetry_.frame_publish_rejections;
        if (fatal()) (void)publish_fatal(supervisor);
        return result;
    }
    ScriptTaskAppFrame frame;
    if (options_.frame_codec.version == kRuntimeShapedFrameVersion) {
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        const auto status = make_script_task_shaped_frame(*frame_tree, shaped_layout_, shaped_positions_,
            options_.viewport, options_.frame_codec, frame);
        if (status != ScriptTaskAppFrameCodecStatus::Accepted) {
            ++telemetry_.frame_publish_rejections;
            return result;
        }
#else
        ++telemetry_.frame_publish_rejections;
        return result;
#endif
    } else if (uses_owned_frame_resources(options_.frame_codec.version)) {
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
        telemetry_.published_frame_seq = result.lease.frame_sequence;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
        if (contextual_frames_) record_contextual_publication(result.lease.frame_sequence);
#endif
        if (recovering_frame_) {
            recovery_frame_sequence_ = result.lease.frame_sequence;
            recovering_frame_ = false;
        }
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
        if (has_dirty_document()) rebuild_after_mutation(supervisor, result);
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
            rebuild_after_mutation(supervisor, result);
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
    bool input_frame_admitted = admits_input(packet.input_frame_sequence);
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    if (contextual_frames_) {
        const auto last_input = telemetry_.input_packet_seq;
        input_frame_admitted = packet.sequence != 0 && packet.sequence != last_input &&
            (last_input == 0 || frame_at_or_after(packet.sequence, last_input)) &&
            select_contextual_input_frame(packet.input_frame_sequence, input.kind);
    }
#endif
    if (!input_frame_admitted) {
        ++telemetry_.rejected_input_packets;
        if (pending_frame_publication_) {
            result.frame = publish_frame(supervisor);
            result.frame_published = result.frame.accepted();
        }
        return finish(result);
    }
    if (recovery_frame_sequence_ != 0 && !admits_recovered_gesture(input)) {
        ++telemetry_.rejected_input_packets;
        return result;
    }
    ++telemetry_.input_packets;
    telemetry_.input_packet_seq = packet.sequence;
    const DomDirtyFlags before = subtree_dirty_flags(*document_owner_.root());
    bool interaction_style_changed = false;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    if (contextual_frames_) {
        const auto* hovered = input_controller_->hovered_node();
        const auto* active = input_controller_->active_node();
        const auto* focused = input_controller_->focused_node();
        const auto hints = style_resolver_->interaction_invalidation_hints();
        result.input_status = dispatch_script_task_input(*input_controller_, input, contextual_frames_->input).status;
        if (!stopped_) {
            interaction_style_changed = (hints.hover && input_controller_->hovered_node() != hovered) ||
                (hints.active && input_controller_->active_node() != active) ||
                (hints.focus && input_controller_->focused_node() != focused);
        }
    } else
#endif
    {
        result.input_status = dispatch_script_task_input(*input_controller_, input).status;
    }
    result.input_accepted = result.input_status == ScriptTaskInputDispatchStatus::Accepted;
    if (stopped_) return result;
    if (consume_callback_failure()) {
        return finish(result);
    }
    const DomDirtyFlags after = subtree_dirty_flags(*document_owner_.root());
    result.dom_mutated = before != after || after != DomDirtyNone;
    if (result.dom_mutated) {
        ++telemetry_.js_mutation_seq;
        rebuild_after_mutation(supervisor, result);
    } else if (interaction_style_changed) {
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
    rebuild_after_mutation(supervisor, result);
    return finish(result);
}

void ScriptTaskWorkerRuntime::State::rebuild_after_mutation(
    ScriptTaskSupervisor& supervisor, ScriptTaskWorkerRuntimeStepResult& result) {
    if (subtree_dirty_flags(*document_owner_.root()) != DomDirtyNone && !rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return;
    }
    if (!pending_frame_publication_) return;
    result.frame = publish_frame(supervisor);
    result.frame_published = result.frame.accepted();
}

bool ScriptTaskWorkerRuntime::State::admits_input(std::uint32_t frame_sequence) const {
    if (recovering_frame_ || !input_controller_) return false;
    if (recovery_frame_sequence_ == 0) return true;
    const auto accepted = telemetry_.ui_accepted_frame_seq;
    return frame_at_or_after(frame_sequence, recovery_frame_sequence_) &&
           frame_at_or_after(accepted, frame_sequence);
}

#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
void ScriptTaskWorkerRuntime::State::acknowledge_contextual_frame(std::uint32_t sequence) {
    if (!sequence || !telemetry_.published_frame_seq ||
        !frame_at_or_after(telemetry_.published_frame_seq, sequence) ||
        (telemetry_.ui_accepted_frame_seq != 0 &&
         (sequence == telemetry_.ui_accepted_frame_seq ||
          !frame_at_or_after(sequence, telemetry_.ui_accepted_frame_seq)))) return;
    telemetry_.ui_accepted_frame_seq = sequence;
    for (auto& frame : contextual_frames_->published) {
        if (!frame_at_or_after(frame.sequence, sequence)) frame = {};
    }
}

bool ScriptTaskWorkerRuntime::State::select_contextual_input_frame(std::uint32_t sequence,
                                                                  ScriptTaskInputKind kind) {
    if (sequence == 0) return false;
    if (sequence != telemetry_.ui_accepted_frame_seq) {
        // A release may have been queued just before a newer presentation
        // was acknowledged. Complete that gesture using the live capture
        // already selected by its press.
        return kind == ScriptTaskInputKind::PointerUp && input_controller_ && input_controller_->active_node() &&
               sequence == contextual_frames_->input_frame_sequence &&
               contextual_frames_->input.source_is_current();
    }
    for (const auto& frame : contextual_frames_->published) {
        if (frame.sequence != sequence || !frame.capture.source_is_current()) continue;
        if (frame.capture.layer_tree() != contextual_frames_->input.layer_tree()) {
            // The resolver has its own interaction generation. Rebuilding for a
            // pseudo-class must not invalidate the on-screen DOM capture.
            auto controller = std::make_unique<InputController>(*frame.capture.layer_tree(),
                InteractionInvalidationOptions{false, false, false}, false);
            if (input_controller_) {
                const auto* active = contextual_frames_->input.source_is_current()
                    ? input_controller_->active_node() : nullptr;
                controller->set_interaction_state(input_controller_->hovered_node(),
                    active, input_controller_->focused_node(), frame.capture);
            }
            input_controller_ = std::move(controller);
            contextual_frames_->input = frame.capture;
        }
        contextual_frames_->input_frame_sequence = sequence;
        return true;
    }
    return false;
}

void ScriptTaskWorkerRuntime::State::record_contextual_publication(std::uint32_t sequence) {
    auto& frames = *contextual_frames_;
    if (frames.published[frames.next_slot].sequence != 0 &&
        frames.published[frames.next_slot].sequence == telemetry_.ui_accepted_frame_seq) {
        frames.next_slot = (frames.next_slot + 1) % frames.published.size();
    }
    frames.published[frames.next_slot] = {sequence, frames.draft};
    frames.next_slot = (frames.next_slot + 1) % frames.published.size();
}
#endif

bool ScriptTaskWorkerRuntime::State::admits_recovered_gesture(const ScriptTaskInputEvent& input) {
    // A touch can span the rejected and recovered frames. Never dispatch its
    // tail to the replacement DOM if its down event was dropped.
    switch (input.kind) {
    case ScriptTaskInputKind::PointerDown:
        recovery_pointer_down_ = true;
        return true;
    case ScriptTaskInputKind::PointerUp: {
        const bool accepted = recovery_pointer_down_;
        recovery_pointer_down_ = false;
        return accepted;
    }
    case ScriptTaskInputKind::PointerMove:
        return input.buttons == 0 || recovery_pointer_down_;
    default:
        return true;
    }
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
    const Node* hovered = input_controller_ ? input_controller_->hovered_node() : nullptr;
    const Node* active = input_controller_ ? input_controller_->active_node() : nullptr;
    const Node* focused = input_controller_ ? input_controller_->focused_node() : nullptr;
    style_resolver_->set_interaction_state(hovered, active, focused);
    RenderTreeOptions render_options = render_tree_options_from_budgets(options_.budgets);
    render_options.viewport_width = options_.viewport.width;
    render_options.viewport_height = options_.viewport.height;
    RenderTreeBuilder render_builder(*style_resolver_, render_options);
    RenderObjectPtr render_tree = render_builder.build(*document_owner_.root());
    if (!render_tree) return false;
    LayoutEngineOptions layout_options = layout_engine_options_from_budgets(options_.budgets);
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    layout_options.text_system = uses_owned_frame_resources(options_.frame_codec.version)
        ? &options_.text_catalog->text_system() : options_.text_system;
#else
    layout_options.text_system = options_.text_system;
#endif
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) && JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1 && JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    if (uses_owned_frame_resources(options_.frame_codec.version) && options_.image_provider) {
        layout_options.natural_image_sizing = options_.natural_image_sizing;
        // Metadata and paint resolve against the same immutable, owned snapshot.
        layout_options.natural_image_sizing.provider = {[](const Node& node, void* context) {
            const auto* runtime = static_cast<const State*>(context);
            const auto& images = runtime->options_.image_provider;
            RuntimeResolvedImage resolved;
            return images->resolve(node.attribute("src"), resolved)
                ? resolved.intrinsic_size : ImageIntrinsicSize{};
        }, this};
    }
#elif JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    auto natural_image_sizing = options_.natural_image_sizing;
    if (options_.frame_codec.version == kRuntimeScalarFrameVersion && options_.image_provider) {
        natural_image_sizing.provider = {[](const Node& node, int& width, int& height, void* context) {
            const auto& runtime = *static_cast<const State*>(context);
            RuntimeResolvedImage resolved;
            if (!runtime.options_.image_provider->resolve(node.attribute("src"), resolved) ||
                resolved.width <= 0 || resolved.height <= 0) return false;
            width = resolved.width;
            height = resolved.height;
            return true;
        }, this};
        layout_options.natural_image_sizing = &natural_image_sizing;
    }
#endif
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    if(options_.frame_codec.version==kRuntimeShapedFrameVersion) layout_options.text_system=nullptr;
#endif
#if defined(JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION)
    LayoutEngine layout_engine(*style_resolver_, layout_options);
#else
    LayoutEngine layout_engine(*style_resolver_, TextMeasureProvider{}, layout_options);
#endif
    LayoutBoxPtr layout_tree;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    runtime_shaped::LayoutResult shaped_layout;
    if (options_.frame_codec.version == kRuntimeShapedFrameVersion) {
        auto work = std::min(options_.shaped_text_binding->work_limit(), options_.shaped_layout.paragraph.shaping.work);
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
        if (options_.contextual_inline_input) {
            if (options_.shaped_text_binding->layout_inline_document(layout_engine, *render_tree,
                    options_.viewport.width, options_.viewport.height, options_.shaped_layout, work,
                    shaped_layout) != runtime_shaped::Status::ok) return false;
        } else
#endif
        if (options_.shaped_text_binding->layout_document(layout_engine, *render_tree,
                options_.viewport.width, options_.viewport.height, options_.shaped_layout, work,
                shaped_layout) != runtime_shaped::Status::ok) return false;
    } else
#endif
    {
        layout_tree = layout_engine.layout(*render_tree, options_.viewport.width, options_.viewport.height);
        if (!layout_tree) return false;
    }
    // Text layout caches were just rebuilt from this DOM snapshot. Core's layer
    // builder rejects caches while their source nodes still carry dirty flags.
    clear_dirty_flags(*document_owner_.root());
    LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(options_.budgets);
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
    layer_options.text_system = uses_owned_frame_resources(options_.frame_codec.version)
        ? &options_.text_catalog->text_system() : options_.text_system;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    if (options_.frame_codec.version == kRuntimeShapedFrameVersion) layer_options.text_system = nullptr;
#endif
#if JELLYFRAME_RUNTIME_IMAGE_API
    if (uses_owned_frame_resources(options_.frame_codec.version)) {
        configure_background_resource_resolver(layer_options, style_resolver_.get());
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
            RuntimeResolvedImage resolved;
            return !url.empty() && runtime->options_.image_provider->resolve(url, resolved) &&
                set_image_resolve_result(result, resolved);
        }, this};
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        layer_options.image_resolver.intrinsic_size = [](std::uint32_t handle, int& width, int& height,
                                                         void* context) {
            const auto& runtime = *static_cast<const State*>(context);
            return runtime.options_.image_provider &&
                runtime.options_.image_provider->intrinsic_size(handle, width, height);
        };
        layer_options.image_resolver.resolve_url = [](std::string_view url, std::uint32_t& handle,
                                                      void* context) {
            const auto& runtime = *static_cast<const State*>(context);
            RuntimeResolvedImage resolved;
            if (!runtime.options_.image_provider || !runtime.options_.image_provider->resolve(url, resolved)) return false;
            handle = resolved.image_handle;
            return true;
        };
#endif
    }
#endif
#else
    layer_options.text_system = options_.text_system;
#endif
    LayerTreeBuilder layer_builder(layer_options);
    LayerNodePtr layer_tree;
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    runtime_shaped::LayerCapture shaped_capture;
    if (options_.frame_codec.version == kRuntimeShapedFrameVersion) {
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
        if (options_.contextual_inline_input) {
            if (!layer_builder.build_contextual(shaped_layout, runtime_shaped::TextHitCaptureOptions{},
                                               shaped_capture)) return false;
            if (!contextual_frames_) contextual_frames_ = std::make_unique<ContextualFrames>();
            shaped_positions_ = shaped_capture.after_command_counts();
            shaped_layout_ = std::move(shaped_layout);
            contextual_frames_->draft = std::move(shaped_capture);
            render_tree_ = std::move(render_tree);
            pending_frame_publication_ = true;
            return true;
        }
#endif
        if (!layer_builder.build_contextual(shaped_layout, shaped_capture)) return false;
        layer_tree=LayerNodePtr(new LayerNode);
        layer_tree->type=LayerType::Root;layer_tree->reasons=LayerReasonRoot;
        layer_tree->box=shaped_layout.tree();layer_tree->bounds=shaped_layout.tree()->rect;
        layer_tree->clip_rect=layer_tree->bounds;
        layer_tree->display_list=shaped_capture.layer_tree()->display_list;
#else
        if (!layer_builder.build_shaped(shaped_layout, shaped_capture)) return false;
        layer_tree = std::move(shaped_capture.layer_tree);
#endif
    } else
#endif
    {
#if defined(JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION) && JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION >= 1
        if (options_.frame_codec.version == 10) {
            const auto status = layer_builder.build_strict(*layout_tree, layer_tree);
            if (status == LayerTreeBuildStatus::LayerLimit || status == LayerTreeBuildStatus::CommandLimit) {
                if (telemetry_.published_frame_seq == 0 || telemetry_.ui_accepted_frame_seq == 0)
                    return false;
                // The UI owns its accepted snapshot. The worker's old layout
                // and hit targets may borrow DOM nodes removed by the script.
                release_pipeline();
                style_resolver_->set_interaction_state(nullptr, nullptr, nullptr);
                pending_frame_publication_ = false;
                recovering_frame_ = true;
                recovery_pointer_down_ = false;
                return true;
            }
            if (status != LayerTreeBuildStatus::Success) return false;
        } else
#endif
        {
            layer_tree = layer_builder.build(*layout_tree);
        }
    }
    if (!layer_tree) return false;

    // Rebuilding the layer tree must restore the existing focus silently;
    // autofocus is an initialization action, not a layout-change event.
    const auto interaction_hints = style_resolver_->interaction_invalidation_hints();
    std::unique_ptr<InputController> input_controller = std::make_unique<InputController>(
        *layer_tree, InteractionInvalidationOptions{
            interaction_hints.hover, interaction_hints.active, interaction_hints.focus},
        input_controller_ == nullptr && !recovering_frame_);
    if (input_controller_) {
        input_controller->set_interaction_state(hovered, active, focused);
    }
    input_controller_ = std::move(input_controller);
    layer_tree_ = std::move(layer_tree);
    layout_tree_ = std::move(layout_tree);
    render_tree_ = std::move(render_tree);
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
    shaped_layout_ = std::move(shaped_layout);
    #if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    shaped_positions_ = shaped_capture.after_command_counts();
#else
    shaped_positions_ = std::move(shaped_capture.after_command_counts);
#endif
#endif
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
    if (!pin) return;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
    if (pin->contextual_frames_) {
        pin->acknowledge_contextual_frame(sequence);
        return;
    }
#endif
    pin->telemetry_.ui_accepted_frame_seq = sequence;
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::initialize(std::string_view html, std::string_view css) {
    const auto admission = state_ ? state_->initialization_admission()
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (admission != ScriptTaskWorkerRuntimeInitStatus::Accepted) return admission;
    OperationPin pin(state_);
    if (!pin) return state_ && !state_->stopped_
        ? ScriptTaskWorkerRuntimeInitStatus::BudgetRejected
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    try {
        return pin->initialize(html, css);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    }
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::initialize(ScriptTaskSupervisor& supervisor, std::string_view html, std::string_view css) {
    const auto admission = state_ ? state_->initialization_admission()
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (admission != ScriptTaskWorkerRuntimeInitStatus::Accepted) return admission;
    OperationPin pin(state_);
    if (!pin) return state_ && !state_->stopped_
        ? ScriptTaskWorkerRuntimeInitStatus::BudgetRejected
        : ScriptTaskWorkerRuntimeInitStatus::Stopped;
    try {
        return pin->initialize(supervisor, html, css);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    }
}

void ScriptTaskWorkerRuntime::set_host_time_ms(std::uint64_t now_ms) {
    OperationPin pin(state_);
    if (!pin) return;
    return pin->set_host_time_ms(now_ms);
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval(std::string_view source, std::string_view source_name) {
    OperationPin pin(state_);
    if (!pin) return {false, {}, "worker stopped or operation limit", ScriptEvaluationStatus::Exception};
    try {
        return pin->eval(source, source_name);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {false, {}, {}, ScriptEvaluationStatus::Exception};
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {false, {}, {}, ScriptEvaluationStatus::Exception};
    }
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval_with_supervisor(ScriptTaskSupervisor& supervisor, std::string_view source, std::string_view source_name) {
    OperationPin pin(state_);
    if (!pin) return {false, {}, "worker stopped or operation limit", ScriptEvaluationStatus::Exception};
    try {
        return pin->eval_with_supervisor(supervisor, source, source_name);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {false, {}, {}, ScriptEvaluationStatus::Exception};
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {false, {}, {}, ScriptEvaluationStatus::Exception};
    }
}

ScriptTaskAppFramePublishResult ScriptTaskWorkerRuntime::publish_frame(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return {};
    try {
        return pin->publish_frame(supervisor);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {};
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return {};
    }
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::process_one(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return {};
    try {
        return pin->process_one(supervisor);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        ScriptTaskWorkerRuntimeStepResult result;
        result.fatal = true;
        return result;
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        ScriptTaskWorkerRuntimeStepResult result;
        result.fatal = true;
        return result;
    }
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::pump_callbacks(std::uint64_t now_ms, ScriptTaskSupervisor& supervisor, std::size_t max_timer_callbacks, std::size_t max_animation_callbacks) {
    OperationPin pin(state_);
    if (!pin) return {};
    try {
        return pin->pump_callbacks(now_ms, supervisor, max_timer_callbacks, max_animation_callbacks);
    } catch (const std::bad_alloc&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        ScriptTaskWorkerRuntimeStepResult result;
        result.fatal = true;
        return result;
    } catch (const std::length_error&) {
        pin->set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        ScriptTaskWorkerRuntimeStepResult result;
        result.fatal = true;
        return result;
    }
}

bool ScriptTaskWorkerRuntime::publish_fatal(ScriptTaskSupervisor& supervisor) {
    OperationPin pin(state_);
    if (!pin) return false;
    try {
        return pin->publish_fatal(supervisor);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

bool ScriptTaskWorkerRuntime::inject_c_safe_fatal_for_test(ScriptTaskSupervisor& supervisor, std::uint32_t diagnostic_code) {
    OperationPin pin(state_);
    if (!pin) return false;
    return pin->inject_c_safe_fatal_for_test(supervisor, diagnostic_code);
}

} // namespace jellyframe
