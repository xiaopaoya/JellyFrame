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

namespace jellyframe {
namespace {

ScriptTaskAppFrameCodecOptions frame_options_from(const ScriptTaskWorkerRuntimeOptions& options) {
    ScriptTaskAppFrameCodecOptions result = options.frame_codec;
    if (result.max_commands == 0) result.max_commands = std::max<std::size_t>(1, options.budgets.max_display_commands);
    if (result.max_input_targets == 0) result.max_input_targets = std::max<std::size_t>(1, options.budgets.max_display_commands);
    if (result.max_payload_bytes == 0) result.max_payload_bytes = std::max<std::size_t>(1, options.budgets.max_resource_bytes);
    if (result.max_text_bytes == 0) result.max_text_bytes = result.max_payload_bytes;
    return result;
}

ScriptTaskInputCodecOptions input_options_from(const ScriptTaskWorkerRuntimeOptions& options) {
    ScriptTaskInputCodecOptions result = options.input_codec;
    if (result.max_payload_bytes == 0) result.max_payload_bytes = std::max<std::size_t>(1, options.budgets.max_resource_bytes);
    if (result.max_text_bytes == 0) result.max_text_bytes = result.max_payload_bytes;
    return result;
}

} // namespace

ScriptTaskWorkerRuntime::ActiveSupervisorScope::ActiveSupervisorScope(
    ScriptTaskWorkerRuntime& runtime,
    ScriptTaskSupervisor& supervisor)
    : runtime_(runtime), previous_(runtime.active_supervisor_) {
    runtime_.active_supervisor_ = &supervisor;
}

ScriptTaskWorkerRuntime::ActiveSupervisorScope::~ActiveSupervisorScope() {
    runtime_.active_supervisor_ = previous_;
}

ScriptTaskWorkerRuntime::ScriptTaskWorkerRuntime(ScriptAppSession session,
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

ScriptTaskWorkerRuntime::~ScriptTaskWorkerRuntime() {
    stop();
}

void ScriptTaskWorkerRuntime::stop() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    initialized_ = false;
    service_gateway_available_ = false;
    active_supervisor_ = nullptr;
    // Script-backend wrappers must die before their private document. Both are
    // worker-owned, so this ordering never requires another task to help.
    runtime_.reset();
    input_controller_.reset();
    layer_tree_.reset();
    layout_tree_.reset();
    render_tree_.reset();
    style_resolver_.reset();
    document_owner_.set_root(nullptr);
}

bool ScriptTaskWorkerRuntime::publish_fatal(ScriptTaskSupervisor& supervisor) {
    if (!fatal()) {
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

bool ScriptTaskWorkerRuntime::inject_c_safe_fatal_for_test(
    ScriptTaskSupervisor& supervisor,
    std::uint32_t diagnostic_code) {
    if (!initialized_ || stopped_ || fatal()) {
        return false;
    }
    ActiveSupervisorScope scope(*this, supervisor);
    set_fatal(ScriptTaskWorkerRuntimeFatalReason::ScriptFatal, diagnostic_code);
    return publish_fatal(supervisor);
}

ScriptTaskWorkerRuntimeInitStatus ScriptTaskWorkerRuntime::initialize(std::string_view html,
                                                                       std::string_view css) {
    if (stopped_) return ScriptTaskWorkerRuntimeInitStatus::Stopped;
    if (fatal()) return ScriptTaskWorkerRuntimeInitStatus::Fatal;
    if (!session_.valid()) return ScriptTaskWorkerRuntimeInitStatus::InvalidSession;
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
    runtime_ = create_script_runtime(options_.script);
    runtime_->bind_document(*document_owner_.root());
    runtime_->bind_script_service_gateway(submit_service_request, this, cancel_service_request);
    service_gateway_available_ = true;
    style_resolver_ = std::make_unique<StyleResolver>(std::move(stylesheet));
    if (!rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
        return ScriptTaskWorkerRuntimeInitStatus::BudgetRejected;
    }
    initialized_ = true;
    return ScriptTaskWorkerRuntimeInitStatus::Accepted;
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval(std::string_view source, std::string_view source_name) {
    if (!initialized_ || fatal()) {
        return {false, {}, "worker runtime is not active", ScriptEvaluationStatus::Exception};
    }
    ScriptEvaluationResult result = runtime_->eval(source, source_name);
    if (!result.ok) {
        set_fatal(result.status == ScriptEvaluationStatus::ExecutionBudgetExceeded
                      ? ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog
                      : ScriptTaskWorkerRuntimeFatalReason::ScriptException);
        (void) runtime_->take_script_callback_failure();
    } else if (consume_callback_failure()) {
        // A callback failure is terminal even when the selected backend returned a
        // successful value for the surrounding worker operation.
    } else if (has_dirty_document() && !rebuild_pipeline()) {
        set_fatal(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded);
    }
    return result;
}

ScriptEvaluationResult ScriptTaskWorkerRuntime::eval_with_supervisor(
    ScriptTaskSupervisor& supervisor,
    std::string_view source,
    std::string_view source_name) {
    ActiveSupervisorScope scope(*this, supervisor);
    ScriptEvaluationResult result = eval(source, source_name);
    if (fatal()) {
        (void)publish_fatal(supervisor);
    }
    return result;
}

ScriptTaskAppFramePublishResult ScriptTaskWorkerRuntime::publish_frame(ScriptTaskSupervisor& supervisor) {
    ScriptTaskAppFramePublishResult result;
    ++telemetry_.frame_publish_attempts;
    if (!initialized_ || fatal() || !layer_tree_) {
        ++telemetry_.frame_publish_rejections;
        return result;
    }
    ScriptTaskAppFrame frame = make_script_task_app_frame(
        *layer_tree_, options_.viewport, {}, options_.frame_codec.version >= 2);
    result = frame_publisher_.publish(supervisor, session_, frame);
    if (result.accepted()) {
        ++telemetry_.published_frame_seq;
    } else {
        ++telemetry_.frame_publish_rejections;
    }
    if (result.accepted()) {
        clear_dirty_flags(*document_owner_.root());
    }
    return result;
}

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::process_one(ScriptTaskSupervisor& supervisor) {
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

bool ScriptTaskWorkerRuntime::submit_service_request(void* user,
                                                     std::uint8_t kind,
                                                     std::uint32_t request_id,
                                                     std::uint32_t client_token,
                                                     std::uint32_t input_handle,
                                                     std::uint8_t priority,
                                                     std::uint32_t timeout_ms) {
    auto* runtime = static_cast<ScriptTaskWorkerRuntime*>(user);
    return runtime != nullptr && runtime->submit_service_request_impl(
        kind, request_id, client_token, input_handle, priority, timeout_ms);
}

bool ScriptTaskWorkerRuntime::cancel_service_request(void* user,
                                                      std::uint32_t request_id,
                                                      std::uint32_t client_token) {
    auto* runtime = static_cast<ScriptTaskWorkerRuntime*>(user);
    return runtime != nullptr && runtime->cancel_service_request_impl(request_id, client_token);
}

bool ScriptTaskWorkerRuntime::submit_service_request_impl(std::uint8_t kind,
                                                          std::uint32_t request_id,
                                                          std::uint32_t client_token,
                                                          std::uint32_t input_handle,
                                                          std::uint8_t priority,
                                                          std::uint32_t timeout_ms) {
    if (active_supervisor_ == nullptr || request_id == 0 || client_token == 0) {
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

bool ScriptTaskWorkerRuntime::cancel_service_request_impl(std::uint32_t request_id,
                                                          std::uint32_t client_token) {
    if (active_supervisor_ == nullptr || request_id == 0 || client_token == 0) {
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

ScriptTaskWorkerRuntimeStepResult ScriptTaskWorkerRuntime::pump_callbacks(
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

    // Timer callbacks can issue service requests too. Keep the same
    // supervisor scope as input and completion processing while they run.
    ActiveSupervisorScope scope(*this, supervisor);
    const DomDirtyFlags before = subtree_dirty_flags(*document_owner_.root());
    telemetry_.timer_callbacks += runtime_->pump_timers(now_ms, max_timer_callbacks);
    if (consume_callback_failure()) {
        return finish(result);
    }
    telemetry_.animation_frame_callbacks += runtime_->pump_animation_frame(now_ms, max_animation_callbacks);
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

void ScriptTaskWorkerRuntime::set_fatal(ScriptTaskWorkerRuntimeFatalReason reason,
                                        std::uint32_t diagnostic_code) {
    if (fatal()) return;
    fatal_record_.reason = reason;
    fatal_record_.diagnostic_code = diagnostic_code;
    fatal_record_.last_input_sequence = telemetry_.input_packet_seq;
    fatal_record_.last_frame_sequence = telemetry_.published_frame_seq;
    fatal_record_.message_bytes = 0;
}

bool ScriptTaskWorkerRuntime::consume_callback_failure() {
    const ScriptCallbackFailure failure = runtime_->take_script_callback_failure();
    if (!failure.failed()) {
        return false;
    }
    set_fatal(failure.status == ScriptCallbackFailureStatus::ExecutionBudgetExceeded
                  ? ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog
                  : ScriptTaskWorkerRuntimeFatalReason::ScriptException);
    fatal_record_.message_bytes = std::min(failure.message.size(), options_.budgets.max_resource_bytes);
    return true;
}

bool ScriptTaskWorkerRuntime::rebuild_pipeline() {
    if (!document_owner_.root() || !style_resolver_) return false;
    RenderTreeBuilder render_builder(*style_resolver_, render_tree_options_from_budgets(options_.budgets));
    RenderObjectPtr render_tree = render_builder.build(*document_owner_.root());
    if (!render_tree) return false;
    LayoutEngine layout_engine(*style_resolver_);
    LayoutBoxPtr layout_tree = layout_engine.layout(*render_tree, options_.viewport.width, options_.viewport.height);
    if (!layout_tree) return false;
    LayerTreeBuilder layer_builder(layer_tree_options_from_budgets(options_.budgets));
    LayerNodePtr layer_tree = layer_builder.build(*layout_tree);
    if (!layer_tree) return false;

    const Node* hovered = input_controller_ != nullptr ? input_controller_->hovered_node() : nullptr;
    const Node* active = input_controller_ != nullptr ? input_controller_->active_node() : nullptr;
    const Node* focused = input_controller_ != nullptr ? input_controller_->focused_node() : nullptr;
    std::unique_ptr<InputController> input_controller = std::make_unique<InputController>(*layer_tree);
    if (input_controller_) {
        input_controller->set_interaction_state(hovered, active, focused);
    }
    render_tree_ = std::move(render_tree);
    layout_tree_ = std::move(layout_tree);
    layer_tree_ = std::move(layer_tree);
    input_controller_ = std::move(input_controller);
    return true;
}

bool ScriptTaskWorkerRuntime::has_dirty_document() const {
    return document_owner_.root() != nullptr &&
           subtree_dirty_flags(*document_owner_.root()) != DomDirtyNone;
}

} // namespace jellyframe
