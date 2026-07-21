#include "script/jerryscript_runtime.h"

#include "app_runtime/app_device_services.h"
#include "app_runtime/app_host_data.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "app_runtime/xml_http_request.h"
#include "render_core/canvas2d.h"
#include "render_core/form_control.h"
#include "render_core/form_submission.h"
#include "render_core/layout.h"
#include "render_core/style.h"
#include "render_core/text_scan.h"

#include <jerryscript.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace jellyframe {

struct ScriptEventListener {
    JerryScriptRuntime* runtime = nullptr;
    Node* node = nullptr;
    std::string type;
    EventTarget::ListenerId listener_id = 0;
    jerry_value_t callback = 0;
    jerry_value_t target_object = 0;
    EventListenerOptions options;
    bool active = false;
    bool property_handler = false;
};

struct ScriptEventBinding {
    Event* event = nullptr;
    bool active = false;
    bool cancelable = false;
    bool default_prevented = false;
};

struct ScriptTimer {
    std::uint32_t id = 0;
    std::uint64_t due_ms = 0;
    std::uint32_t delay_ms = 0;
    jerry_value_t callback = 0;
    bool repeat = false;
    bool active = false;
};

struct ScriptAnimationFrameCallback {
    std::uint32_t id = 0;
    jerry_value_t callback = 0;
    bool active = false;
};

struct ScriptXmlHttpRequest {
    JerryScriptRuntime* runtime = nullptr;
    AppXmlHttpRequest request;
    jerry_value_t object = 0;
    std::array<jerry_value_t, 6> callbacks{};
    bool active = false;
    bool js_object_alive = false;
};

struct ScriptAudioElement {
    JerryScriptRuntime* runtime = nullptr;
    std::uint32_t id = 0;
    std::string src;
    double volume = 1.0;
    jerry_value_t object = 0;
    std::array<jerry_value_t, 2> property_callbacks{};
    std::array<jerry_value_t, 2> event_listeners{};
    bool active = false;
    bool js_object_alive = false;
};

struct ScriptGeolocationRequest {
    std::uint32_t job_id = 0;
    jerry_value_t success_callback = 0;
    jerry_value_t error_callback = 0;
    bool active = false;
};

struct ScriptCanvasGradient {
    std::uint32_t id = 0;
};

struct ScriptDialogState {
    Node* node = nullptr;
    std::string return_value;
};

struct ScriptFormData {
    JerryScriptRuntime* runtime = nullptr;
    std::vector<FormDataEntry> entries;
};

struct ScriptNodeBinding {
    JerryScriptRuntime* runtime = nullptr;
    Node* node = nullptr;
    Rect layout_rect;
    bool active = false;
    bool layout_snapshot_requested = false;
    bool has_layout_snapshot = false;
};

struct ScriptLocalStorageBinding {
    JerryScriptRuntime* runtime = nullptr;
    AppLocalStorageShadow* storage = nullptr;
    bool active = false;
};

struct ScriptRuntimeAccess {
    static bool can_adopt_detached_node(const JerryScriptRuntime& runtime) {
        return runtime.can_adopt_detached_node();
    }

    static Node* adopt_detached_node(JerryScriptRuntime& runtime, std::unique_ptr<Node> node) {
        return runtime.adopt_detached_node(std::move(node));
    }

    static std::unique_ptr<Node> release_detached_node(JerryScriptRuntime& runtime, Node& node) {
        return runtime.release_detached_node(node);
    }

    static void add_script_event_listener(JerryScriptRuntime& runtime,
                                          Node& node,
                                          std::string type,
                                          jerry_value_t callback,
                                          EventListenerOptions options) {
        runtime.add_script_event_listener(node, std::move(type), callback, options);
    }

    static void set_script_event_handler(JerryScriptRuntime& runtime,
                                         Node& node,
                                         std::string type,
                                         jerry_value_t callback) {
        runtime.set_script_event_handler(node, std::move(type), callback);
    }

    static jerry_value_t get_script_event_handler(JerryScriptRuntime& runtime,
                                                  Node& node,
                                                  const std::string& type) {
        return runtime.get_script_event_handler(node, type);
    }

    static void remove_script_event_listener(JerryScriptRuntime& runtime,
                                             Node& node,
                                             std::string type,
                                             jerry_value_t callback) {
        runtime.remove_script_event_listener(node, std::move(type), callback);
    }

    static void add_window_event_listener(JerryScriptRuntime& runtime,
                                          std::string type,
                                          jerry_value_t callback,
                                          jerry_value_t target,
                                          EventListenerOptions options) {
        runtime.add_window_event_listener(std::move(type), callback, target, options);
    }

    static void set_window_event_handler(JerryScriptRuntime& runtime,
                                         std::string type,
                                         jerry_value_t callback,
                                         jerry_value_t target) {
        runtime.set_window_event_handler(std::move(type), callback, target);
    }

    static jerry_value_t get_window_event_handler(JerryScriptRuntime& runtime,
                                                  const std::string& type) {
        return runtime.get_window_event_handler(type);
    }

    static void remove_window_event_listener(JerryScriptRuntime& runtime,
                                             std::string type,
                                             jerry_value_t callback) {
        runtime.remove_window_event_listener(std::move(type), callback);
    }

    static std::uint32_t add_timer(JerryScriptRuntime& runtime,
                                   jerry_value_t callback,
                                   std::uint32_t delay_ms,
                                   bool repeat) {
        return runtime.add_timer(callback, delay_ms, repeat);
    }

    static void clear_timer(JerryScriptRuntime& runtime, std::uint32_t id) {
        runtime.clear_timer(id);
    }

    static std::uint32_t add_animation_frame_callback(JerryScriptRuntime& runtime,
                                                      jerry_value_t callback) {
        return runtime.add_animation_frame_callback(callback);
    }

    static void cancel_animation_frame_callback(JerryScriptRuntime& runtime, std::uint32_t id) {
        runtime.cancel_animation_frame_callback(id);
    }

    static ScriptXmlHttpRequest* create_xml_http_request(JerryScriptRuntime& runtime) {
        return runtime.create_xml_http_request();
    }

    static ScriptAudioElement* create_audio_element(JerryScriptRuntime& runtime, std::string src) {
        return runtime.create_audio_element(std::move(src));
    }

    static ScriptGeolocationRequest* create_geolocation_request(JerryScriptRuntime& runtime,
                                                                std::uint32_t job_id,
                                                                jerry_value_t success_callback,
                                                                jerry_value_t error_callback) {
        return runtime.create_geolocation_request(job_id, success_callback, error_callback);
    }

    static bool can_create_geolocation_request(const JerryScriptRuntime& runtime) {
        const auto active_requests =
            static_cast<std::size_t>(std::count_if(runtime.geolocation_requests_.begin(),
                                                   runtime.geolocation_requests_.end(),
                                                   [](const std::unique_ptr<ScriptGeolocationRequest>& request) {
                                                       return request->active;
                                                   }));
        return active_requests < runtime.options_.max_geolocation_requests;
    }

    static AppRuntimeHost* app_host(JerryScriptRuntime& runtime) {
        return runtime.app_host_;
    }

    static NetworkFetchMock* network_fetch(JerryScriptRuntime& runtime) {
        return runtime.network_fetch_;
    }

    static AppLocationSnapshotMock* location_snapshot(JerryScriptRuntime& runtime) {
        return runtime.location_snapshot_;
    }

    static const AppHostDataSnapshot* host_data_snapshot(const JerryScriptRuntime& runtime) {
        return runtime.host_data_snapshot_;
    }

    static const AppHostDataAccessPolicy* host_data_access_policy(const JerryScriptRuntime& runtime) {
        return runtime.host_data_access_policy_.get();
    }

    static bool host_data_battery_allowed(const JerryScriptRuntime& runtime) {
        return runtime.host_data_access_policy_ != nullptr && runtime.host_data_access_policy_->battery;
    }

    static bool host_data_weather_allowed(const JerryScriptRuntime& runtime) {
        return runtime.host_data_access_policy_ != nullptr && runtime.host_data_access_policy_->weather;
    }

    static bool host_data_activity_allowed(const JerryScriptRuntime& runtime) {
        return runtime.host_data_access_policy_ != nullptr && runtime.host_data_access_policy_->activity;
    }

    static ScriptSystemState system_state(const JerryScriptRuntime& runtime) {
        return runtime.system_state_;
    }

    static std::uint64_t current_time_ms(const JerryScriptRuntime& runtime) {
        return runtime.current_time_ms_;
    }

    static ScriptAudioHost audio_host(const JerryScriptRuntime& runtime) {
        return runtime.audio_host_;
    }

    static Canvas2DRegistry* canvas_2d(JerryScriptRuntime& runtime) {
        return runtime.canvas_2d_;
    }

    static ScriptCanvasGradient* create_canvas_gradient(JerryScriptRuntime& runtime, std::uint32_t gradient_id) {
        return runtime.create_canvas_gradient(gradient_id);
    }

    static bool show_modal_dialog(JerryScriptRuntime& runtime, Node& node) {
        return runtime.show_modal_dialog(node);
    }

    static void close_dialog(JerryScriptRuntime& runtime,
                             Node& node,
                             std::string return_value,
                             bool update_return_value) {
        runtime.close_dialog(node, std::move(return_value), update_return_value);
    }

    static void set_dialog_open(JerryScriptRuntime& runtime, Node& node, bool open) {
        runtime.set_dialog_open(node, open);
    }

    static std::string dialog_return_value(const JerryScriptRuntime& runtime, const Node& node) {
        return runtime.dialog_return_value(node);
    }

    static void set_dialog_return_value(JerryScriptRuntime& runtime, Node& node, std::string value) {
        runtime.set_dialog_return_value(node, std::move(value));
    }

    static ScriptNodeBinding* bind_script_node(JerryScriptRuntime& runtime, Node& node) {
        return runtime.bind_script_node(node);
    }

    static Node* resolve_script_node(JerryScriptRuntime& runtime, const ScriptNodeBinding& binding) {
        return runtime.resolve_script_node(binding);
    }

    static void forget_script_node_binding(JerryScriptRuntime& runtime, ScriptNodeBinding& binding) {
        runtime.forget_script_node_binding(binding);
    }

    static void invalidate_script_node(JerryScriptRuntime& runtime, Node& node) {
        runtime.invalidate_script_node(node);
    }

    static bool request_layout_snapshot(JerryScriptRuntime& runtime, ScriptNodeBinding& binding) {
        if (binding.runtime != &runtime || !binding.active) {
            return false;
        }
        if (!binding.layout_snapshot_requested) {
            if (runtime.layout_snapshot_bindings_.size() >= runtime.options_.max_layout_snapshot_nodes) {
                return false;
            }
            binding.layout_snapshot_requested = true;
            runtime.layout_snapshot_bindings_.push_back(&binding);
        }
        return binding.has_layout_snapshot;
    }

    static ScriptLocalStorageBinding* bind_script_local_storage(JerryScriptRuntime& runtime,
                                                                AppLocalStorageShadow& storage) {
        return runtime.bind_script_local_storage(storage);
    }

    static AppLocalStorageShadow* resolve_script_local_storage(JerryScriptRuntime& runtime,
                                                               const ScriptLocalStorageBinding& binding) {
        return runtime.resolve_script_local_storage(binding);
    }

    static void forget_script_local_storage_binding(JerryScriptRuntime& runtime,
                                                    ScriptLocalStorageBinding& binding) {
        runtime.forget_script_local_storage_binding(binding);
    }

    static const JerryScriptRuntimeOptions& options(const JerryScriptRuntime& runtime) {
        return runtime.options_;
    }

    static std::uint32_t& execution_watchdog_depth(JerryScriptRuntime& runtime) {
        return runtime.execution_watchdog_depth_;
    }

    static std::uint32_t& execution_watchdog_remaining(JerryScriptRuntime& runtime) {
        return runtime.execution_watchdog_remaining_;
    }

    static bool& execution_watchdog_interrupted(JerryScriptRuntime& runtime) {
        return runtime.execution_watchdog_interrupted_;
    }

    static std::string location_hash(const JerryScriptRuntime& runtime) {
        return runtime.location_hash();
    }

    static void set_location_hash(JerryScriptRuntime& runtime, std::string value) {
        runtime.set_location_hash(std::move(value));
    }

    static std::size_t route_history_length(const JerryScriptRuntime& runtime) {
        return runtime.route_history_length();
    }

    static void push_route_history(JerryScriptRuntime& runtime, std::string value) {
        runtime.push_route_history(std::move(value));
    }

    static void replace_route_history(JerryScriptRuntime& runtime, std::string value) {
        runtime.replace_route_history(std::move(value));
    }

    static bool traverse_route_history(JerryScriptRuntime& runtime, int delta) {
        return runtime.traverse_route_history(delta);
    }

    static bool& execution_watchdog_interrupt_pending(JerryScriptRuntime& runtime) {
        return runtime.execution_watchdog_interrupt_pending_;
    }
};

namespace {

bool g_runtime_active = false;

void free_script_event_binding(void* native_p, jerry_object_native_info_t*) {
    delete static_cast<ScriptEventBinding*>(native_p);
}

void script_node_destroyed(Node& node, void* context) {
    auto* runtime = static_cast<JerryScriptRuntime*>(context);
    if (runtime != nullptr) {
        ScriptRuntimeAccess::invalidate_script_node(*runtime, node);
    }
}

void free_script_node_binding(void* native_p, jerry_object_native_info_t*) {
    auto* binding = static_cast<ScriptNodeBinding*>(native_p);
    if (binding == nullptr) {
        return;
    }
    if (binding->runtime != nullptr) {
        ScriptRuntimeAccess::forget_script_node_binding(*binding->runtime, *binding);
    }
    delete binding;
}

void free_script_xhr(void* native_p, jerry_object_native_info_t*) {
    auto* xhr = static_cast<ScriptXmlHttpRequest*>(native_p);
    if (xhr != nullptr) {
        xhr->js_object_alive = false;
        xhr->object = 0;
    }
}

void free_script_audio(void* native_p, jerry_object_native_info_t*) {
    auto* audio = static_cast<ScriptAudioElement*>(native_p);
    if (audio != nullptr) {
        audio->js_object_alive = false;
        audio->object = 0;
    }
}

void free_script_local_storage_binding(void* native_p, jerry_object_native_info_t*) {
    auto* binding = static_cast<ScriptLocalStorageBinding*>(native_p);
    if (binding == nullptr) {
        return;
    }
    if (binding->runtime != nullptr) {
        ScriptRuntimeAccess::forget_script_local_storage_binding(*binding->runtime, *binding);
    }
    delete binding;
}

void free_script_form_data(void* native_p, jerry_object_native_info_t*) {
    delete static_cast<ScriptFormData*>(native_p);
}

void script_node_event_listener_removed(void* context) {
    auto* listener = static_cast<ScriptEventListener*>(context);
    if (listener == nullptr) {
        return;
    }
    listener->node = nullptr;
    listener->listener_id = 0;
    listener->active = false;
}

const jerry_object_native_info_t kNodeNativeInfo = {free_script_node_binding, 0, 0};
const jerry_object_native_info_t kRuntimeNativeInfo = {nullptr, 0, 0};
const jerry_object_native_info_t kEventNativeInfo = {free_script_event_binding, 0, 0};
const jerry_object_native_info_t kXhrNativeInfo = {free_script_xhr, 0, 0};
const jerry_object_native_info_t kLocalStorageNativeInfo = {free_script_local_storage_binding, 0, 0};
const jerry_object_native_info_t kAudioNativeInfo = {free_script_audio, 0, 0};
const jerry_object_native_info_t kCanvasGradientNativeInfo = {nullptr, 0, 0};
const jerry_object_native_info_t kFormDataNativeInfo = {free_script_form_data, 0, 0};

class JerryValue {
public:
    explicit JerryValue(jerry_value_t value)
        : value_(value) {}

    ~JerryValue() {
        if (owns_) {
            jerry_value_free(value_);
        }
    }

    JerryValue(const JerryValue&) = delete;
    JerryValue& operator=(const JerryValue&) = delete;

    JerryValue(JerryValue&& other) noexcept
        : value_(other.value_),
          owns_(other.owns_) {
        other.owns_ = false;
    }

    JerryValue& operator=(JerryValue&& other) noexcept {
        if (this != &other) {
            if (owns_) {
                jerry_value_free(value_);
            }
            value_ = other.value_;
            owns_ = other.owns_;
            other.owns_ = false;
        }
        return *this;
    }

    jerry_value_t get() const {
        return value_;
    }

    jerry_value_t release() {
        owns_ = false;
        return value_;
    }

private:
    jerry_value_t value_;
    bool owns_ = true;
};

std::string ascii_lowercase(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::string jerry_string_to_std_string(jerry_value_t value) {
    const jerry_size_t size = jerry_string_size(value, JERRY_ENCODING_UTF8);
    if (size == 0) {
        return {};
    }

    std::vector<jerry_char_t> buffer(size);
    const jerry_size_t written = jerry_string_to_buffer(value, JERRY_ENCODING_UTF8, buffer.data(), size);
    return std::string(reinterpret_cast<const char*>(buffer.data()), written);
}

std::string value_to_string(jerry_value_t value) {
    JerryValue string_value(jerry_value_to_string(value));
    if (jerry_value_is_exception(string_value.get())) {
        return "<unprintable JavaScript value>";
    }
    return jerry_string_to_std_string(string_value.get());
}

JerryValue string_to_value(std::string_view value) {
    return JerryValue(jerry_string(reinterpret_cast<const jerry_char_t*>(value.data()),
                                   static_cast<jerry_size_t>(value.size()),
                                   JERRY_ENCODING_UTF8));
}

JerryValue string_to_value(const char* value) {
    return string_to_value(std::string_view(value != nullptr ? value : ""));
}

JerryValue string_to_value(const std::string& value) {
    return string_to_value(std::string_view(value));
}

JerryValue evaluate_script(std::string_view source, std::string_view source_name) {
    const auto* bytes = reinterpret_cast<const jerry_char_t*>(source.data());
    if (source_name.empty()) {
        return JerryValue(jerry_eval(bytes, source.size(), JERRY_PARSE_NO_OPTS));
    }

    const std::string name(source_name);
    JerryValue name_value(jerry_string_sz(name.c_str()));

    jerry_parse_options_t options;
    std::memset(&options, 0, sizeof(options));
    options.options = JERRY_PARSE_HAS_SOURCE_NAME;
    options.source_name = name_value.get();

    JerryValue parsed(jerry_parse(bytes, source.size(), &options));
    if (jerry_value_is_exception(parsed.get())) {
        return parsed;
    }

    return JerryValue(jerry_run(parsed.get()));
}

jerry_value_t script_execution_halt_callback(void* user) {
    auto* runtime = static_cast<JerryScriptRuntime*>(user);
    if (runtime == nullptr) {
        return jerry_string_sz("script execution budget exceeded");
    }

    std::uint32_t& remaining = ScriptRuntimeAccess::execution_watchdog_remaining(*runtime);
    if (remaining > 0) {
        --remaining;
        return jerry_undefined();
    }

    ScriptRuntimeAccess::execution_watchdog_interrupted(*runtime) = true;
    ScriptRuntimeAccess::execution_watchdog_interrupt_pending(*runtime) = true;
    return jerry_string_sz("script execution budget exceeded");
}

class ScriptExecutionBudgetScope {
public:
    explicit ScriptExecutionBudgetScope(JerryScriptRuntime& runtime)
        : runtime_(runtime) {
        const JerryScriptRuntimeOptions& options = ScriptRuntimeAccess::options(runtime_);
        if (options.max_execution_check_count == 0 ||
            !jerry_feature_enabled(JERRY_FEATURE_VM_EXEC_STOP)) {
            return;
        }

        std::uint32_t& depth = ScriptRuntimeAccess::execution_watchdog_depth(runtime_);
        ++depth;
        enabled_ = true;
        if (depth != 1) {
            return;
        }

        ScriptRuntimeAccess::execution_watchdog_remaining(runtime_) = options.max_execution_check_count;
        ScriptRuntimeAccess::execution_watchdog_interrupted(runtime_) = false;
        const std::uint32_t interval = std::max<std::uint32_t>(1, options.execution_check_interval);
        jerry_halt_handler(interval, script_execution_halt_callback, &runtime_);
        installed_ = true;
    }

    ~ScriptExecutionBudgetScope() {
        if (!enabled_) {
            return;
        }

        std::uint32_t& depth = ScriptRuntimeAccess::execution_watchdog_depth(runtime_);
        if (depth > 0) {
            --depth;
        }
        if (installed_ && depth == 0) {
            jerry_halt_handler(1, nullptr, nullptr);
        }
    }

    ScriptExecutionBudgetScope(const ScriptExecutionBudgetScope&) = delete;
    ScriptExecutionBudgetScope& operator=(const ScriptExecutionBudgetScope&) = delete;

private:
    JerryScriptRuntime& runtime_;
    bool enabled_ = false;
    bool installed_ = false;
};

template <typename Callback>
JerryValue run_with_execution_budget(JerryScriptRuntime& runtime, Callback&& callback) {
    ScriptExecutionBudgetScope scope(runtime);
    return JerryValue(callback());
}

Node* native_node(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    auto* binding = static_cast<ScriptNodeBinding*>(jerry_object_get_native_ptr(object, &kNodeNativeInfo));
    if (binding == nullptr || binding->runtime == nullptr) {
        return nullptr;
    }
    return ScriptRuntimeAccess::resolve_script_node(*binding->runtime, *binding);
}

ScriptNodeBinding* native_node_binding(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    auto* binding = static_cast<ScriptNodeBinding*>(jerry_object_get_native_ptr(object, &kNodeNativeInfo));
    return binding != nullptr && binding->runtime != nullptr && binding->active ? binding : nullptr;
}

JerryScriptRuntime* native_runtime(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<JerryScriptRuntime*>(jerry_object_get_native_ptr(object, &kRuntimeNativeInfo));
}

void bind_native_node(jerry_value_t object, JerryScriptRuntime& runtime, Node& node) {
    jerry_object_set_native_ptr(object, &kNodeNativeInfo, ScriptRuntimeAccess::bind_script_node(runtime, node));
    jerry_object_set_native_ptr(object, &kRuntimeNativeInfo, &runtime);
}

Node* find_by_id(Node& node, const std::string& id) {
    if (node.type == NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (Node* found = find_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

Node* find_first_element_by_tag(Node& node, const std::string& tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (Node* found = find_first_element_by_tag(*child, tag_name)) {
            return found;
        }
    }
    return nullptr;
}

bool has_attribute(const Node& node, const std::string& name) {
    return node.attributes.find(name) != node.attributes.end();
}

bool is_progress_or_meter(const Node& node) {
    return node.type == NodeType::Element && (node.tag_name == "progress" || node.tag_name == "meter");
}

bool is_option_or_optgroup(const Node& node) {
    return node.type == NodeType::Element && (node.tag_name == "option" || node.tag_name == "optgroup");
}

bool is_labelable_element(const Node& node) {
    if (node.type != NodeType::Element) {
        return false;
    }
    if (node.tag_name == "input") {
        return ascii_lowercase(node.attribute("type")) != "hidden";
    }
    return node.tag_name == "button" || node.tag_name == "meter" || node.tag_name == "output" ||
           node.tag_name == "progress" || node.tag_name == "select" || node.tag_name == "textarea";
}

Node& root_node(Node& node) {
    Node* current = &node;
    while (current->parent != nullptr) {
        current = current->parent;
    }
    return *current;
}

Node* first_labelable_descendant(Node& node) {
    std::vector<Node*> pending;
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        pending.push_back(it->get());
    }
    while (!pending.empty()) {
        Node* current = pending.back();
        pending.pop_back();
        if (is_labelable_element(*current)) {
            return current;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return nullptr;
}

bool document_collection_image(const Node& node) {
    return node.type == NodeType::Element && node.tag_name == "img";
}

bool document_collection_embed(const Node& node) {
    return node.type == NodeType::Element && node.tag_name == "embed";
}

bool document_collection_link(const Node& node) {
    return node.type == NodeType::Element && (node.tag_name == "a" || node.tag_name == "area") &&
           has_attribute(node, "href");
}

bool document_collection_form(const Node& node) {
    return node.type == NodeType::Element && node.tag_name == "form";
}

bool document_collection_script(const Node& node) {
    return node.type == NodeType::Element && node.tag_name == "script";
}

Node* closest_ancestor_select(Node& node) {
    for (Node* current = node.parent; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element && current->tag_name == "select") {
            return current;
        }
    }
    return nullptr;
}

int option_index_in_select(const Node& select, const Node& option) {
    int index = 0;
    std::vector<const Node*> pending;
    pending.push_back(&select);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            if (current == &option) {
                return index;
            }
            ++index;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return -1;
}

std::size_t utf8_codepoint_count(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size();) {
        consume_utf8_codepoint(text, index);
        ++count;
    }
    return count;
}

bool html_binary_string_from_utf8(std::string_view text, std::string& output) {
    output.clear();
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        if (codepoint > 0xffU) {
            return false;
        }
        output.push_back(static_cast<char>(codepoint));
    }
    return true;
}

void append_utf8_codepoint(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
}

std::string html_binary_string_to_utf8(std::string_view bytes) {
    std::string output;
    output.reserve(bytes.size());
    for (const unsigned char byte : bytes) {
        append_utf8_codepoint(output, byte);
    }
    return output;
}

std::string base64_encode(std::string_view input) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t index = 0; index < input.size(); index += 3U) {
        const unsigned a = static_cast<unsigned char>(input[index]);
        const bool have_b = index + 1U < input.size();
        const bool have_c = index + 2U < input.size();
        const unsigned b = have_b ? static_cast<unsigned char>(input[index + 1U]) : 0U;
        const unsigned c = have_c ? static_cast<unsigned char>(input[index + 2U]) : 0U;
        output.push_back(kAlphabet[(a >> 2U) & 0x3fU]);
        output.push_back(kAlphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0fU)]);
        output.push_back(have_b ? kAlphabet[((b & 0x0fU) << 2U) | ((c >> 6U) & 0x03U)] : '=');
        output.push_back(have_c ? kAlphabet[c & 0x3fU] : '=');
    }
    return output;
}

int base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool is_ascii_whitespace(char ch) {
    return ch == '\t' || ch == '\n' || ch == '\f' || ch == '\r' || ch == ' ';
}

bool base64_decode_html(std::string_view input, std::string& output) {
    std::string compact;
    compact.reserve(input.size());
    for (const char ch : input) {
        if (!is_ascii_whitespace(ch)) {
            compact.push_back(ch);
        }
    }
    if (compact.size() % 4U == 1U) {
        return false;
    }
    while (compact.size() % 4U != 0U) {
        compact.push_back('=');
    }

    output.clear();
    output.reserve((compact.size() / 4U) * 3U);
    for (std::size_t index = 0; index < compact.size(); index += 4U) {
        const char c0 = compact[index];
        const char c1 = compact[index + 1U];
        const char c2 = compact[index + 2U];
        const char c3 = compact[index + 3U];
        const bool pad2 = c2 == '=';
        const bool pad3 = c3 == '=';
        if (c0 == '=' || c1 == '=' || (pad2 && !pad3) ||
            ((pad2 || pad3) && index + 4U != compact.size())) {
            return false;
        }
        const int v0 = base64_value(c0);
        const int v1 = base64_value(c1);
        const int v2 = pad2 ? 0 : base64_value(c2);
        const int v3 = pad3 ? 0 : base64_value(c3);
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
            return false;
        }
        output.push_back(static_cast<char>((v0 << 2U) | (v1 >> 4U)));
        if (!pad2) {
            output.push_back(static_cast<char>(((v1 & 0x0f) << 4U) | (v2 >> 2U)));
        }
        if (!pad3) {
            output.push_back(static_cast<char>(((v2 & 0x03) << 6U) | v3));
        }
    }
    return true;
}

std::string data_attribute_to_dataset_key(std::string_view attribute_name) {
    if (attribute_name.rfind("data-", 0) != 0 || attribute_name.size() <= 5) {
        return {};
    }
    std::string key;
    bool upper_next = false;
    for (std::size_t index = 5; index < attribute_name.size(); ++index) {
        const char ch = attribute_name[index];
        if (ch == '-') {
            upper_next = true;
            continue;
        }
        if (upper_next && ch >= 'a' && ch <= 'z') {
            key.push_back(static_cast<char>(ch - 'a' + 'A'));
        } else {
            key.push_back(ch);
        }
        upper_next = false;
    }
    return key;
}

std::string dataset_key_to_data_attribute(std::string_view key) {
    std::string attribute = "data-";
    for (char ch : key) {
        if (ch >= 'A' && ch <= 'Z') {
            attribute.push_back('-');
            attribute.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            attribute.push_back(ch);
        }
    }
    return attribute;
}

bool dataset_key_is_writable(std::string_view key) {
    if (key.empty() || key.size() > 48 || key == "__proto__" || key == "constructor" || key == "prototype") {
        return false;
    }
    if (!(std::isalpha(static_cast<unsigned char>(key.front())) != 0 || key.front() == '_')) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
    });
}

std::string css_property_name_from_js(std::string_view key) {
    std::string property;
    for (char ch : key) {
        if (ch >= 'A' && ch <= 'Z') {
            property.push_back('-');
            property.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            property.push_back(ch);
        }
    }
    return property;
}

bool is_script_writable_style_property(const std::string& property) {
    if (property.size() > 2 && property[0] == '-' && property[1] == '-') {
        return true;
    }
    static constexpr std::string_view kWritableProperties[] = {
        "display",
        "color",
        "background",
        "background-color",
        "background-image",
        "text-align",
        "text-transform",
        "font-size",
        "font-weight",
        "line-height",
        "width",
        "height",
        "min-width",
        "min-height",
        "max-width",
        "max-height",
        "box-sizing",
        "margin",
        "margin-top",
        "margin-right",
        "margin-bottom",
        "margin-left",
        "padding",
        "padding-top",
        "padding-right",
        "padding-bottom",
        "padding-left",
        "opacity",
        "transform",
        "border-radius",
        "left",
        "top",
        "right",
        "bottom",
        "position",
        "visibility",
        "white-space",
        "text-overflow",
        "overflow",
        "overflow-y",
        "z-index",
    };
    return std::find(std::begin(kWritableProperties), std::end(kWritableProperties), std::string_view(property)) !=
        std::end(kWritableProperties);
}

std::vector<CssDeclaration> inline_declarations_for(const Node& node) {
    std::vector<CssDeclaration> declarations;
    const std::string& source = node.attribute("style");
    std::size_t index = 0;
    while (index < source.size()) {
        const std::size_t colon = source.find(':', index);
        if (colon == std::string::npos) {
            break;
        }
        const std::size_t end = source.find(';', colon + 1);
        const std::size_t value_end = end == std::string::npos ? source.size() : end;
        CssDeclaration declaration;
        declaration.property = ascii_lowercase(source.substr(index, colon - index));
        declaration.property.erase(declaration.property.begin(),
                                   std::find_if(declaration.property.begin(), declaration.property.end(),
                                                [](unsigned char ch) { return std::isspace(ch) == 0; }));
        declaration.property.erase(std::find_if(declaration.property.rbegin(), declaration.property.rend(),
                                                [](unsigned char ch) { return std::isspace(ch) == 0; }).base(),
                                   declaration.property.end());
        declaration.value = source.substr(colon + 1, value_end - colon - 1);
        declaration.value.erase(declaration.value.begin(),
                                std::find_if(declaration.value.begin(), declaration.value.end(),
                                             [](unsigned char ch) { return std::isspace(ch) == 0; }));
        declaration.value.erase(std::find_if(declaration.value.rbegin(), declaration.value.rend(),
                                             [](unsigned char ch) { return std::isspace(ch) == 0; }).base(),
                                declaration.value.end());
        if (!declaration.property.empty() && !declaration.value.empty()) {
            declarations.push_back(std::move(declaration));
        }
        if (end == std::string::npos) {
            break;
        }
        index = end + 1;
    }
    return declarations;
}

std::string inline_style_property(const Node& node, const std::string& property) {
    std::string output;
    for (const CssDeclaration& declaration : inline_declarations_for(node)) {
        if (declaration.property == property) {
            output = declaration.value;
        }
    }
    return output;
}

void set_inline_style_property(Node& node, const std::string& property, const std::string& value) {
    std::vector<CssDeclaration> declarations = inline_declarations_for(node);
    bool updated = false;
    for (CssDeclaration& declaration : declarations) {
        if (declaration.property == property) {
            declaration.value = value;
            updated = true;
        }
    }
    if (!updated) {
        declarations.push_back(CssDeclaration{property, value, false});
    }

    std::string style;
    for (const CssDeclaration& declaration : declarations) {
        if (declaration.property.empty() || declaration.value.empty()) {
            continue;
        }
        if (!style.empty()) {
            style += ' ';
        }
        style += declaration.property;
        style += ": ";
        style += declaration.value;
        style += ';';
    }
    node.set_attribute("style", std::move(style));
}

bool simple_selector_matches(const Node& node, std::string_view selector) {
    if (node.type != NodeType::Element) {
        return false;
    }
    std::string value(selector);
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) == 0;
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) == 0;
    }).base(), value.end());
    if (value.empty() || value.find(' ') != std::string::npos || value.find('>') != std::string::npos) {
        return false;
    }

    std::size_t index = 0;
    if (value[index] != '.' && value[index] != '#' && value[index] != '[') {
        const std::size_t begin = index;
        while (index < value.size() &&
               (std::isalnum(static_cast<unsigned char>(value[index])) != 0 ||
                value[index] == '-' || value[index] == '_')) {
            ++index;
        }
        if (node.tag_name != value.substr(begin, index - begin)) {
            return false;
        }
    }

    while (index < value.size()) {
        if (value[index] == '.') {
            const std::size_t begin = ++index;
            while (index < value.size() &&
                   (std::isalnum(static_cast<unsigned char>(value[index])) != 0 ||
                    value[index] == '-' || value[index] == '_')) {
                ++index;
            }
            if (index == begin) {
                return false;
            }
            if (!node.has_class(value.substr(begin, index - begin))) {
                return false;
            }
        } else if (value[index] == '#') {
            const std::size_t begin = ++index;
            while (index < value.size() &&
                   (std::isalnum(static_cast<unsigned char>(value[index])) != 0 ||
                    value[index] == '-' || value[index] == '_')) {
                ++index;
            }
            if (index == begin) {
                return false;
            }
            if (node.attribute("id") != value.substr(begin, index - begin)) {
                return false;
            }
        } else if (value[index] == '[') {
            const std::size_t close = value.find(']', index + 1);
            if (close == std::string::npos) {
                return false;
            }
            const std::string content = value.substr(index + 1, close - index - 1);
            const std::size_t equals = content.find('=');
            if (equals == std::string::npos) {
                if (!has_attribute(node, ascii_lowercase(content))) {
                    return false;
                }
            } else {
                const std::string name = ascii_lowercase(content.substr(0, equals));
                std::string expected = content.substr(equals + 1);
                if (expected.size() >= 2 &&
                    ((expected.front() == '"' && expected.back() == '"') ||
                     (expected.front() == '\'' && expected.back() == '\''))) {
                    expected = expected.substr(1, expected.size() - 2);
                }
                if (node.attribute(name) != expected) {
                    return false;
                }
            }
            index = close + 1;
        } else {
            return false;
        }
    }
    return true;
}

bool is_ancestor_of(const Node& possible_ancestor, const Node& node) {
    for (const Node* current = node.parent; current != nullptr; current = current->parent) {
        if (current == &possible_ancestor) {
            return true;
        }
    }
    return false;
}

jerry_value_t throw_type_error(const char* message) {
    return jerry_throw_sz(JERRY_ERROR_TYPE, message);
}

ScriptEventBinding* native_event_binding(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<ScriptEventBinding*>(jerry_object_get_native_ptr(object, &kEventNativeInfo));
}

Event* native_event(const jerry_value_t object) {
    ScriptEventBinding* binding = native_event_binding(object);
    if (binding == nullptr || !binding->active) {
        return nullptr;
    }
    return binding->event;
}

ScriptXmlHttpRequest* native_xhr(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    auto* xhr = static_cast<ScriptXmlHttpRequest*>(jerry_object_get_native_ptr(object, &kXhrNativeInfo));
    return xhr != nullptr && xhr->active ? xhr : nullptr;
}

AppLocalStorageShadow* native_local_storage(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    auto* binding =
        static_cast<ScriptLocalStorageBinding*>(jerry_object_get_native_ptr(object, &kLocalStorageNativeInfo));
    if (binding == nullptr || binding->runtime == nullptr) {
        return nullptr;
    }
    return ScriptRuntimeAccess::resolve_script_local_storage(*binding->runtime, *binding);
}

ScriptAudioElement* native_audio(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    auto* audio = static_cast<ScriptAudioElement*>(jerry_object_get_native_ptr(object, &kAudioNativeInfo));
    return audio != nullptr && audio->active ? audio : nullptr;
}

ScriptCanvasGradient* native_canvas_gradient(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<ScriptCanvasGradient*>(jerry_object_get_native_ptr(object, &kCanvasGradientNativeInfo));
}

ScriptFormData* native_form_data(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<ScriptFormData*>(jerry_object_get_native_ptr(object, &kFormDataNativeInfo));
}

std::size_t xhr_event_index(AppXhrEventKind kind) {
    return static_cast<std::size_t>(kind);
}

const char* xhr_event_type(AppXhrEventKind kind) {
    switch (kind) {
    case AppXhrEventKind::ReadyStateChange:
        return "readystatechange";
    case AppXhrEventKind::Load:
        return "load";
    case AppXhrEventKind::Error:
        return "error";
    case AppXhrEventKind::Timeout:
        return "timeout";
    case AppXhrEventKind::Abort:
        return "abort";
    case AppXhrEventKind::LoadEnd:
        return "loadend";
    }
    return "event";
}

std::size_t audio_event_index(ScriptAudioEventKind kind) {
    return kind == ScriptAudioEventKind::Error ? 1U : 0U;
}

ScriptAudioEventKind audio_event_kind_from_type(const std::string& type, bool* known = nullptr) {
    if (type == "ended") {
        if (known != nullptr) {
            *known = true;
        }
        return ScriptAudioEventKind::Ended;
    }
    if (type == "error") {
        if (known != nullptr) {
            *known = true;
        }
        return ScriptAudioEventKind::Error;
    }
    if (known != nullptr) {
        *known = false;
    }
    return ScriptAudioEventKind::Error;
}

const char* audio_event_type(ScriptAudioEventKind kind) {
    return kind == ScriptAudioEventKind::Error ? "error" : "ended";
}

bool same_js_value(jerry_value_t left, jerry_value_t right) {
    JerryValue result(jerry_binary_op(JERRY_BIN_OP_STRICT_EQUAL, left, right));
    return jerry_value_is_true(result.get());
}

bool object_bool_property(jerry_value_t object, const char* name) {
    JerryValue value(jerry_object_get_sz(object, name));
    return !jerry_value_is_exception(value.get()) && jerry_value_to_boolean(value.get());
}

std::uint32_t delay_from_value(jerry_value_t value) {
    JerryValue number_value(jerry_value_to_number(value));
    if (jerry_value_is_exception(number_value.get())) {
        return 0;
    }
    const double number = jerry_value_as_number(number_value.get());
    if (!std::isfinite(number) || number <= 0.0) {
        return 0;
    }
    constexpr double kMaxDelay = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::min(number, kMaxDelay));
}

std::uint32_t timer_id_from_value(jerry_value_t value) {
    JerryValue number_value(jerry_value_to_number(value));
    if (jerry_value_is_exception(number_value.get())) {
        return 0;
    }
    const double number = jerry_value_as_number(number_value.get());
    if (!std::isfinite(number) || number <= 0.0) {
        return 0;
    }
    constexpr double kMaxId = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::min(number, kMaxId));
}

double double_from_value(jerry_value_t value, double fallback = 0.0) {
    JerryValue number_value(jerry_value_to_number(value));
    if (jerry_value_is_exception(number_value.get())) {
        return fallback;
    }
    const double number = jerry_value_as_number(number_value.get());
    return std::isfinite(number) ? number : fallback;
}

int int_from_value(jerry_value_t value, int fallback = 0) {
    const double number = double_from_value(value, static_cast<double>(fallback));
    if (number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (number < static_cast<double>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(std::round(number));
}

double numeric_attribute(const Node& node, const char* name, double fallback) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    return end != value.c_str() && std::isfinite(parsed) ? parsed : fallback;
}

std::string number_attribute_value(jerry_value_t value) {
    const double number = double_from_value(value, 0.0);
    std::ostringstream stream;
    stream << number;
    return stream.str();
}

EventListenerOptions listener_options_from_value(jerry_value_t value) {
    EventListenerOptions options;
    if (jerry_value_is_boolean(value)) {
        options.capture = jerry_value_to_boolean(value);
        return options;
    }
    if (jerry_value_is_object(value)) {
        options.capture = object_bool_property(value, "capture");
        options.once = object_bool_property(value, "once");
    }
    return options;
}

void set_bool_property(jerry_value_t object, const char* name, bool value);

jerry_value_t event_prevent_default(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    ScriptEventBinding* binding = native_event_binding(call_info_p->this_value);
    if (binding == nullptr) {
        return throw_type_error("preventDefault called on non-event object");
    }
    if (binding->active && binding->event != nullptr) {
        binding->event->prevent_default();
        binding->default_prevented = binding->event->default_prevented();
    } else if (binding->cancelable) {
        binding->default_prevented = true;
    }
    set_bool_property(call_info_p->this_value, "defaultPrevented", binding->default_prevented);
    return jerry_undefined();
}

jerry_value_t event_stop_propagation(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    ScriptEventBinding* binding = native_event_binding(call_info_p->this_value);
    if (binding == nullptr) {
        return throw_type_error("stopPropagation called on non-event object");
    }
    if (binding->active && binding->event != nullptr) {
        binding->event->stop_propagation();
    }
    return jerry_undefined();
}

jerry_value_t event_stop_immediate_propagation(const jerry_call_info_t* call_info_p,
                                               const jerry_value_t[],
                                               const jerry_length_t) {
    ScriptEventBinding* binding = native_event_binding(call_info_p->this_value);
    if (binding == nullptr) {
        return throw_type_error("stopImmediatePropagation called on non-event object");
    }
    if (binding->active && binding->event != nullptr) {
        binding->event->stop_immediate_propagation();
    }
    return jerry_undefined();
}

jerry_value_t script_set_timeout(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || args_count < 1 || !jerry_value_is_function(args_p[0])) {
        return throw_type_error("setTimeout requires a function callback");
    }
    const std::uint32_t delay_ms = args_count > 1 ? delay_from_value(args_p[1]) : 0;
    const std::uint32_t id = ScriptRuntimeAccess::add_timer(*runtime, args_p[0], delay_ms, false);
    return jerry_number(id);
}

jerry_value_t script_set_interval(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || args_count < 1 || !jerry_value_is_function(args_p[0])) {
        return throw_type_error("setInterval requires a function callback");
    }
    const std::uint32_t delay_ms = args_count > 1 ? delay_from_value(args_p[1]) : 0;
    const std::uint32_t id = ScriptRuntimeAccess::add_timer(*runtime, args_p[0], delay_ms, true);
    return jerry_number(id);
}

jerry_value_t script_clear_timer(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime != nullptr && args_count > 0) {
        ScriptRuntimeAccess::clear_timer(*runtime, timer_id_from_value(args_p[0]));
    }
    return jerry_undefined();
}

jerry_value_t script_request_animation_frame(const jerry_call_info_t* call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || args_count < 1 || !jerry_value_is_function(args_p[0])) {
        return throw_type_error("requestAnimationFrame requires a function callback");
    }
    const std::uint32_t id = ScriptRuntimeAccess::add_animation_frame_callback(*runtime, args_p[0]);
    return jerry_number(id);
}

jerry_value_t script_cancel_animation_frame(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime != nullptr && args_count > 0) {
        ScriptRuntimeAccess::cancel_animation_frame_callback(*runtime, timer_id_from_value(args_p[0]));
    }
    return jerry_undefined();
}

void set_number_property(jerry_value_t object, const char* name, double value);
void set_bool_property(jerry_value_t object, const char* name, bool value);
void set_property(jerry_value_t object, const char* name, jerry_value_t value);
void set_method(jerry_value_t object, const char* name, jerry_external_handler_t handler);
jerry_value_t make_node_wrapper(JerryScriptRuntime& runtime, Node& node, bool document_methods);

jerry_value_t make_event_object(JerryScriptRuntime& runtime, Event& event) {
    JerryValue object(jerry_object());
    auto* binding = new ScriptEventBinding{&event, true, event.cancelable(), event.default_prevented()};
    jerry_object_set_native_ptr(object.get(), &kEventNativeInfo, binding);
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);

    set_property(object.get(), "type", string_to_value(event.type()).get());
    set_number_property(object.get(), "eventPhase", static_cast<int>(event.event_phase()));
    set_bool_property(object.get(), "bubbles", event.bubbles());
    set_bool_property(object.get(), "cancelable", event.cancelable());
    set_bool_property(object.get(), "defaultPrevented", event.default_prevented());

    if (event.target() != nullptr) {
        set_property(object.get(), "target", JerryValue(make_node_wrapper(runtime, *const_cast<Node*>(event.target()), false)).get());
    } else {
        set_property(object.get(), "target", jerry_null());
    }
    if (event.current_target() != nullptr) {
        set_property(object.get(), "currentTarget",
                     JerryValue(make_node_wrapper(runtime, *const_cast<Node*>(event.current_target()), false)).get());
    } else {
        set_property(object.get(), "currentTarget", jerry_null());
    }

    if (event.kind() == EventKind::Mouse || event.kind() == EventKind::Wheel) {
        const auto& mouse = static_cast<const MouseEvent&>(event);
        set_number_property(object.get(), "clientX", mouse.client_x);
        set_number_property(object.get(), "clientY", mouse.client_y);
        set_number_property(object.get(), "button", mouse.button);
        set_number_property(object.get(), "buttons", mouse.buttons);
        set_bool_property(object.get(), "altKey", mouse.alt_key);
        set_bool_property(object.get(), "ctrlKey", mouse.ctrl_key);
        set_bool_property(object.get(), "metaKey", mouse.meta_key);
        set_bool_property(object.get(), "shiftKey", mouse.shift_key);
    }
    if (event.kind() == EventKind::Wheel) {
        const auto& wheel = static_cast<const WheelEvent&>(event);
        set_number_property(object.get(), "deltaX", wheel.delta_x);
        set_number_property(object.get(), "deltaY", wheel.delta_y);
        set_number_property(object.get(), "deltaMode", wheel.delta_mode);
    }
    if (const auto* submit = dynamic_cast<const SubmitEvent*>(&event)) {
        if (submit->submitter() != nullptr) {
            set_property(object.get(), "submitter",
                         JerryValue(make_node_wrapper(runtime,
                                                      *const_cast<Node*>(submit->submitter()),
                                                      false)).get());
        } else {
            set_property(object.get(), "submitter", jerry_null());
        }
    }

    set_method(object.get(), "preventDefault", event_prevent_default);
    set_method(object.get(), "stopPropagation", event_stop_propagation);
    set_method(object.get(), "stopImmediatePropagation", event_stop_immediate_propagation);
    return object.release();
}

void invalidate_event_object(jerry_value_t object) {
    ScriptEventBinding* binding = native_event_binding(object);
    if (binding == nullptr) {
        return;
    }
    if (binding->event != nullptr) {
        binding->default_prevented = binding->event->default_prevented();
    }
    binding->event = nullptr;
    binding->active = false;
}

jerry_value_t make_window_event_object(const char* type, jerry_value_t target) {
    JerryValue object(jerry_object());
    set_property(object.get(), "type", string_to_value(type).get());
    set_number_property(object.get(), "eventPhase", 2);
    set_bool_property(object.get(), "bubbles", false);
    set_bool_property(object.get(), "cancelable", false);
    set_bool_property(object.get(), "defaultPrevented", false);
    if (jerry_value_is_object(target)) {
        JerryValue target_copy(jerry_value_copy(target));
        set_property(object.get(), "target", target_copy.get());
        set_property(object.get(), "currentTarget", target_copy.get());
    } else {
        set_property(object.get(), "target", jerry_null());
        set_property(object.get(), "currentTarget", jerry_null());
    }
    return object.release();
}

jerry_value_t node_get_text_content(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr) {
        return throw_type_error("textContent getter called on non-node object");
    }
    return jerry_string_sz(node->text_content().c_str());
}

jerry_value_t node_get_parent_element(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->parent == nullptr ||
        node->parent->type != NodeType::Element || node->parent->tag_name == "document") {
        return jerry_null();
    }
    return make_node_wrapper(*runtime, *node->parent, false);
}

jerry_value_t node_get_children(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr) {
        return jerry_array(0);
    }
    JerryValue array(jerry_array(0));
    std::uint32_t index = 0;
    for (const auto& child : node->children) {
        if (child->type != NodeType::Element) {
            continue;
        }
        JerryValue child_object(make_node_wrapper(*runtime, *child, false));
        JerryValue result(jerry_object_set_index(array.get(), index, child_object.get()));
        (void) result;
        ++index;
    }
    set_number_property(array.get(), "length", index);
    return array.release();
}

jerry_value_t node_set_text_content(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr) {
        return throw_type_error("textContent setter called on non-node object");
    }
    node->set_text_content(args_count > 0 ? value_to_string(args_p[0]) : std::string());
    return jerry_undefined();
}

jerry_value_t node_get_class_name(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    return jerry_string_sz(node->attribute("class").c_str());
}

jerry_value_t node_get_id(const jerry_call_info_t* call_info_p,
                          const jerry_value_t[],
                          const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    return jerry_string_sz(node->attribute("id").c_str());
}

jerry_value_t node_set_id(const jerry_call_info_t* call_info_p,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        node->set_attribute("id", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_set_class_name(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        node->set_attribute("class", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_reflected_string_attribute(const jerry_call_info_t* call_info_p,
                                                  const char* attribute_name) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    return jerry_string_sz(node->attribute(attribute_name).c_str());
}

jerry_value_t node_set_reflected_string_attribute(const jerry_call_info_t* call_info_p,
                                                  const jerry_value_t args_p[],
                                                  const jerry_length_t args_count,
                                                  const char* attribute_name) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        node->set_attribute(attribute_name, args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

#define JELLYFRAME_REFLECTED_STRING_ACCESSOR(js_name, attr_name) \
    jerry_value_t node_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return node_get_reflected_string_attribute(call_info_p, attr_name); \
    } \
    jerry_value_t node_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return node_set_reflected_string_attribute(call_info_p, args_p, args_count, attr_name); \
    }

JELLYFRAME_REFLECTED_STRING_ACCESSOR(title, "title")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(lang, "lang")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(dir, "dir")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(step, "step")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(name, "name")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(placeholder, "placeholder")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(wrap, "wrap")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(content, "content")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(httpEquiv, "http-equiv")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(media, "media")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(download, "download")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(ping, "ping")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(rel, "rel")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(referrerPolicy, "referrerpolicy")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(dateTime, "datetime")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(alt, "alt")
JELLYFRAME_REFLECTED_STRING_ACCESSOR(htmlFor, "for")

#undef JELLYFRAME_REFLECTED_STRING_ACCESSOR

jerry_value_t node_get_value_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t[],
                                       const jerry_length_t) {
    return node_get_reflected_string_attribute(call_info_p, "value");
}

jerry_value_t node_set_value_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    return node_set_reflected_string_attribute(call_info_p, args_p, args_count, "value");
}

jerry_value_t node_get_label_control(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "label") {
        return jerry_null();
    }
    const std::string& for_id = node->attribute("for");
    Node* control = !for_id.empty() ? find_by_id(root_node(*node), for_id) : first_labelable_descendant(*node);
    if (control == nullptr || !is_labelable_element(*control)) {
        return jerry_null();
    }
    return make_node_wrapper(*runtime, *control, false);
}

jerry_value_t node_get_reflected_number_attribute(const jerry_call_info_t* call_info_p,
                                                  const char* attribute_name,
                                                  double missing_value) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_number(missing_value);
    }
    return jerry_number(numeric_attribute(*node, attribute_name, missing_value));
}

jerry_value_t node_set_reflected_number_attribute(const jerry_call_info_t* call_info_p,
                                                  const jerry_value_t args_p[],
                                                  const jerry_length_t args_count,
                                                  const char* attribute_name) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && args_count > 0) {
        node->set_attribute(attribute_name, number_attribute_value(args_p[0]));
    }
    return jerry_undefined();
}

#define JELLYFRAME_REFLECTED_NUMBER_ACCESSOR(js_name, attr_name, missing_value) \
    jerry_value_t node_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return node_get_reflected_number_attribute(call_info_p, attr_name, missing_value); \
    } \
    jerry_value_t node_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return node_set_reflected_number_attribute(call_info_p, args_p, args_count, attr_name); \
    }

JELLYFRAME_REFLECTED_NUMBER_ACCESSOR(low, "low", 0.0)
JELLYFRAME_REFLECTED_NUMBER_ACCESSOR(high, "high", 1.0)
JELLYFRAME_REFLECTED_NUMBER_ACCESSOR(optimum, "optimum", 0.0)

#undef JELLYFRAME_REFLECTED_NUMBER_ACCESSOR

jerry_value_t node_get_min(const jerry_call_info_t* call_info_p,
                           const jerry_value_t[],
                           const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    if (node->tag_name == "meter") {
        return jerry_number(numeric_attribute(*node, "min", 0.0));
    }
    return jerry_string_sz(node->attribute("min").c_str());
}

jerry_value_t node_set_min(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        node->set_attribute("min", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_max(const jerry_call_info_t* call_info_p,
                           const jerry_value_t[],
                           const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    if (node->tag_name == "progress" || node->tag_name == "meter") {
        return jerry_number(numeric_attribute(*node, "max", 1.0));
    }
    return jerry_string_sz(node->attribute("max").c_str());
}

jerry_value_t node_set_max(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        node->set_attribute("max", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_reflected_int_attribute(const jerry_call_info_t* call_info_p,
                                               const char* attribute_name,
                                               int missing_value) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_number(missing_value);
    }
    const std::string& value = node->attribute(attribute_name);
    if (value.empty()) {
        return jerry_number(missing_value);
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return jerry_number(end != value.c_str() ? static_cast<int>(parsed) : missing_value);
}

jerry_value_t node_set_reflected_int_attribute(const jerry_call_info_t* call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count,
                                               const char* attribute_name) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && args_count > 0) {
        node->set_attribute(attribute_name, std::to_string(int_from_value(args_p[0], -1)));
    }
    return jerry_undefined();
}

#define JELLYFRAME_REFLECTED_INT_ACCESSOR(js_name, attr_name, missing_value) \
    jerry_value_t node_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return node_get_reflected_int_attribute(call_info_p, attr_name, missing_value); \
    } \
    jerry_value_t node_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return node_set_reflected_int_attribute(call_info_p, args_p, args_count, attr_name); \
    }

JELLYFRAME_REFLECTED_INT_ACCESSOR(maxLength, "maxlength", -1)
JELLYFRAME_REFLECTED_INT_ACCESSOR(minLength, "minlength", -1)
JELLYFRAME_REFLECTED_INT_ACCESSOR(rows, "rows", 2)
JELLYFRAME_REFLECTED_INT_ACCESSOR(cols, "cols", 20)
JELLYFRAME_REFLECTED_INT_ACCESSOR(size, "size", 0)
JELLYFRAME_REFLECTED_INT_ACCESSOR(tabIndex, "tabindex", -1)

#undef JELLYFRAME_REFLECTED_INT_ACCESSOR

std::vector<std::string> class_tokens_for(const Node& node) {
    std::vector<std::string> tokens;
    const std::string& value = node.attribute("class");
    std::size_t index = 0;
    while (index < value.size()) {
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) {
            ++index;
        }
        const std::size_t begin = index;
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) == 0) {
            ++index;
        }
        if (index > begin) {
            tokens.push_back(value.substr(begin, index - begin));
        }
    }
    return tokens;
}

void set_class_tokens(Node& node, const std::vector<std::string>& tokens) {
    std::string value;
    for (const std::string& token : tokens) {
        if (token.empty()) {
            continue;
        }
        if (!value.empty()) {
            value.push_back(' ');
        }
        value += token;
    }
    node.set_attribute("class", std::move(value));
}

bool class_token_valid(const std::string& token) {
    return !token.empty() &&
        std::find_if(token.begin(), token.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }) == token.end();
}

bool class_tokens_contains(const std::vector<std::string>& tokens, const std::string& token) {
    return std::find(tokens.begin(), tokens.end(), token) != tokens.end();
}

jerry_value_t class_list_contains(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return jerry_boolean(false);
    }
    const std::string token = value_to_string(args_p[0]);
    return jerry_boolean(class_token_valid(token) && class_tokens_contains(class_tokens_for(*node), token));
}

jerry_value_t class_list_add(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    std::vector<std::string> tokens = class_tokens_for(*node);
    bool changed = false;
    for (jerry_length_t index = 0; index < args_count; ++index) {
        const std::string token = value_to_string(args_p[index]);
        if (!class_token_valid(token) || class_tokens_contains(tokens, token)) {
            continue;
        }
        tokens.push_back(token);
        changed = true;
    }
    if (changed) {
        set_class_tokens(*node, tokens);
    }
    return jerry_undefined();
}

jerry_value_t class_list_remove(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    std::vector<std::string> tokens = class_tokens_for(*node);
    const std::size_t before = tokens.size();
    for (jerry_length_t index = 0; index < args_count; ++index) {
        const std::string token = value_to_string(args_p[index]);
        if (!class_token_valid(token)) {
            continue;
        }
        tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
    }
    if (tokens.size() != before) {
        set_class_tokens(*node, tokens);
    }
    return jerry_undefined();
}

jerry_value_t class_list_toggle(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return jerry_boolean(false);
    }
    const std::string token = value_to_string(args_p[0]);
    if (!class_token_valid(token)) {
        return jerry_boolean(false);
    }
    std::vector<std::string> tokens = class_tokens_for(*node);
    const bool contained = class_tokens_contains(tokens, token);
    const bool should_have = args_count >= 2 ? jerry_value_to_boolean(args_p[1]) : !contained;
    if (should_have && !contained) {
        tokens.push_back(token);
        set_class_tokens(*node, tokens);
    } else if (!should_have && contained) {
        tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
        set_class_tokens(*node, tokens);
    }
    return jerry_boolean(should_have);
}

jerry_value_t class_list_replace(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 2) {
        return jerry_boolean(false);
    }

    const std::string old_token = value_to_string(args_p[0]);
    const std::string new_token = value_to_string(args_p[1]);
    if (!class_token_valid(old_token) || !class_token_valid(new_token)) {
        return jerry_boolean(false);
    }

    std::vector<std::string> tokens = class_tokens_for(*node);
    const auto old = std::find(tokens.begin(), tokens.end(), old_token);
    if (old == tokens.end()) {
        return jerry_boolean(false);
    }
    if (old_token == new_token) {
        return jerry_boolean(true);
    }

    const auto existing_new = std::find(tokens.begin(), tokens.end(), new_token);
    if (existing_new != tokens.end()) {
        tokens.erase(old);
    } else {
        *old = new_token;
    }
    set_class_tokens(*node, tokens);
    return jerry_boolean(true);
}

jerry_value_t node_get_class_list(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    JerryValue object(jerry_object());
    bind_native_node(object.get(), *runtime, *node);
    set_method(object.get(), "contains", class_list_contains);
    set_method(object.get(), "add", class_list_add);
    set_method(object.get(), "remove", class_list_remove);
    set_method(object.get(), "toggle", class_list_toggle);
    set_method(object.get(), "replace", class_list_replace);
    return object.release();
}

jerry_value_t node_ignore_setter(const jerry_call_info_t*,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    return jerry_undefined();
}

jerry_value_t node_get_hidden(const jerry_call_info_t* call_info_p,
                              const jerry_value_t[],
                              const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "hidden"));
}

jerry_value_t node_get_autofocus(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "autofocus"));
}

jerry_value_t node_set_autofocus(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element) {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("autofocus", "");
        } else {
            node->remove_attribute("autofocus");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_set_hidden(const jerry_call_info_t* call_info_p,
                              const jerry_value_t args_p[],
                              const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr) {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("hidden", "");
        } else {
            node->remove_attribute("hidden");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_disabled(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "disabled"));
}

jerry_value_t node_set_disabled(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr) {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("disabled", "");
        } else {
            node->remove_attribute("disabled");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_read_only(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "readonly"));
}

jerry_value_t node_set_read_only(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr) {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("readonly", "");
        } else {
            node->remove_attribute("readonly");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_open(const jerry_call_info_t* call_info_p,
                            const jerry_value_t[],
                            const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "open"));
}

jerry_value_t node_set_open(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node != nullptr) {
        const bool open = args_count > 0 && jerry_value_to_boolean(args_p[0]);
        if (runtime != nullptr && node->type == NodeType::Element && node->tag_name == "dialog") {
            ScriptRuntimeAccess::set_dialog_open(*runtime, *node, open);
        } else if (open) {
            node->set_attribute("open", "");
        } else {
            node->remove_attribute("open");
        }
    }
    return jerry_undefined();
}

jerry_value_t dialog_get_return_value(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "dialog") {
        return jerry_string_sz("");
    }
    return jerry_string_sz(ScriptRuntimeAccess::dialog_return_value(*runtime, *node).c_str());
}

jerry_value_t dialog_set_return_value(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node != nullptr && runtime != nullptr && node->type == NodeType::Element && node->tag_name == "dialog") {
        ScriptRuntimeAccess::set_dialog_return_value(
            *runtime, *node, args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t dialog_show_modal(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "dialog") {
        return throw_type_error("showModal requires a dialog element");
    }
    if (!ScriptRuntimeAccess::show_modal_dialog(*runtime, *node)) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, "InvalidStateError: another dialog is already open");
    }
    return jerry_undefined();
}

jerry_value_t dialog_close(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "dialog") {
        return throw_type_error("close requires a dialog element");
    }
    ScriptRuntimeAccess::close_dialog(
        *runtime, *node, args_count > 0 ? value_to_string(args_p[0]) : std::string(), args_count > 0);
    return jerry_undefined();
}

jerry_value_t node_get_required(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && has_attribute(*node, "required"));
}

jerry_value_t node_set_required(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr) {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("required", "");
        } else {
            node->remove_attribute("required");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_default_value(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    if (node->tag_name == "textarea") {
        return jerry_string_sz(node->text_content().c_str());
    }
    return jerry_string_sz(node->attribute("value").c_str());
}

jerry_value_t node_set_default_value(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    const std::string value = args_count > 0 ? value_to_string(args_p[0]) : std::string();
    if (node->tag_name == "textarea") {
        node->set_text_content(value);
    } else {
        node->set_attribute("value", value);
    }
    return jerry_undefined();
}

jerry_value_t node_get_type(const jerry_call_info_t* call_info_p,
                            const jerry_value_t[],
                            const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element) {
        return jerry_undefined();
    }
    if (node->tag_name == "textarea") {
        return jerry_string_sz("textarea");
    }
    if (node->tag_name == "select") {
        return jerry_string_sz("select-one");
    }
    if (node->tag_name == "button") {
        const std::string type = ascii_lowercase(node->attribute("type"));
        if (type == "button" || type == "reset" || type == "submit") {
            return jerry_string_sz(type.c_str());
        }
        return jerry_string_sz("submit");
    }
    if (node->tag_name == "input") {
        const std::string type = ascii_lowercase(node->attribute("type"));
        static constexpr std::string_view kSupportedInputTypes[] = {
            "button", "checkbox", "color", "date", "datetime-local", "file", "image", "radio", "range",
            "reset", "submit", "text", "time", "search", "tel", "url", "email", "number",
        };
        if (std::find(std::begin(kSupportedInputTypes), std::end(kSupportedInputTypes), std::string_view(type)) !=
            std::end(kSupportedInputTypes)) {
            return jerry_string_sz(type.c_str());
        }
        return jerry_string_sz("text");
    }
    return jerry_string_sz(node->attribute("type").c_str());
}

jerry_value_t node_set_type(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element &&
        (node->tag_name == "input" || node->tag_name == "button")) {
        node->set_attribute("type", args_count > 0 ? ascii_lowercase(value_to_string(args_p[0])) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_text_length(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "textarea") {
        return jerry_number(0);
    }
    return jerry_number(static_cast<double>(utf8_codepoint_count(form_control_value(*node))));
}

jerry_value_t node_get_position(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "progress") {
        return jerry_number(-1);
    }
    if (!has_attribute(*node, "value")) {
        return jerry_number(-1);
    }
    const double max = std::max(0.0, numeric_attribute(*node, "max", 1.0));
    if (max <= 0.0) {
        return jerry_number(-1);
    }
    const double value = std::max(0.0, std::min(numeric_attribute(*node, "value", 0.0), max));
    return jerry_number(value / max);
}

jerry_value_t node_get_label(const jerry_call_info_t* call_info_p,
                             const jerry_value_t[],
                             const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || !is_option_or_optgroup(*node)) {
        return jerry_undefined();
    }
    const std::string& label = node->attribute("label");
    return jerry_string_sz(!label.empty() || node->tag_name == "optgroup" ? label.c_str() : node->text_content().c_str());
}

jerry_value_t node_set_label(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && is_option_or_optgroup(*node)) {
        node->set_attribute("label", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_default_selected(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t[],
                                        const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && node->type == NodeType::Element &&
                         node->tag_name == "option" && has_attribute(*node, "selected"));
}

jerry_value_t node_set_default_selected(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "option") {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("selected", "");
        } else {
            node->remove_attribute("selected");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_default_checked(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t[],
                                       const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && node->type == NodeType::Element &&
                         node->tag_name == "input" && has_attribute(*node, "checked"));
}

jerry_value_t node_set_default_checked(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "input") {
        if (args_count > 0 && jerry_value_to_boolean(args_p[0])) {
            node->set_attribute("checked", "");
        } else {
            node->remove_attribute("checked");
        }
    }
    return jerry_undefined();
}

jerry_value_t node_get_option_value(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "option") {
        return jerry_undefined();
    }
    const std::string& value = node->attribute("value");
    return jerry_string_sz(!value.empty() ? value.c_str() : node->text_content().c_str());
}

jerry_value_t node_set_option_value(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "option") {
        node->set_attribute("value", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_option_text(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "option") {
        return jerry_undefined();
    }
    return jerry_string_sz(node->text_content().c_str());
}

jerry_value_t node_set_option_text(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t args_p[],
                                   const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "option") {
        node->set_text_content(args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t node_get_option_index(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "option") {
        return jerry_number(-1);
    }
    Node* select = closest_ancestor_select(*node);
    return jerry_number(select != nullptr ? option_index_in_select(*select, *node) : -1);
}

jerry_value_t node_get_value(const jerry_call_info_t* call_info_p,
                             const jerry_value_t[],
                             const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && is_progress_or_meter(*node)) {
        return jerry_number(numeric_attribute(*node, "value", 0.0));
    }
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "button") {
        return jerry_string_sz(node->attribute("value").c_str());
    }
    if (node == nullptr || !is_form_control(*node)) {
        return jerry_undefined();
    }
    return jerry_string_sz(form_control_value(*node).c_str());
}

jerry_value_t node_set_value(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr && is_progress_or_meter(*node)) {
        node->set_attribute("value", args_count > 0 ? number_attribute_value(args_p[0]) : std::string());
        return jerry_undefined();
    }
    if (node != nullptr && node->type == NodeType::Element && node->tag_name == "button") {
        node->set_attribute("value", args_count > 0 ? value_to_string(args_p[0]) : std::string());
        return jerry_undefined();
    }
    if (node == nullptr || !is_form_control(*node)) {
        return jerry_undefined();
    }
    set_form_control_value(*node, args_count > 0 ? value_to_string(args_p[0]) : std::string());
    return jerry_undefined();
}

jerry_value_t node_get_checked(const jerry_call_info_t* call_info_p,
                               const jerry_value_t[],
                               const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && form_control_checked(*node));
}

jerry_value_t node_set_checked(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node != nullptr) {
        set_form_control_checked(*node, args_count > 0 && jerry_value_to_boolean(args_p[0]));
    }
    return jerry_undefined();
}

jerry_value_t node_get_selected_index(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_number(node != nullptr ? form_control_selected_index(*node) : -1);
}

jerry_value_t node_set_selected_index(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || args_count == 0) {
        return jerry_undefined();
    }
    const std::string text = value_to_string(args_p[0]);
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str()) {
        set_form_control_selected_index(*node, static_cast<int>(parsed));
    }
    return jerry_undefined();
}

jerry_value_t node_append_child(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count);
jerry_value_t node_remove_child(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count);
jerry_value_t element_set_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count);
jerry_value_t element_get_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count);
jerry_value_t document_get_element_by_id(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count);
jerry_value_t document_get_body(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t);
jerry_value_t document_get_head(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t);
jerry_value_t document_get_title_attr(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t);
jerry_value_t document_set_title_attr(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t document_get_dir(const jerry_call_info_t* call_info_p,
                               const jerry_value_t[],
                               const jerry_length_t);
jerry_value_t document_set_dir(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count);
jerry_value_t document_get_images(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count);
jerry_value_t document_get_embeds(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count);
jerry_value_t document_get_links(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count);
jerry_value_t document_get_forms(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count);
jerry_value_t document_get_scripts(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t args_p[],
                                   const jerry_length_t args_count);
jerry_value_t document_get_elements_by_name(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count);
jerry_value_t document_create_element(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t document_create_text_node(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count);
jerry_value_t document_get_hidden(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count);
jerry_value_t document_get_visibility_state(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count);
jerry_value_t document_get_ready_state(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count);
jerry_value_t document_get_default_view(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count);
jerry_value_t document_has_focus(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count);
jerry_value_t node_click(const jerry_call_info_t* call_info_p,
                         const jerry_value_t args_p[],
                         const jerry_length_t args_count);
jerry_value_t node_add_event_listener(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t node_remove_event_listener(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count);
jerry_value_t window_add_event_listener(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count);
jerry_value_t window_remove_event_listener(const jerry_call_info_t* call_info_p,
                                           const jerry_value_t args_p[],
                                           const jerry_length_t args_count);
#define JELLYFRAME_NODE_EVENT_HANDLER_LIST(X) \
    X(onclick, "click") \
    X(oninput, "input") \
    X(onchange, "change") \
    X(ontoggle, "toggle") \
    X(oncancel, "cancel") \
    X(onclose, "close") \
    X(onpointerdown, "pointerdown") \
    X(onpointerup, "pointerup") \
    X(ontouchstart, "touchstart") \
    X(ontouchend, "touchend") \
    X(onwheel, "wheel") \
    X(onmousedown, "mousedown") \
    X(onmouseup, "mouseup") \
    X(onmousemove, "mousemove") \
    X(onmouseover, "mouseover") \
    X(onmouseout, "mouseout") \
    X(onfocus, "focus") \
    X(onblur, "blur") \
    X(onvisibilitychange, "visibilitychange")

#define JELLYFRAME_WINDOW_EVENT_HANDLER_LIST(X) \
    X(ononline, "online") \
    X(onoffline, "offline") \
    X(onhashchange, "hashchange") \
    X(onpopstate, "popstate")

#define JELLYFRAME_DECLARE_NODE_EVENT_HANDLER(js_name, event_type) \
    jerry_value_t node_get_##js_name(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t); \
    jerry_value_t node_set_##js_name(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t);

#define JELLYFRAME_DECLARE_WINDOW_EVENT_HANDLER(js_name, event_type) \
    jerry_value_t window_get_##js_name(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t); \
    jerry_value_t window_set_##js_name(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t);

JELLYFRAME_NODE_EVENT_HANDLER_LIST(JELLYFRAME_DECLARE_NODE_EVENT_HANDLER)
JELLYFRAME_WINDOW_EVENT_HANDLER_LIST(JELLYFRAME_DECLARE_WINDOW_EVENT_HANDLER)

#undef JELLYFRAME_DECLARE_NODE_EVENT_HANDLER
#undef JELLYFRAME_DECLARE_WINDOW_EVENT_HANDLER
jerry_value_t node_matches(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count);
jerry_value_t node_closest(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count);
jerry_value_t node_query_selector(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count);
jerry_value_t node_query_selector_all(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t document_query_selector(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t document_query_selector_all(const jerry_call_info_t* call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count);
jerry_value_t element_remove_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count);
jerry_value_t element_has_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count);
jerry_value_t element_toggle_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count);
jerry_value_t node_remove(const jerry_call_info_t* call_info_p,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count);

void set_property(jerry_value_t object, const char* name, jerry_value_t value) {
    JerryValue result(jerry_object_set_sz(object, name, value));
    (void) result;
}

void delete_property(jerry_value_t object, const char* name) {
    JerryValue result(jerry_object_delete_sz(object, name));
    (void) result;
}

void set_number_property(jerry_value_t object, const char* name, double value) {
    set_property(object, name, JerryValue(jerry_number(value)).get());
}

void set_bool_property(jerry_value_t object, const char* name, bool value) {
    set_property(object, name, JerryValue(jerry_boolean(value)).get());
}

void set_method(jerry_value_t object, const char* name, jerry_external_handler_t handler) {
    JerryValue function(jerry_function_external(handler));
    set_property(object, name, function.get());
}

void set_runtime_method(jerry_value_t object,
                        const char* name,
                        jerry_external_handler_t handler,
                        JerryScriptRuntime& runtime) {
    JerryValue function(jerry_function_external(handler));
    jerry_object_set_native_ptr(function.get(), &kRuntimeNativeInfo, &runtime);
    set_property(object, name, function.get());
}

void define_accessor(jerry_value_t object,
                     const char* property,
                     jerry_external_handler_t getter,
                     jerry_external_handler_t setter) {
    jerry_property_descriptor_t descriptor = jerry_property_descriptor();
    descriptor.flags = JERRY_PROP_IS_GET_DEFINED | JERRY_PROP_IS_SET_DEFINED |
        JERRY_PROP_IS_CONFIGURABLE_DEFINED | JERRY_PROP_IS_CONFIGURABLE;
    descriptor.getter = jerry_function_external(getter);
    descriptor.setter = jerry_function_external(setter);

    JerryValue name(jerry_string_sz(property));
    JerryValue result(jerry_object_define_own_prop(object, name.get(), &descriptor));
    (void) result;
    jerry_property_descriptor_free(&descriptor);
}

jerry_value_t navigator_get_on_line(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    return jerry_boolean(runtime != nullptr && ScriptRuntimeAccess::system_state(*runtime).navigator_online);
}

jerry_value_t script_date_now(const jerry_call_info_t* call_info_p,
                              const jerry_value_t[],
                              const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr) {
        runtime = native_runtime(call_info_p->this_value);
    }
    return jerry_number(runtime != nullptr ? static_cast<double>(ScriptRuntimeAccess::current_time_ms(*runtime)) : 0.0);
}

jerry_value_t script_btoa(const jerry_call_info_t*,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count) {
    if (args_count == 0) {
        return throw_type_error("btoa requires an input string");
    }
    std::string binary;
    if (!html_binary_string_from_utf8(value_to_string(args_p[0]), binary)) {
        return throw_type_error("btoa input contains characters outside the byte range");
    }
    return jerry_string_sz(base64_encode(binary).c_str());
}

jerry_value_t script_atob(const jerry_call_info_t*,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count) {
    if (args_count == 0) {
        return throw_type_error("atob requires an input string");
    }
    std::string decoded;
    if (!base64_decode_html(value_to_string(args_p[0]), decoded)) {
        return throw_type_error("atob input is not valid base64");
    }
    return string_to_value(html_binary_string_to_utf8(decoded)).release();
}

jerry_value_t make_geolocation_error_object(int code, const char* message) {
    JerryValue object(jerry_object());
    set_number_property(object.get(), "code", code);
    set_property(object.get(), "message", string_to_value(message).get());
    return object.release();
}

int geolocation_error_code(AppDeviceFailureReason reason) {
    switch (reason) {
    case AppDeviceFailureReason::CapabilityDenied:
        return 1; // PERMISSION_DENIED
    case AppDeviceFailureReason::RequestTimeout:
        return 3; // TIMEOUT
    case AppDeviceFailureReason::None:
    case AppDeviceFailureReason::EmptyInstance:
    case AppDeviceFailureReason::InvalidRequest:
    case AppDeviceFailureReason::QueueFull:
    case AppDeviceFailureReason::SampleUnavailable:
    case AppDeviceFailureReason::RecordBudgetExceeded:
    case AppDeviceFailureReason::HandleBudgetExceeded:
    case AppDeviceFailureReason::RequestFailed:
    case AppDeviceFailureReason::RequestCancelled:
    case AppDeviceFailureReason::Unsupported:
    case AppDeviceFailureReason::Unknown:
        break;
    }
    return 2; // POSITION_UNAVAILABLE
}

const char* geolocation_error_message(AppDeviceFailureReason reason) {
    switch (reason) {
    case AppDeviceFailureReason::CapabilityDenied:
        return "geolocation permission denied";
    case AppDeviceFailureReason::RequestTimeout:
        return "geolocation request timed out";
    case AppDeviceFailureReason::SampleUnavailable:
        return "geolocation position unavailable";
    case AppDeviceFailureReason::QueueFull:
    case AppDeviceFailureReason::RecordBudgetExceeded:
    case AppDeviceFailureReason::HandleBudgetExceeded:
        return "geolocation budget exceeded";
    case AppDeviceFailureReason::RequestCancelled:
        return "geolocation request cancelled";
    case AppDeviceFailureReason::Unsupported:
        return "geolocation unsupported";
    case AppDeviceFailureReason::None:
    case AppDeviceFailureReason::EmptyInstance:
    case AppDeviceFailureReason::InvalidRequest:
    case AppDeviceFailureReason::RequestFailed:
    case AppDeviceFailureReason::Unknown:
        break;
    }
    return "geolocation request failed";
}

void call_geolocation_error(JerryScriptRuntime& runtime,
                            jerry_value_t callback,
                            AppDeviceFailureReason reason) {
    if (callback == 0 || !jerry_value_is_function(callback)) {
        return;
    }
    JerryValue error(make_geolocation_error_object(geolocation_error_code(reason),
                                                   geolocation_error_message(reason)));
    const jerry_value_t arg = error.get();
    JerryValue result(run_with_execution_budget(runtime, [&]() {
        return jerry_call(callback, jerry_undefined(), &arg, 1);
    }));
    if (jerry_value_is_exception(result.get())) {
        JerryValue exception_value(jerry_exception_value(result.release(), true));
        (void) exception_value;
    }
}

jerry_value_t make_geolocation_position_object(const AppLocationSnapshotRecord& snapshot) {
    JerryValue coords(jerry_object());
    set_number_property(coords.get(), "latitude", snapshot.latitude);
    set_number_property(coords.get(), "longitude", snapshot.longitude);
    set_number_property(coords.get(), "accuracy", snapshot.accuracy_m);
    set_number_property(coords.get(), "altitude", snapshot.altitude_m);
    set_property(coords.get(), "altitudeAccuracy", jerry_null());
    set_property(coords.get(), "heading", jerry_null());
    set_number_property(coords.get(), "speed", snapshot.speed_mps);

    JerryValue position(jerry_object());
    set_property(position.get(), "coords", coords.get());
    set_number_property(position.get(), "timestamp", static_cast<double>(snapshot.timestamp_ms));
    return position.release();
}

jerry_value_t geolocation_get_current_position(const jerry_call_info_t* call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || args_count < 1 || !jerry_value_is_function(args_p[0])) {
        return throw_type_error("geolocation.getCurrentPosition requires a success callback");
    }

    AppRuntimeHost* host = ScriptRuntimeAccess::app_host(*runtime);
    AppLocationSnapshotMock* location = ScriptRuntimeAccess::location_snapshot(*runtime);
    const jerry_value_t error_callback =
        (args_count > 1 && jerry_value_is_function(args_p[1])) ? args_p[1] : 0;
    if (host == nullptr || location == nullptr) {
        call_geolocation_error(*runtime, error_callback, AppDeviceFailureReason::Unsupported);
        return jerry_undefined();
    }
    if (!ScriptRuntimeAccess::can_create_geolocation_request(*runtime)) {
        call_geolocation_error(*runtime, error_callback, AppDeviceFailureReason::RecordBudgetExceeded);
        return jerry_undefined();
    }

    const AppServiceSubmitResult submitted = location->submit_position(*host);
    if (!submitted.accepted()) {
        call_geolocation_error(*runtime,
                               error_callback,
                               classify_app_device_failure(submitted.status,
                                                           submitted.rejected_status,
                                                           submitted.error_code));
        return jerry_undefined();
    }
    if (ScriptRuntimeAccess::create_geolocation_request(*runtime,
                                                        submitted.job_id,
                                                        args_p[0],
                                                        error_callback) == nullptr) {
        // Capacity was checked before submit; this is a defensive fallback.
        call_geolocation_error(*runtime, error_callback, AppDeviceFailureReason::RecordBudgetExceeded);
    }
    return jerry_undefined();
}

jerry_value_t make_geolocation_object(JerryScriptRuntime& runtime) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    set_runtime_method(object.get(), "getCurrentPosition", geolocation_get_current_position, runtime);
    return object.release();
}

jerry_value_t make_host_data_battery_object(const AppHostDataSnapshot& snapshot) {
    JerryValue object(jerry_object());
    set_number_property(object.get(), "timestamp", static_cast<double>(snapshot.battery.timestamp_ms));
    set_number_property(object.get(), "percent", snapshot.battery.percent);
    set_bool_property(object.get(), "charging", snapshot.battery.charging);
    return object.release();
}

jerry_value_t make_host_data_weather_object(const AppHostDataSnapshot& snapshot) {
    JerryValue object(jerry_object());
    set_number_property(object.get(), "timestamp", static_cast<double>(snapshot.weather.timestamp_ms));
    set_property(object.get(), "condition", string_to_value(app_weather_condition_name(snapshot.weather.condition)).get());
    set_number_property(object.get(), "temperatureC", static_cast<double>(snapshot.weather.temperature_c_x10) / 10.0);
    set_number_property(object.get(), "feelsLikeC", static_cast<double>(snapshot.weather.feels_like_c_x10) / 10.0);
    set_number_property(object.get(), "humidity", snapshot.weather.humidity_percent);
    set_number_property(object.get(), "windSpeedMps", static_cast<double>(snapshot.weather.wind_speed_mps_x10) / 10.0);
    set_number_property(object.get(), "precipitationMm", static_cast<double>(snapshot.weather.precipitation_mm_x10) / 10.0);
    set_number_property(object.get(), "airQualityIndex", snapshot.weather.air_quality_index);
    return object.release();
}

jerry_value_t make_host_data_activity_object(const AppHostDataSnapshot& snapshot) {
    JerryValue object(jerry_object());
    set_number_property(object.get(), "timestamp", static_cast<double>(snapshot.activity.timestamp_ms));
    set_number_property(object.get(), "steps", snapshot.activity.steps);
    set_number_property(object.get(), "activeMinutes", snapshot.activity.active_minutes);
    set_number_property(object.get(), "caloriesKcal", snapshot.activity.calories_kcal);
    set_number_property(object.get(), "distanceM", snapshot.activity.distance_m);
    return object.release();
}

jerry_value_t host_data_get_snapshot(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    const AppHostDataSnapshot* snapshot = runtime != nullptr
        ? ScriptRuntimeAccess::host_data_snapshot(*runtime)
        : nullptr;
    if (snapshot == nullptr) {
        return jerry_null();
    }
    const AppHostDataAccessPolicy* policy = ScriptRuntimeAccess::host_data_access_policy(*runtime);
    if (policy == nullptr) {
        return jerry_null();
    }

    const AppHostDataSnapshot filtered = app_host_data_filter_for_app(*snapshot, *policy);
    JerryValue result(jerry_object());
    if (filtered.has.battery) {
        JerryValue battery(make_host_data_battery_object(filtered));
        set_property(result.get(), "battery", battery.get());
    } else {
        set_property(result.get(), "battery", jerry_null());
    }
    if (filtered.has.weather) {
        JerryValue weather(make_host_data_weather_object(filtered));
        set_property(result.get(), "weather", weather.get());
    } else {
        set_property(result.get(), "weather", jerry_null());
    }
    if (filtered.has.activity) {
        JerryValue activity(make_host_data_activity_object(filtered));
        set_property(result.get(), "activity", activity.get());
    } else {
        set_property(result.get(), "activity", jerry_null());
    }
    return result.release();
}

jerry_value_t make_jellyframe_host_data_object(JerryScriptRuntime& runtime) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    set_runtime_method(object.get(), "getSnapshot", host_data_get_snapshot, runtime);
    return object.release();
}

jerry_value_t make_navigator_object(JerryScriptRuntime& runtime) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    define_accessor(object.get(), "onLine", navigator_get_on_line, node_ignore_setter);
    if (ScriptRuntimeAccess::location_snapshot(runtime) != nullptr) {
        JerryValue geolocation(make_geolocation_object(runtime));
        set_property(object.get(), "geolocation", geolocation.get());
    }
    if (ScriptRuntimeAccess::host_data_snapshot(runtime) != nullptr &&
        (ScriptRuntimeAccess::host_data_battery_allowed(runtime) ||
         ScriptRuntimeAccess::host_data_weather_allowed(runtime) ||
         ScriptRuntimeAccess::host_data_activity_allowed(runtime))) {
        JerryValue jellyframe(make_jellyframe_host_data_object(runtime));
        set_property(object.get(), "jellyframe", jellyframe.get());
    }
    return object.release();
}

jerry_value_t location_get_hash(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    return string_to_value(runtime != nullptr ? ScriptRuntimeAccess::location_hash(*runtime) : std::string()).release();
}

jerry_value_t location_set_hash(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime != nullptr) {
        ScriptRuntimeAccess::set_location_hash(*runtime,
                                               args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t make_location_object(JerryScriptRuntime& runtime) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    define_accessor(object.get(), "hash", location_get_hash, location_set_hash);
    return object.release();
}

jerry_value_t history_get_length(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    return jerry_number(runtime != nullptr ? static_cast<double>(ScriptRuntimeAccess::route_history_length(*runtime)) : 0.0);
}

bool history_url_to_fragment(const jerry_value_t args_p[], jerry_length_t args_count, std::string& fragment) {
    if (args_count < 3 || jerry_value_is_undefined(args_p[2]) || jerry_value_is_null(args_p[2])) {
        return true;
    }
    const std::string url = value_to_string(args_p[2]);
    if (!url.empty() && url.front() != '#') {
        return false;
    }
    fragment = url.empty() ? std::string() : url.substr(1);
    return true;
}

jerry_value_t history_push_state(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        return throw_type_error("history is not bound to a document");
    }
    std::string fragment = ScriptRuntimeAccess::location_hash(*runtime);
    if (!fragment.empty()) {
        fragment.erase(0, 1);
    }
    if (!history_url_to_fragment(args_p, args_count, fragment)) {
        return throw_type_error("history only accepts an empty or fragment URL");
    }
    ScriptRuntimeAccess::push_route_history(*runtime, std::move(fragment));
    return jerry_undefined();
}

jerry_value_t history_replace_state(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        return throw_type_error("history is not bound to a document");
    }
    std::string fragment = ScriptRuntimeAccess::location_hash(*runtime);
    if (!fragment.empty()) {
        fragment.erase(0, 1);
    }
    if (!history_url_to_fragment(args_p, args_count, fragment)) {
        return throw_type_error("history only accepts an empty or fragment URL");
    }
    ScriptRuntimeAccess::replace_route_history(*runtime, std::move(fragment));
    return jerry_undefined();
}

jerry_value_t history_go(const jerry_call_info_t* call_info_p,
                         const jerry_value_t args_p[],
                         const jerry_length_t args_count) {
    if (JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value); runtime != nullptr) {
        ScriptRuntimeAccess::traverse_route_history(*runtime, args_count > 0 ? int_from_value(args_p[0], 0) : 0);
    }
    return jerry_undefined();
}

jerry_value_t history_back(const jerry_call_info_t* call_info_p,
                           const jerry_value_t[],
                           const jerry_length_t) {
    if (JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value); runtime != nullptr) {
        ScriptRuntimeAccess::traverse_route_history(*runtime, -1);
    }
    return jerry_undefined();
}

jerry_value_t history_forward(const jerry_call_info_t* call_info_p,
                              const jerry_value_t[],
                              const jerry_length_t) {
    if (JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value); runtime != nullptr) {
        ScriptRuntimeAccess::traverse_route_history(*runtime, 1);
    }
    return jerry_undefined();
}

jerry_value_t make_history_object(JerryScriptRuntime& runtime) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    define_accessor(object.get(), "length", history_get_length, node_ignore_setter);
    set_method(object.get(), "pushState", history_push_state);
    set_method(object.get(), "replaceState", history_replace_state);
    set_method(object.get(), "go", history_go);
    set_method(object.get(), "back", history_back);
    set_method(object.get(), "forward", history_forward);
    return object.release();
}

jerry_value_t make_xhr_event_object(ScriptXmlHttpRequest& xhr, AppXhrEventKind kind) {
    JerryValue object(jerry_object());
    set_property(object.get(), "type", string_to_value(xhr_event_type(kind)).get());
    if (xhr.object != 0) {
        JerryValue target(jerry_value_copy(xhr.object));
        set_property(object.get(), "target", target.get());
        set_property(object.get(), "currentTarget", target.get());
    } else {
        set_property(object.get(), "target", jerry_null());
        set_property(object.get(), "currentTarget", jerry_null());
    }
    return object.release();
}

void dispatch_xhr_events(ScriptXmlHttpRequest& xhr) {
    AppXhrEventKind events[AppXmlHttpRequest::kMaxQueuedEvents];
    while (true) {
        const std::size_t count = xhr.request.take_events(events, AppXmlHttpRequest::kMaxQueuedEvents);
        if (count == 0) {
            return;
        }
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t callback_index = xhr_event_index(events[index]);
            if (callback_index >= xhr.callbacks.size() || xhr.callbacks[callback_index] == 0 ||
                !jerry_value_is_function(xhr.callbacks[callback_index])) {
                continue;
            }
            JerryValue callback(jerry_value_copy(xhr.callbacks[callback_index]));
            JerryValue event_object(make_xhr_event_object(xhr, events[index]));
            const jerry_value_t event_arg = event_object.get();
            JerryValue this_value(xhr.object != 0 ? jerry_value_copy(xhr.object) : jerry_undefined());
            JerryValue result(run_with_execution_budget(*xhr.runtime, [&]() {
                return jerry_call(callback.get(), this_value.get(), &event_arg, 1);
            }));
            if (jerry_value_is_exception(result.get())) {
                JerryValue exception_value(jerry_exception_value(result.release(), true));
                (void) exception_value;
            }
        }
    }
}

void set_xhr_callback(ScriptXmlHttpRequest& xhr, AppXhrEventKind kind, jerry_value_t value) {
    const std::size_t index = xhr_event_index(kind);
    if (index >= xhr.callbacks.size()) {
        return;
    }
    if (xhr.callbacks[index] != 0) {
        jerry_value_free(xhr.callbacks[index]);
        xhr.callbacks[index] = 0;
    }
    if (jerry_value_is_function(value)) {
        xhr.callbacks[index] = jerry_value_copy(value);
    }
}

jerry_value_t get_xhr_callback(const ScriptXmlHttpRequest& xhr, AppXhrEventKind kind) {
    const std::size_t index = xhr_event_index(kind);
    if (index >= xhr.callbacks.size() || xhr.callbacks[index] == 0) {
        return jerry_null();
    }
    return jerry_value_copy(xhr.callbacks[index]);
}

jerry_value_t make_audio_event_object(ScriptAudioElement& audio, ScriptAudioEventKind kind) {
    JerryValue object(jerry_object());
    set_property(object.get(), "type", string_to_value(audio_event_type(kind)).get());
    if (audio.object != 0) {
        JerryValue target(jerry_value_copy(audio.object));
        set_property(object.get(), "target", target.get());
        set_property(object.get(), "currentTarget", target.get());
    } else {
        set_property(object.get(), "target", jerry_null());
        set_property(object.get(), "currentTarget", jerry_null());
    }
    return object.release();
}

bool call_audio_callback(ScriptAudioElement& audio, jerry_value_t callback, ScriptAudioEventKind kind) {
    if (callback == 0 || !jerry_value_is_function(callback)) {
        return false;
    }
    JerryValue callback_copy(jerry_value_copy(callback));
    JerryValue event_object(make_audio_event_object(audio, kind));
    const jerry_value_t event_arg = event_object.get();
    JerryValue this_value(audio.object != 0 ? jerry_value_copy(audio.object) : jerry_undefined());
    JerryValue result(run_with_execution_budget(*audio.runtime, [&]() {
        return jerry_call(callback_copy.get(), this_value.get(), &event_arg, 1);
    }));
    if (jerry_value_is_exception(result.get())) {
        JerryValue exception_value(jerry_exception_value(result.release(), true));
        (void) exception_value;
    }
    return true;
}

bool dispatch_audio_event_to_element(ScriptAudioElement& audio, ScriptAudioEventKind kind) {
    const std::size_t index = audio_event_index(kind);
    if (index >= audio.property_callbacks.size()) {
        return false;
    }
    bool handled = false;
    handled = call_audio_callback(audio, audio.property_callbacks[index], kind) || handled;
    handled = call_audio_callback(audio, audio.event_listeners[index], kind) || handled;
    return handled;
}

void set_audio_callback(std::array<jerry_value_t, 2>& callbacks,
                        ScriptAudioEventKind kind,
                        jerry_value_t value) {
    const std::size_t index = audio_event_index(kind);
    if (index >= callbacks.size()) {
        return;
    }
    if (callbacks[index] != 0) {
        jerry_value_free(callbacks[index]);
        callbacks[index] = 0;
    }
    if (jerry_value_is_function(value)) {
        callbacks[index] = jerry_value_copy(value);
    }
}

jerry_value_t get_audio_callback(const std::array<jerry_value_t, 2>& callbacks,
                                 ScriptAudioEventKind kind) {
    const std::size_t index = audio_event_index(kind);
    if (index >= callbacks.size() || callbacks[index] == 0) {
        return jerry_null();
    }
    return jerry_value_copy(callbacks[index]);
}

jerry_value_t xhr_construct(const jerry_call_info_t* call_info_p,
                            const jerry_value_t[],
                            const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || jerry_value_is_undefined(call_info_p->new_target) ||
        !jerry_value_is_object(call_info_p->this_value)) {
        return throw_type_error("XMLHttpRequest must be constructed with new");
    }
    if (ScriptRuntimeAccess::app_host(*runtime) == nullptr ||
        ScriptRuntimeAccess::network_fetch(*runtime) == nullptr) {
        return throw_type_error("XMLHttpRequest network service is not bound");
    }
    ScriptXmlHttpRequest* xhr = ScriptRuntimeAccess::create_xml_http_request(*runtime);
    if (xhr == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "XMLHttpRequest budget exceeded");
    }

    xhr->object = jerry_value_copy(call_info_p->this_value);
    xhr->js_object_alive = true;
    jerry_object_set_native_ptr(call_info_p->this_value, &kXhrNativeInfo, xhr);
    jerry_object_set_native_ptr(call_info_p->this_value, &kRuntimeNativeInfo, runtime);
    return jerry_undefined();
}

jerry_value_t xhr_open(const jerry_call_info_t* call_info_p,
                       const jerry_value_t args_p[],
                       const jerry_length_t args_count) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    if (xhr == nullptr || args_count < 2) {
        return throw_type_error("XMLHttpRequest.open requires method and url");
    }
    const bool async = args_count < 3 || jerry_value_to_boolean(args_p[2]);
    const AppXhrStatus status = xhr->request.open(value_to_string(args_p[0]), value_to_string(args_p[1]), async);
    dispatch_xhr_events(*xhr);
    if (status == AppXhrStatus::Ok) {
        return jerry_undefined();
    }
    if (status == AppXhrStatus::SyncNotSupported) {
        return throw_type_error("synchronous XMLHttpRequest is not supported");
    }
    if (status == AppXhrStatus::UnsupportedMethod) {
        return throw_type_error("XMLHttpRequest only supports GET in this build");
    }
    return throw_type_error("invalid XMLHttpRequest.open arguments");
}

jerry_value_t xhr_send(const jerry_call_info_t* call_info_p,
                       const jerry_value_t[],
                       const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    JerryScriptRuntime* runtime = xhr != nullptr ? xhr->runtime : nullptr;
    if (xhr == nullptr || runtime == nullptr) {
        return throw_type_error("XMLHttpRequest.send called on invalid object");
    }
    AppRuntimeHost* host = ScriptRuntimeAccess::app_host(*runtime);
    NetworkFetchMock* network = ScriptRuntimeAccess::network_fetch(*runtime);
    if (host == nullptr || network == nullptr) {
        return throw_type_error("XMLHttpRequest network service is not bound");
    }
    const AppXhrStatus status = xhr->request.send(*host, *network);
    dispatch_xhr_events(*xhr);
    if (status == AppXhrStatus::Ok || status == AppXhrStatus::SubmitFailed) {
        return jerry_undefined();
    }
    return throw_type_error("XMLHttpRequest.send called before open or after send");
}

jerry_value_t xhr_abort(const jerry_call_info_t* call_info_p,
                        const jerry_value_t[],
                        const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    JerryScriptRuntime* runtime = xhr != nullptr ? xhr->runtime : nullptr;
    AppRuntimeHost* host = runtime != nullptr ? ScriptRuntimeAccess::app_host(*runtime) : nullptr;
    if (xhr == nullptr || runtime == nullptr || host == nullptr) {
        return jerry_undefined();
    }
    xhr->request.abort(*host);
    dispatch_xhr_events(*xhr);
    return jerry_undefined();
}

jerry_value_t xhr_get_ready_state(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    return jerry_number(xhr != nullptr ? static_cast<int>(xhr->request.ready_state()) : 0);
}

jerry_value_t xhr_get_status(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    return jerry_number(xhr != nullptr ? xhr->request.status() : 0);
}

jerry_value_t xhr_get_response_text(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    return jerry_string_sz(xhr != nullptr ? xhr->request.response_text().c_str() : "");
}

jerry_value_t xhr_get_response_url(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value);
    return jerry_string_sz(xhr != nullptr ? xhr->request.response_url().c_str() : "");
}

#define JELLYFRAME_XHR_CALLBACK_ACCESSOR(js_name, event_kind) \
    jerry_value_t xhr_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value); \
        return xhr != nullptr ? get_xhr_callback(*xhr, event_kind) : jerry_null(); \
    } \
    jerry_value_t xhr_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        ScriptXmlHttpRequest* xhr = native_xhr(call_info_p->this_value); \
        if (xhr != nullptr) { \
            set_xhr_callback(*xhr, event_kind, args_count > 0 ? args_p[0] : jerry_null()); \
        } \
        return jerry_undefined(); \
    }

JELLYFRAME_XHR_CALLBACK_ACCESSOR(onreadystatechange, AppXhrEventKind::ReadyStateChange)
JELLYFRAME_XHR_CALLBACK_ACCESSOR(onload, AppXhrEventKind::Load)
JELLYFRAME_XHR_CALLBACK_ACCESSOR(onerror, AppXhrEventKind::Error)
JELLYFRAME_XHR_CALLBACK_ACCESSOR(ontimeout, AppXhrEventKind::Timeout)
JELLYFRAME_XHR_CALLBACK_ACCESSOR(onabort, AppXhrEventKind::Abort)
JELLYFRAME_XHR_CALLBACK_ACCESSOR(onloadend, AppXhrEventKind::LoadEnd)

#undef JELLYFRAME_XHR_CALLBACK_ACCESSOR

void install_xhr_members(jerry_value_t object) {
    define_accessor(object, "readyState", xhr_get_ready_state, node_ignore_setter);
    define_accessor(object, "status", xhr_get_status, node_ignore_setter);
    define_accessor(object, "responseText", xhr_get_response_text, node_ignore_setter);
    define_accessor(object, "responseURL", xhr_get_response_url, node_ignore_setter);
    define_accessor(object, "onreadystatechange", xhr_get_onreadystatechange, xhr_set_onreadystatechange);
    define_accessor(object, "onload", xhr_get_onload, xhr_set_onload);
    define_accessor(object, "onerror", xhr_get_onerror, xhr_set_onerror);
    define_accessor(object, "ontimeout", xhr_get_ontimeout, xhr_set_ontimeout);
    define_accessor(object, "onabort", xhr_get_onabort, xhr_set_onabort);
    define_accessor(object, "onloadend", xhr_get_onloadend, xhr_set_onloadend);
    set_method(object, "open", xhr_open);
    set_method(object, "send", xhr_send);
    set_method(object, "abort", xhr_abort);
}

jerry_value_t make_xml_http_request_constructor(JerryScriptRuntime& runtime) {
    JerryValue constructor(jerry_function_external(xhr_construct));
    jerry_object_set_native_ptr(constructor.get(), &kRuntimeNativeInfo, &runtime);
    JerryValue prototype(jerry_object());
    install_xhr_members(prototype.get());
    set_property(constructor.get(), "prototype", prototype.get());
    return constructor.release();
}

jerry_value_t local_storage_error(AppLocalStorageStatus status) {
    switch (status) {
    case AppLocalStorageStatus::Ok:
    case AppLocalStorageStatus::NotFound:
        return jerry_undefined();
    case AppLocalStorageStatus::Disabled:
        return jerry_throw_sz(JERRY_ERROR_TYPE, "localStorage is disabled");
    case AppLocalStorageStatus::InvalidKey:
        return jerry_throw_sz(JERRY_ERROR_TYPE, "localStorage key is invalid");
    case AppLocalStorageStatus::BudgetExceeded:
        return jerry_throw_sz(JERRY_ERROR_RANGE, "localStorage quota exceeded");
    }
    return jerry_throw_sz(JERRY_ERROR_TYPE, "localStorage operation failed");
}

jerry_value_t local_storage_get_item(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    if (storage == nullptr || args_count < 1) {
        return throw_type_error("localStorage.getItem requires a key");
    }
    std::string value;
    const AppLocalStorageStatus status = storage->get_item(value_to_string(args_p[0]), &value);
    if (status == AppLocalStorageStatus::NotFound) {
        return jerry_null();
    }
    return status == AppLocalStorageStatus::Ok ? jerry_string_sz(value.c_str()) : local_storage_error(status);
}

jerry_value_t local_storage_set_item(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    if (storage == nullptr || args_count < 1) {
        return throw_type_error("localStorage.setItem requires a key");
    }
    const AppLocalStorageStatus status = storage->set_item(
        value_to_string(args_p[0]),
        args_count > 1 ? value_to_string(args_p[1]) : std::string());
    return status == AppLocalStorageStatus::Ok ? jerry_undefined() : local_storage_error(status);
}

jerry_value_t local_storage_remove_item(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    if (storage == nullptr || args_count < 1) {
        return throw_type_error("localStorage.removeItem requires a key");
    }
    const AppLocalStorageStatus status = storage->remove_item(value_to_string(args_p[0]));
    if (status == AppLocalStorageStatus::Ok || status == AppLocalStorageStatus::NotFound) {
        return jerry_undefined();
    }
    return local_storage_error(status);
}

jerry_value_t local_storage_clear(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    if (storage != nullptr) {
        storage->clear();
    }
    return jerry_undefined();
}

jerry_value_t local_storage_key(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    if (storage == nullptr || args_count < 1) {
        return jerry_null();
    }
    JerryValue number_value(jerry_value_to_number(args_p[0]));
    if (jerry_value_is_exception(number_value.get())) {
        return jerry_null();
    }
    const double number = jerry_value_as_number(number_value.get());
    if (!std::isfinite(number) || number < 0.0) {
        return jerry_null();
    }
    const double index = std::floor(number);
    if (index > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return jerry_null();
    }
    std::string key;
    const AppLocalStorageStatus status = storage->key(static_cast<std::size_t>(index), &key);
    return status == AppLocalStorageStatus::Ok ? jerry_string_sz(key.c_str()) : jerry_null();
}

jerry_value_t local_storage_get_length(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t[],
                                       const jerry_length_t) {
    AppLocalStorageShadow* storage = native_local_storage(call_info_p->this_value);
    return jerry_number(static_cast<double>(storage != nullptr ? storage->length() : 0));
}

jerry_value_t make_local_storage_object(JerryScriptRuntime& runtime, AppLocalStorageShadow& storage) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(),
                                &kLocalStorageNativeInfo,
                                ScriptRuntimeAccess::bind_script_local_storage(runtime, storage));
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    define_accessor(object.get(), "length", local_storage_get_length, node_ignore_setter);
    set_method(object.get(), "getItem", local_storage_get_item);
    set_method(object.get(), "setItem", local_storage_set_item);
    set_method(object.get(), "removeItem", local_storage_remove_item);
    set_method(object.get(), "clear", local_storage_clear);
    set_method(object.get(), "key", local_storage_key);
    return object.release();
}

jerry_value_t form_data_construct(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    if (jerry_value_is_undefined(call_info_p->new_target) || !jerry_value_is_object(call_info_p->this_value)) {
        return throw_type_error("FormData must be constructed with new");
    }
    std::vector<FormDataEntry> entries;
    if (args_count > 0) {
        Node* form = native_node(args_p[0]);
        if (form == nullptr || form->type != NodeType::Element || form->tag_name != "form") {
            return throw_type_error("FormData constructor requires a form element");
        }
        entries = collect_form_data(*form);
    }
    auto* data = new ScriptFormData;
    data->runtime = native_runtime(call_info_p->function);
    data->entries = std::move(entries);
    jerry_object_set_native_ptr(call_info_p->this_value, &kFormDataNativeInfo, data);
    return jerry_undefined();
}

jerry_value_t form_data_append(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 2) {
        return throw_type_error("FormData.append requires name and value");
    }
    data->entries.push_back(FormDataEntry{value_to_string(args_p[0]), value_to_string(args_p[1])});
    return jerry_undefined();
}

jerry_value_t form_data_delete(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 1) {
        return throw_type_error("FormData.delete requires a name");
    }
    const std::string name = value_to_string(args_p[0]);
    data->entries.erase(std::remove_if(data->entries.begin(), data->entries.end(),
                                       [&](const FormDataEntry& entry) { return entry.name == name; }),
                        data->entries.end());
    return jerry_undefined();
}

jerry_value_t form_data_get(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 1) {
        return throw_type_error("FormData.get requires a name");
    }
    const std::string name = value_to_string(args_p[0]);
    for (const FormDataEntry& entry : data->entries) {
        if (entry.name == name) {
            return jerry_string_sz(entry.value.c_str());
        }
    }
    return jerry_null();
}

jerry_value_t form_data_get_all(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 1) {
        return throw_type_error("FormData.getAll requires a name");
    }
    const std::string name = value_to_string(args_p[0]);
    std::size_t count = 0;
    for (const FormDataEntry& entry : data->entries) {
        count += entry.name == name ? 1U : 0U;
    }
    JerryValue result(jerry_array(static_cast<jerry_length_t>(count)));
    std::size_t index = 0;
    for (const FormDataEntry& entry : data->entries) {
        if (entry.name == name) {
            JerryValue value(jerry_string_sz(entry.value.c_str()));
            JerryValue set(jerry_object_set_index(result.get(), static_cast<jerry_length_t>(index++), value.get()));
            (void) set;
        }
    }
    return result.release();
}

jerry_value_t form_data_has(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 1) {
        return throw_type_error("FormData.has requires a name");
    }
    const std::string name = value_to_string(args_p[0]);
    return jerry_boolean(std::any_of(data->entries.begin(), data->entries.end(),
                                     [&](const FormDataEntry& entry) { return entry.name == name; }));
}

jerry_value_t form_data_set(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || args_count < 2) {
        return throw_type_error("FormData.set requires name and value");
    }
    const std::string name = value_to_string(args_p[0]);
    const std::string value = value_to_string(args_p[1]);
    auto first = std::find_if(data->entries.begin(), data->entries.end(),
                              [&](const FormDataEntry& entry) { return entry.name == name; });
    if (first == data->entries.end()) {
        data->entries.push_back(FormDataEntry{name, value});
        return jerry_undefined();
    }
    first->value = value;
    data->entries.erase(std::remove_if(std::next(first), data->entries.end(),
                                       [&](const FormDataEntry& entry) { return entry.name == name; }),
                        data->entries.end());
    return jerry_undefined();
}

jerry_value_t form_data_for_each(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    ScriptFormData* data = native_form_data(call_info_p->this_value);
    if (data == nullptr || data->runtime == nullptr || args_count < 1 || !jerry_value_is_function(args_p[0])) {
        return throw_type_error("FormData.forEach requires a callback");
    }

    // Callbacks may mutate FormData. Iterating a bounded snapshot keeps one call finite.
    const std::vector<FormDataEntry> entries = data->entries;
    const jerry_value_t this_arg = args_count > 1 ? args_p[1] : jerry_undefined();
    JerryValue source(jerry_value_copy(call_info_p->this_value));
    for (const FormDataEntry& entry : entries) {
        JerryValue value(jerry_string_sz(entry.value.c_str()));
        JerryValue name(jerry_string_sz(entry.name.c_str()));
        const jerry_value_t callback_args[] = {value.get(), name.get(), source.get()};
        JerryValue result(run_with_execution_budget(*data->runtime, [&]() {
            return jerry_call(args_p[0], this_arg, callback_args, 3);
        }));
        if (jerry_value_is_exception(result.get())) {
            return result.release();
        }
    }
    return jerry_undefined();
}

void install_form_data_members(jerry_value_t object) {
    set_method(object, "append", form_data_append);
    set_method(object, "delete", form_data_delete);
    set_method(object, "get", form_data_get);
    set_method(object, "getAll", form_data_get_all);
    set_method(object, "has", form_data_has);
    set_method(object, "set", form_data_set);
    set_method(object, "forEach", form_data_for_each);
}

jerry_value_t make_form_data_constructor(JerryScriptRuntime& runtime) {
    JerryValue constructor(jerry_function_external(form_data_construct));
    jerry_object_set_native_ptr(constructor.get(), &kRuntimeNativeInfo, &runtime);
    JerryValue prototype(jerry_object());
    install_form_data_members(prototype.get());
    set_property(constructor.get(), "prototype", prototype.get());
    return constructor.release();
}

jerry_value_t form_check_validity(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    Node* form = native_node(call_info_p->this_value);
    if (form == nullptr || form->type != NodeType::Element || form->tag_name != "form") {
        return throw_type_error("checkValidity requires a form element");
    }
    return jerry_boolean(check_form_validity(*form));
}

jerry_value_t form_report_validity(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    return form_check_validity(call_info_p, nullptr, 0);
}

jerry_value_t form_request_submit(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* form = native_node(call_info_p->this_value);
    if (form == nullptr || form->type != NodeType::Element || form->tag_name != "form") {
        return throw_type_error("requestSubmit requires a form element");
    }
    Node* submitter = nullptr;
    if (args_count > 0 && !jerry_value_is_undefined(args_p[0]) && !jerry_value_is_null(args_p[0])) {
        submitter = native_node(args_p[0]);
        if (submitter == nullptr || form_owner(*submitter) != form || !is_form_submitter(*submitter)) {
            return throw_type_error("requestSubmit submitter must belong to this form");
        }
    }
    request_form_submit(*form, submitter);
    return jerry_undefined();
}

jerry_value_t form_reset(const jerry_call_info_t* call_info_p,
                         const jerry_value_t[],
                         const jerry_length_t) {
    Node* form = native_node(call_info_p->this_value);
    if (form == nullptr || form->type != NodeType::Element || form->tag_name != "form") {
        return throw_type_error("reset requires a form element");
    }
    reset_form(*form);
    return jerry_undefined();
}

jerry_value_t node_get_will_validate(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    return jerry_boolean(node != nullptr && form_control_will_validate(*node));
}

jerry_value_t node_get_validation_message(const jerry_call_info_t* call_info_p,
                                           const jerry_value_t[],
                                           const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || !is_form_control(*node)) {
        return jerry_string_sz("");
    }
    return jerry_string_sz(form_control_validation_message(*node).c_str());
}

jerry_value_t node_get_validity(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    const FormControlValidationResult validation =
        node != nullptr && is_form_control(*node) ? validate_form_control(*node) : FormControlValidationResult{};
    JerryValue object(jerry_object());
    set_property(object.get(), "valueMissing", JerryValue(jerry_boolean(validation.value_missing)).get());
    set_property(object.get(), "tooShort", JerryValue(jerry_boolean(validation.too_short)).get());
    set_property(object.get(), "tooLong", JerryValue(jerry_boolean(validation.too_long)).get());
    set_property(object.get(), "customError", JerryValue(jerry_boolean(validation.custom_error)).get());
    set_property(object.get(), "valid", JerryValue(jerry_boolean(validation.valid())).get());
    return object.release();
}

jerry_value_t node_check_validity(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || !is_form_control(*node)) {
        return throw_type_error("checkValidity requires a form control");
    }
    return jerry_boolean(check_form_control_validity(*node));
}

jerry_value_t node_report_validity(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    return node_check_validity(call_info_p, nullptr, 0);
}

jerry_value_t node_set_custom_validity(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || !is_form_control(*node)) {
        return throw_type_error("setCustomValidity requires a form control");
    }
    set_form_control_custom_validity(*node, args_count > 0 ? value_to_string(args_p[0]) : std::string());
    return jerry_undefined();
}

jerry_value_t audio_construct(const jerry_call_info_t* call_info_p,
                              const jerry_value_t args_p[],
                              const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->function);
    if (runtime == nullptr || jerry_value_is_undefined(call_info_p->new_target) ||
        !jerry_value_is_object(call_info_p->this_value)) {
        return throw_type_error("Audio must be constructed with new");
    }
    if (ScriptRuntimeAccess::audio_host(*runtime).play == nullptr) {
        return throw_type_error("Audio host is not bound");
    }
    ScriptAudioElement* audio =
        ScriptRuntimeAccess::create_audio_element(*runtime, args_count > 0 ? value_to_string(args_p[0]) : "");
    if (audio == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "Audio element budget exceeded");
    }
    audio->object = jerry_value_copy(call_info_p->this_value);
    audio->js_object_alive = true;
    jerry_object_set_native_ptr(call_info_p->this_value, &kAudioNativeInfo, audio);
    jerry_object_set_native_ptr(call_info_p->this_value, &kRuntimeNativeInfo, runtime);
    return jerry_undefined();
}

jerry_value_t audio_get_src(const jerry_call_info_t* call_info_p,
                            const jerry_value_t[],
                            const jerry_length_t) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    return jerry_string_sz(audio != nullptr ? audio->src.c_str() : "");
}

jerry_value_t audio_set_src(const jerry_call_info_t* call_info_p,
                            const jerry_value_t args_p[],
                            const jerry_length_t args_count) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    if (audio != nullptr) {
        audio->src = args_count > 0 ? value_to_string(args_p[0]) : std::string();
    }
    return jerry_undefined();
}

jerry_value_t audio_get_volume(const jerry_call_info_t* call_info_p,
                               const jerry_value_t[],
                               const jerry_length_t) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    return jerry_number(audio != nullptr ? audio->volume : 1.0);
}

jerry_value_t audio_set_volume(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    if (audio == nullptr || args_count == 0) {
        return jerry_undefined();
    }
    JerryValue number_value(jerry_value_to_number(args_p[0]));
    if (jerry_value_is_exception(number_value.get())) {
        return jerry_undefined();
    }
    const double volume = jerry_value_as_number(number_value.get());
    if (std::isfinite(volume)) {
        audio->volume = std::max(0.0, std::min(1.0, volume));
    }
    return jerry_undefined();
}

jerry_value_t audio_play(const jerry_call_info_t* call_info_p,
                         const jerry_value_t[],
                         const jerry_length_t) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (audio == nullptr || runtime == nullptr) {
        return throw_type_error("Audio.play called on invalid object");
    }
    if (audio->src.empty()) {
        return throw_type_error("Audio.play requires a non-empty src");
    }
    const ScriptAudioHost host = ScriptRuntimeAccess::audio_host(*runtime);
    if (host.play == nullptr) {
        return throw_type_error("Audio host is not bound");
    }
    std::string error;
    if (!host.play(host.user, audio->id, audio->src, audio->volume, &error)) {
        if (error.empty()) {
            error = "Audio playback failed";
        }
        dispatch_audio_event_to_element(*audio, ScriptAudioEventKind::Error);
        return jerry_throw_sz(JERRY_ERROR_TYPE, error.c_str());
    }
    return jerry_undefined();
}

jerry_value_t audio_pause(const jerry_call_info_t* call_info_p,
                          const jerry_value_t[],
                          const jerry_length_t) {
    if (native_audio(call_info_p->this_value) == nullptr) {
        return throw_type_error("Audio.pause called on invalid object");
    }
    return jerry_undefined();
}

jerry_value_t audio_add_event_listener(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    if (audio == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return throw_type_error("Audio.addEventListener requires an event type and function");
    }
    bool known = false;
    const ScriptAudioEventKind kind = audio_event_kind_from_type(value_to_string(args_p[0]), &known);
    if (known) {
        set_audio_callback(audio->event_listeners, kind, args_p[1]);
    }
    return jerry_undefined();
}

jerry_value_t audio_remove_event_listener(const jerry_call_info_t* call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count) {
    ScriptAudioElement* audio = native_audio(call_info_p->this_value);
    if (audio == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return jerry_undefined();
    }
    bool known = false;
    const ScriptAudioEventKind kind = audio_event_kind_from_type(value_to_string(args_p[0]), &known);
    const std::size_t index = audio_event_index(kind);
    if (!known || index >= audio->event_listeners.size() || audio->event_listeners[index] == 0 ||
        !same_js_value(audio->event_listeners[index], args_p[1])) {
        return jerry_undefined();
    }
    jerry_value_free(audio->event_listeners[index]);
    audio->event_listeners[index] = 0;
    return jerry_undefined();
}

#define JELLYFRAME_AUDIO_CALLBACK_ACCESSOR(js_name, event_kind) \
    jerry_value_t audio_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        ScriptAudioElement* audio = native_audio(call_info_p->this_value); \
        return audio != nullptr ? get_audio_callback(audio->property_callbacks, event_kind) : jerry_null(); \
    } \
    jerry_value_t audio_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        ScriptAudioElement* audio = native_audio(call_info_p->this_value); \
        if (audio != nullptr) { \
            set_audio_callback(audio->property_callbacks, event_kind, args_count > 0 ? args_p[0] : jerry_null()); \
        } \
        return jerry_undefined(); \
    }

JELLYFRAME_AUDIO_CALLBACK_ACCESSOR(onended, ScriptAudioEventKind::Ended)
JELLYFRAME_AUDIO_CALLBACK_ACCESSOR(onerror, ScriptAudioEventKind::Error)

#undef JELLYFRAME_AUDIO_CALLBACK_ACCESSOR

void install_audio_members(jerry_value_t object) {
    define_accessor(object, "src", audio_get_src, audio_set_src);
    define_accessor(object, "volume", audio_get_volume, audio_set_volume);
    define_accessor(object, "onended", audio_get_onended, audio_set_onended);
    define_accessor(object, "onerror", audio_get_onerror, audio_set_onerror);
    set_method(object, "play", audio_play);
    set_method(object, "pause", audio_pause);
    set_method(object, "addEventListener", audio_add_event_listener);
    set_method(object, "removeEventListener", audio_remove_event_listener);
}

jerry_value_t make_audio_constructor(JerryScriptRuntime& runtime) {
    JerryValue constructor(jerry_function_external(audio_construct));
    jerry_object_set_native_ptr(constructor.get(), &kRuntimeNativeInfo, &runtime);
    JerryValue prototype(jerry_object());
    install_audio_members(prototype.get());
    set_property(constructor.get(), "prototype", prototype.get());
    return constructor.release();
}

constexpr std::size_t kMaxDatasetAttributes = 64;
constexpr std::size_t kMaxDatasetValueBytes = 256;

jerry_value_t dataset_proxy_get(const jerry_call_info_t*,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    if (args_count < 2) {
        return jerry_undefined();
    }
    Node* node = native_node(args_p[0]);
    if (node != nullptr && node->type == NodeType::Element && jerry_value_is_string(args_p[1])) {
        const std::string key = value_to_string(args_p[1]);
        if (dataset_key_is_writable(key)) {
            const std::string value = node->attribute(dataset_key_to_data_attribute(key));
            if (!value.empty() || node->attributes.find(dataset_key_to_data_attribute(key)) != node->attributes.end()) {
                return jerry_string_sz(value.c_str());
            }
        }
    }
    return jerry_object_get(args_p[0], args_p[1]);
}

jerry_value_t dataset_proxy_set(const jerry_call_info_t*,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    if (args_count < 3 || !jerry_value_is_string(args_p[1])) {
        return jerry_boolean(false);
    }
    Node* node = native_node(args_p[0]);
    const std::string key = value_to_string(args_p[1]);
    const std::string value = value_to_string(args_p[2]);
    if (node == nullptr || node->type != NodeType::Element || !dataset_key_is_writable(key) ||
        value.size() > kMaxDatasetValueBytes) {
        return jerry_boolean(false);
    }

    const std::string attribute = dataset_key_to_data_attribute(key);
    if (node->attributes.find(attribute) == node->attributes.end() &&
        node->attributes.size() >= kMaxDatasetAttributes) {
        return jerry_boolean(false);
    }
    node->set_attribute(attribute, value);
    return jerry_boolean(true);
}

jerry_value_t dataset_proxy_delete_property(const jerry_call_info_t*,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count) {
    if (args_count < 2 || !jerry_value_is_string(args_p[1])) {
        return jerry_boolean(false);
    }
    Node* node = native_node(args_p[0]);
    const std::string key = value_to_string(args_p[1]);
    if (node == nullptr || node->type != NodeType::Element || !dataset_key_is_writable(key)) {
        return jerry_boolean(false);
    }
    node->remove_attribute(dataset_key_to_data_attribute(key));
    return jerry_boolean(true);
}

jerry_value_t make_dataset_object(JerryScriptRuntime& runtime, Node& node) {
    if (jerry_feature_enabled(JERRY_FEATURE_PROXY)) {
        JerryValue target(jerry_object());
        bind_native_node(target.get(), runtime, node);
        JerryValue handler(jerry_object());
        set_method(handler.get(), "get", dataset_proxy_get);
        set_method(handler.get(), "set", dataset_proxy_set);
        set_method(handler.get(), "deleteProperty", dataset_proxy_delete_property);
        JerryValue proxy(jerry_proxy(target.get(), handler.get()));
        if (!jerry_value_is_exception(proxy.get())) {
            return proxy.release();
        }
    }

    JerryValue object(jerry_object());
    for (const auto& attribute : node.attributes) {
        const std::string key = data_attribute_to_dataset_key(attribute.first);
        if (key.empty()) {
            continue;
        }
        set_property(object.get(), key.c_str(), string_to_value(attribute.second).get());
    }
    return object.release();
}

Node* style_node(const jerry_call_info_t* call_info_p) {
    return native_node(call_info_p->this_value);
}

jerry_value_t style_get_named(const jerry_call_info_t* call_info_p, const char* property) {
    Node* node = style_node(call_info_p);
    if (node == nullptr) {
        return jerry_undefined();
    }
    return jerry_string_sz(inline_style_property(*node, property).c_str());
}

jerry_value_t style_set_named(const jerry_call_info_t* call_info_p,
                              const jerry_value_t args_p[],
                              const jerry_length_t args_count,
                              const char* property) {
    Node* node = style_node(call_info_p);
    if (node != nullptr) {
        set_inline_style_property(*node, property, args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t style_set_property(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    Node* node = style_node(call_info_p);
    if (node == nullptr || args_count < 2) {
        return jerry_undefined();
    }
    const std::string property = ascii_lowercase(value_to_string(args_p[0]));
    if (!is_script_writable_style_property(property)) {
        return jerry_undefined();
    }
    set_inline_style_property(*node, property, value_to_string(args_p[1]));
    return jerry_undefined();
}

jerry_value_t style_get_property_value(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    Node* node = style_node(call_info_p);
    if (node == nullptr || args_count < 1) {
        return jerry_string_sz("");
    }
    const std::string property = ascii_lowercase(value_to_string(args_p[0]));
    if (!is_script_writable_style_property(property)) {
        return jerry_string_sz("");
    }
    return jerry_string_sz(inline_style_property(*node, property).c_str());
}

jerry_value_t style_remove_property(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = style_node(call_info_p);
    if (node == nullptr || args_count < 1) {
        return jerry_string_sz("");
    }
    const std::string property = ascii_lowercase(value_to_string(args_p[0]));
    if (!is_script_writable_style_property(property)) {
        return jerry_string_sz("");
    }
    const std::string previous = inline_style_property(*node, property);
    set_inline_style_property(*node, property, std::string());
    return jerry_string_sz(previous.c_str());
}

#define JELLYFRAME_STYLE_ACCESSOR(js_name, css_name) \
    jerry_value_t style_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return style_get_named(call_info_p, css_name); \
    } \
    jerry_value_t style_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return style_set_named(call_info_p, args_p, args_count, css_name); \
    }

JELLYFRAME_STYLE_ACCESSOR(display, "display")
JELLYFRAME_STYLE_ACCESSOR(color, "color")
JELLYFRAME_STYLE_ACCESSOR(background, "background")
JELLYFRAME_STYLE_ACCESSOR(backgroundColor, "background-color")
JELLYFRAME_STYLE_ACCESSOR(backgroundImage, "background-image")
JELLYFRAME_STYLE_ACCESSOR(textAlign, "text-align")
JELLYFRAME_STYLE_ACCESSOR(textTransform, "text-transform")
JELLYFRAME_STYLE_ACCESSOR(fontSize, "font-size")
JELLYFRAME_STYLE_ACCESSOR(fontWeight, "font-weight")
JELLYFRAME_STYLE_ACCESSOR(lineHeight, "line-height")
JELLYFRAME_STYLE_ACCESSOR(width, "width")
JELLYFRAME_STYLE_ACCESSOR(height, "height")
JELLYFRAME_STYLE_ACCESSOR(minWidth, "min-width")
JELLYFRAME_STYLE_ACCESSOR(minHeight, "min-height")
JELLYFRAME_STYLE_ACCESSOR(maxWidth, "max-width")
JELLYFRAME_STYLE_ACCESSOR(maxHeight, "max-height")
JELLYFRAME_STYLE_ACCESSOR(boxSizing, "box-sizing")
JELLYFRAME_STYLE_ACCESSOR(margin, "margin")
JELLYFRAME_STYLE_ACCESSOR(marginTop, "margin-top")
JELLYFRAME_STYLE_ACCESSOR(marginRight, "margin-right")
JELLYFRAME_STYLE_ACCESSOR(marginBottom, "margin-bottom")
JELLYFRAME_STYLE_ACCESSOR(marginLeft, "margin-left")
JELLYFRAME_STYLE_ACCESSOR(padding, "padding")
JELLYFRAME_STYLE_ACCESSOR(paddingTop, "padding-top")
JELLYFRAME_STYLE_ACCESSOR(paddingRight, "padding-right")
JELLYFRAME_STYLE_ACCESSOR(paddingBottom, "padding-bottom")
JELLYFRAME_STYLE_ACCESSOR(paddingLeft, "padding-left")
JELLYFRAME_STYLE_ACCESSOR(opacity, "opacity")
JELLYFRAME_STYLE_ACCESSOR(transform, "transform")
JELLYFRAME_STYLE_ACCESSOR(borderRadius, "border-radius")
JELLYFRAME_STYLE_ACCESSOR(left, "left")
JELLYFRAME_STYLE_ACCESSOR(top, "top")
JELLYFRAME_STYLE_ACCESSOR(right, "right")
JELLYFRAME_STYLE_ACCESSOR(bottom, "bottom")
JELLYFRAME_STYLE_ACCESSOR(position, "position")
JELLYFRAME_STYLE_ACCESSOR(visibility, "visibility")
JELLYFRAME_STYLE_ACCESSOR(whiteSpace, "white-space")
JELLYFRAME_STYLE_ACCESSOR(textOverflow, "text-overflow")
JELLYFRAME_STYLE_ACCESSOR(overflow, "overflow")
JELLYFRAME_STYLE_ACCESSOR(zIndex, "z-index")

#undef JELLYFRAME_STYLE_ACCESSOR

jerry_value_t make_style_object(JerryScriptRuntime& runtime, Node& node) {
    JerryValue object(jerry_object());
    bind_native_node(object.get(), runtime, node);
    define_accessor(object.get(), "display", style_get_display, style_set_display);
    define_accessor(object.get(), "color", style_get_color, style_set_color);
    define_accessor(object.get(), "background", style_get_background, style_set_background);
    define_accessor(object.get(), "backgroundColor", style_get_backgroundColor, style_set_backgroundColor);
    define_accessor(object.get(), "backgroundImage", style_get_backgroundImage, style_set_backgroundImage);
    define_accessor(object.get(), "textAlign", style_get_textAlign, style_set_textAlign);
    define_accessor(object.get(), "textTransform", style_get_textTransform, style_set_textTransform);
    define_accessor(object.get(), "fontSize", style_get_fontSize, style_set_fontSize);
    define_accessor(object.get(), "fontWeight", style_get_fontWeight, style_set_fontWeight);
    define_accessor(object.get(), "lineHeight", style_get_lineHeight, style_set_lineHeight);
    define_accessor(object.get(), "width", style_get_width, style_set_width);
    define_accessor(object.get(), "height", style_get_height, style_set_height);
    define_accessor(object.get(), "minWidth", style_get_minWidth, style_set_minWidth);
    define_accessor(object.get(), "minHeight", style_get_minHeight, style_set_minHeight);
    define_accessor(object.get(), "maxWidth", style_get_maxWidth, style_set_maxWidth);
    define_accessor(object.get(), "maxHeight", style_get_maxHeight, style_set_maxHeight);
    define_accessor(object.get(), "boxSizing", style_get_boxSizing, style_set_boxSizing);
    define_accessor(object.get(), "margin", style_get_margin, style_set_margin);
    define_accessor(object.get(), "marginTop", style_get_marginTop, style_set_marginTop);
    define_accessor(object.get(), "marginRight", style_get_marginRight, style_set_marginRight);
    define_accessor(object.get(), "marginBottom", style_get_marginBottom, style_set_marginBottom);
    define_accessor(object.get(), "marginLeft", style_get_marginLeft, style_set_marginLeft);
    define_accessor(object.get(), "padding", style_get_padding, style_set_padding);
    define_accessor(object.get(), "paddingTop", style_get_paddingTop, style_set_paddingTop);
    define_accessor(object.get(), "paddingRight", style_get_paddingRight, style_set_paddingRight);
    define_accessor(object.get(), "paddingBottom", style_get_paddingBottom, style_set_paddingBottom);
    define_accessor(object.get(), "paddingLeft", style_get_paddingLeft, style_set_paddingLeft);
    define_accessor(object.get(), "opacity", style_get_opacity, style_set_opacity);
    define_accessor(object.get(), "transform", style_get_transform, style_set_transform);
    define_accessor(object.get(), "borderRadius", style_get_borderRadius, style_set_borderRadius);
    define_accessor(object.get(), "left", style_get_left, style_set_left);
    define_accessor(object.get(), "top", style_get_top, style_set_top);
    define_accessor(object.get(), "right", style_get_right, style_set_right);
    define_accessor(object.get(), "bottom", style_get_bottom, style_set_bottom);
    define_accessor(object.get(), "position", style_get_position, style_set_position);
    define_accessor(object.get(), "visibility", style_get_visibility, style_set_visibility);
    define_accessor(object.get(), "whiteSpace", style_get_whiteSpace, style_set_whiteSpace);
    define_accessor(object.get(), "textOverflow", style_get_textOverflow, style_set_textOverflow);
    define_accessor(object.get(), "overflow", style_get_overflow, style_set_overflow);
    define_accessor(object.get(), "zIndex", style_get_zIndex, style_set_zIndex);
    set_method(object.get(), "getPropertyValue", style_get_property_value);
    set_method(object.get(), "setProperty", style_set_property);
    set_method(object.get(), "removeProperty", style_remove_property);
    return object.release();
}

std::string canvas_color_to_string(Color color) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string output = "#000000";
    output[1] = kHex[color.r >> 4U];
    output[2] = kHex[color.r & 0x0fU];
    output[3] = kHex[color.g >> 4U];
    output[4] = kHex[color.g & 0x0fU];
    output[5] = kHex[color.b >> 4U];
    output[6] = kHex[color.b & 0x0fU];
    return output;
}

Canvas2DRegistry* canvas_registry_for(jerry_value_t object, Node*& node) {
    node = native_node(object);
    JerryScriptRuntime* runtime = native_runtime(object);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "canvas") {
        return nullptr;
    }
    return ScriptRuntimeAccess::canvas_2d(*runtime);
}

jerry_value_t canvas_get_fill_style(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry == nullptr || node == nullptr) {
        return jerry_string_sz("#000000");
    }
    return string_to_value(canvas_color_to_string(registry->fill_style(*node))).release();
}

jerry_value_t canvas_set_fill_style(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count > 0) {
        if (ScriptCanvasGradient* gradient = native_canvas_gradient(args_p[0])) {
            registry->set_fill_gradient(*node, gradient->id);
        } else {
            registry->set_fill_style(*node, value_to_string(args_p[0]));
        }
    }
    return jerry_undefined();
}

jerry_value_t canvas_get_stroke_style(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry == nullptr || node == nullptr) {
        return jerry_string_sz("#000000");
    }
    return string_to_value(canvas_color_to_string(registry->stroke_style(*node))).release();
}

jerry_value_t canvas_set_stroke_style(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count > 0) {
        if (ScriptCanvasGradient* gradient = native_canvas_gradient(args_p[0])) {
            registry->set_stroke_gradient(*node, gradient->id);
        } else {
            registry->set_stroke_style(*node, value_to_string(args_p[0]));
        }
    }
    return jerry_undefined();
}

jerry_value_t canvas_get_line_width(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    return jerry_number(registry != nullptr && node != nullptr ? registry->line_width(*node) : 1);
}

jerry_value_t canvas_set_line_width(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count > 0) {
        registry->set_line_width(*node, double_from_value(args_p[0], 1.0));
    }
    return jerry_undefined();
}

jerry_value_t canvas_get_global_alpha(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    return jerry_number(registry != nullptr && node != nullptr ? registry->global_alpha(*node) : 1.0);
}

jerry_value_t canvas_set_global_alpha(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count > 0) {
        registry->set_global_alpha(*node, double_from_value(args_p[0], 1.0));
    }
    return jerry_undefined();
}

jerry_value_t canvas_get_font(const jerry_call_info_t* call_info_p,
                              const jerry_value_t[],
                              const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry == nullptr || node == nullptr) {
        return jerry_string_sz("10px sans-serif");
    }
    return string_to_value(registry->font(*node)).release();
}

jerry_value_t canvas_set_font(const jerry_call_info_t* call_info_p,
                              const jerry_value_t args_p[],
                              const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count > 0) {
        registry->set_font(*node, value_to_string(args_p[0]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_save(const jerry_call_info_t* call_info_p,
                          const jerry_value_t[],
                          const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->save(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_restore(const jerry_call_info_t* call_info_p,
                             const jerry_value_t[],
                             const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->restore(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_measure_text(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    JerryValue metrics(jerry_object());
    double width = 0.0;
    if (registry != nullptr && node != nullptr && args_count >= 1) {
        width = registry->measure_text(*node, value_to_string(args_p[0])).width;
    }
    set_number_property(metrics.get(), "width", width);
    return metrics.release();
}

jerry_value_t canvas_fill_text(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 3) {
        registry->fill_text(*node,
                            value_to_string(args_p[0]),
                            double_from_value(args_p[1]),
                            double_from_value(args_p[2]),
                            args_count >= 4 ? double_from_value(args_p[3]) : 0.0);
    }
    return jerry_undefined();
}

jerry_value_t canvas_gradient_add_color_stop(const jerry_call_info_t* call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count) {
    ScriptCanvasGradient* gradient = native_canvas_gradient(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    Canvas2DRegistry* registry = runtime != nullptr ? ScriptRuntimeAccess::canvas_2d(*runtime) : nullptr;
    if (gradient != nullptr && registry != nullptr && args_count >= 2) {
        registry->add_color_stop(gradient->id,
                                 double_from_value(args_p[0]),
                                 value_to_string(args_p[1]));
    }
    return jerry_undefined();
}

jerry_value_t make_canvas_gradient(JerryScriptRuntime& runtime, std::uint32_t gradient_id) {
    ScriptCanvasGradient* gradient = ScriptRuntimeAccess::create_canvas_gradient(runtime, gradient_id);
    if (gradient == nullptr) {
        return jerry_null();
    }
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kCanvasGradientNativeInfo, gradient);
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);
    set_method(object.get(), "addColorStop", canvas_gradient_add_color_stop);
    return object.release();
}

jerry_value_t canvas_clear_rect(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 4) {
        registry->clear_rect(*node,
                             int_from_value(args_p[0]),
                             int_from_value(args_p[1]),
                             int_from_value(args_p[2]),
                             int_from_value(args_p[3]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_fill_rect(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 4) {
        registry->fill_rect(*node,
                            int_from_value(args_p[0]),
                            int_from_value(args_p[1]),
                            int_from_value(args_p[2]),
                            int_from_value(args_p[3]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_stroke_rect(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 4) {
        registry->stroke_rect(*node,
                              int_from_value(args_p[0]),
                              int_from_value(args_p[1]),
                              int_from_value(args_p[2]),
                              int_from_value(args_p[3]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_begin_path(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->begin_path(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_move_to(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 2) {
        registry->move_to(*node, int_from_value(args_p[0]), int_from_value(args_p[1]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_line_to(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 2) {
        registry->line_to(*node, int_from_value(args_p[0]), int_from_value(args_p[1]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_bezier_curve_to(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 6) {
        registry->bezier_curve_to(*node,
                                  double_from_value(args_p[0]), double_from_value(args_p[1]),
                                  double_from_value(args_p[2]), double_from_value(args_p[3]),
                                  double_from_value(args_p[4]), double_from_value(args_p[5]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_quadratic_curve_to(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 4) {
        registry->quadratic_curve_to(*node,
                                     double_from_value(args_p[0]),
                                     double_from_value(args_p[1]),
                                     double_from_value(args_p[2]),
                                     double_from_value(args_p[3]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_translate(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 2) {
        registry->translate(*node, double_from_value(args_p[0]), double_from_value(args_p[1]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_reset_transform(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->reset_transform(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_arc(const jerry_call_info_t* call_info_p,
                         const jerry_value_t args_p[],
                         const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr && args_count >= 5) {
        registry->arc(*node,
                      double_from_value(args_p[0]),
                      double_from_value(args_p[1]),
                      double_from_value(args_p[2]),
                      double_from_value(args_p[3]),
                      double_from_value(args_p[4]),
                      args_count >= 6 && jerry_value_to_boolean(args_p[5]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_close_path(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->close_path(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_fill(const jerry_call_info_t* call_info_p,
                          const jerry_value_t[],
                          const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->fill(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_stroke(const jerry_call_info_t* call_info_p,
                            const jerry_value_t[],
                            const jerry_length_t) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    if (registry != nullptr && node != nullptr) {
        registry->stroke(*node);
    }
    return jerry_undefined();
}

jerry_value_t canvas_draw_image(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* destination = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, destination);
    Node* source = args_count > 0 ? native_node(args_p[0]) : nullptr;
    if (registry == nullptr || destination == nullptr || source == nullptr ||
        source->type != NodeType::Element || source->tag_name != "canvas") {
        return jerry_undefined();
    }
    const Canvas2DSurface* source_surface = registry->surface(registry->handle_for(*source));
    if (source_surface == nullptr) {
        return jerry_undefined();
    }
    if (args_count == 3) {
        registry->draw_image(*destination, *source, 0, 0, source_surface->width, source_surface->height,
                             int_from_value(args_p[1]), int_from_value(args_p[2]),
                             source_surface->width, source_surface->height);
    } else if (args_count == 5) {
        registry->draw_image(*destination, *source, 0, 0, source_surface->width, source_surface->height,
                             int_from_value(args_p[1]), int_from_value(args_p[2]),
                             int_from_value(args_p[3]), int_from_value(args_p[4]));
    } else if (args_count == 9) {
        registry->draw_image(*destination, *source,
                             int_from_value(args_p[1]), int_from_value(args_p[2]),
                             int_from_value(args_p[3]), int_from_value(args_p[4]),
                             int_from_value(args_p[5]), int_from_value(args_p[6]),
                             int_from_value(args_p[7]), int_from_value(args_p[8]));
    }
    return jerry_undefined();
}

jerry_value_t canvas_create_linear_gradient(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (registry == nullptr || runtime == nullptr || args_count < 4) {
        return jerry_null();
    }
    const std::uint32_t gradient_id =
        registry->create_linear_gradient(*node,
                                         double_from_value(args_p[0]),
                                         double_from_value(args_p[1]),
                                         double_from_value(args_p[2]),
                                         double_from_value(args_p[3]));
    return gradient_id != 0 ? make_canvas_gradient(*runtime, gradient_id) : jerry_null();
}

jerry_value_t canvas_create_radial_gradient(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count) {
    Node* node = nullptr;
    Canvas2DRegistry* registry = canvas_registry_for(call_info_p->this_value, node);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (registry == nullptr || runtime == nullptr || args_count < 6) {
        return jerry_null();
    }
    const std::uint32_t gradient_id =
        registry->create_radial_gradient(*node,
                                         double_from_value(args_p[0]),
                                         double_from_value(args_p[1]),
                                         double_from_value(args_p[2]),
                                         double_from_value(args_p[3]),
                                         double_from_value(args_p[4]),
                                         double_from_value(args_p[5]));
    return gradient_id != 0 ? make_canvas_gradient(*runtime, gradient_id) : jerry_null();
}

jerry_value_t make_canvas_2d_context(JerryScriptRuntime& runtime, Node& node) {
    JerryValue object(jerry_object());
    bind_native_node(object.get(), runtime, node);
    define_accessor(object.get(), "fillStyle", canvas_get_fill_style, canvas_set_fill_style);
    define_accessor(object.get(), "strokeStyle", canvas_get_stroke_style, canvas_set_stroke_style);
    define_accessor(object.get(), "lineWidth", canvas_get_line_width, canvas_set_line_width);
    define_accessor(object.get(), "globalAlpha", canvas_get_global_alpha, canvas_set_global_alpha);
    define_accessor(object.get(), "font", canvas_get_font, canvas_set_font);
    set_method(object.get(), "save", canvas_save);
    set_method(object.get(), "restore", canvas_restore);
    set_method(object.get(), "clearRect", canvas_clear_rect);
    set_method(object.get(), "fillRect", canvas_fill_rect);
    set_method(object.get(), "strokeRect", canvas_stroke_rect);
    set_method(object.get(), "beginPath", canvas_begin_path);
    set_method(object.get(), "moveTo", canvas_move_to);
    set_method(object.get(), "lineTo", canvas_line_to);
    set_method(object.get(), "quadraticCurveTo", canvas_quadratic_curve_to);
    set_method(object.get(), "bezierCurveTo", canvas_bezier_curve_to);
    set_method(object.get(), "translate", canvas_translate);
    set_method(object.get(), "resetTransform", canvas_reset_transform);
    set_method(object.get(), "arc", canvas_arc);
    set_method(object.get(), "closePath", canvas_close_path);
    set_method(object.get(), "fill", canvas_fill);
    set_method(object.get(), "stroke", canvas_stroke);
    set_method(object.get(), "measureText", canvas_measure_text);
    set_method(object.get(), "fillText", canvas_fill_text);
    set_method(object.get(), "drawImage", canvas_draw_image);
    set_method(object.get(), "createLinearGradient", canvas_create_linear_gradient);
    set_method(object.get(), "createRadialGradient", canvas_create_radial_gradient);
    return object.release();
}

jerry_value_t canvas_get_context(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t args_p[],
                                 const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element || node->tag_name != "canvas" ||
        args_count < 1 || value_to_string(args_p[0]) != "2d") {
        return jerry_null();
    }
    Canvas2DRegistry* registry = ScriptRuntimeAccess::canvas_2d(*runtime);
    if (registry == nullptr || registry->ensure_surface(*node) == 0) {
        return jerry_null();
    }
    return make_canvas_2d_context(*runtime, *node);
}

jerry_value_t node_get_dataset(const jerry_call_info_t* call_info_p,
                               const jerry_value_t[],
                               const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element) {
        return jerry_object();
    }
    return make_dataset_object(*runtime, *node);
}

jerry_value_t node_get_bounding_client_rect(const jerry_call_info_t* call_info_p,
                                             const jerry_value_t[],
                                             const jerry_length_t) {
    ScriptNodeBinding* binding = native_node_binding(call_info_p->this_value);
    if (binding == nullptr || binding->runtime == nullptr) {
        return throw_type_error("getBoundingClientRect called on non-node object");
    }

    const bool has_snapshot = ScriptRuntimeAccess::request_layout_snapshot(*binding->runtime, *binding);
    const Rect rect = has_snapshot ? binding->layout_rect : Rect{};
    JerryValue result(jerry_object());
    set_number_property(result.get(), "x", rect.x);
    set_number_property(result.get(), "y", rect.y);
    set_number_property(result.get(), "width", rect.width);
    set_number_property(result.get(), "height", rect.height);
    set_number_property(result.get(), "top", rect.y);
    set_number_property(result.get(), "right", rect.x + rect.width);
    set_number_property(result.get(), "bottom", rect.y + rect.height);
    set_number_property(result.get(), "left", rect.x);
    return result.release();
}

jerry_value_t node_get_style_object(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->type != NodeType::Element) {
        return jerry_object();
    }
    return make_style_object(*runtime, *node);
}

jerry_value_t node_append(const jerry_call_info_t* call_info_p,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count);
jerry_value_t node_prepend(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count);

jerry_value_t make_node_wrapper(JerryScriptRuntime& runtime, Node& node, bool document_methods) {
    JerryValue object(jerry_object());
    bind_native_node(object.get(), runtime, node);

    define_accessor(object.get(), "textContent", node_get_text_content, node_set_text_content);
    if (node.type == NodeType::Element) {
        define_accessor(object.get(), "innerText", node_get_text_content, node_set_text_content);
    }
    define_accessor(object.get(), "id", node_get_id, node_set_id);
    define_accessor(object.get(), "className", node_get_class_name, node_set_class_name);
    define_accessor(object.get(), "title", node_get_title, node_set_title);
    define_accessor(object.get(), "lang", node_get_lang, node_set_lang);
    define_accessor(object.get(), "dir", node_get_dir, node_set_dir);
    define_accessor(object.get(), "classList", node_get_class_list, node_ignore_setter);
    define_accessor(object.get(), "parentElement", node_get_parent_element, node_ignore_setter);
    define_accessor(object.get(), "children", node_get_children, node_ignore_setter);
    define_accessor(object.get(), "dataset", node_get_dataset, node_ignore_setter);
    define_accessor(object.get(), "style", node_get_style_object, node_ignore_setter);
    define_accessor(object.get(), "hidden", node_get_hidden, node_set_hidden);
    define_accessor(object.get(), "disabled", node_get_disabled, node_set_disabled);
    define_accessor(object.get(), "open", node_get_open, node_set_open);
    if (node.type == NodeType::Element) {
        define_accessor(object.get(), "tabIndex", node_get_tabIndex, node_set_tabIndex);
        define_accessor(object.get(), "autofocus", node_get_autofocus, node_set_autofocus);
    }
#define JELLYFRAME_DEFINE_NODE_EVENT_HANDLER(js_name, event_type) \
    define_accessor(object.get(), #js_name, node_get_##js_name, node_set_##js_name);
    JELLYFRAME_NODE_EVENT_HANDLER_LIST(JELLYFRAME_DEFINE_NODE_EVENT_HANDLER)
#undef JELLYFRAME_DEFINE_NODE_EVENT_HANDLER
    const bool form_control = is_form_control(node);
    if (form_control || is_progress_or_meter(node)) {
        define_accessor(object.get(), "value", node_get_value, node_set_value);
    }
    if (form_control) {
        define_accessor(object.get(), "type", node_get_type, node_set_type);
        define_accessor(object.get(), "name", node_get_name, node_set_name);
        define_accessor(object.get(), "checked", node_get_checked, node_set_checked);
        define_accessor(object.get(), "selectedIndex", node_get_selected_index, node_set_selected_index);
        define_accessor(object.get(), "readOnly", node_get_read_only, node_set_read_only);
        define_accessor(object.get(), "maxLength", node_get_maxLength, node_set_maxLength);
        define_accessor(object.get(), "minLength", node_get_minLength, node_set_minLength);
        define_accessor(object.get(), "min", node_get_min, node_set_min);
        define_accessor(object.get(), "max", node_get_max, node_set_max);
        define_accessor(object.get(), "step", node_get_step, node_set_step);
        define_accessor(object.get(), "willValidate", node_get_will_validate, node_ignore_setter);
        define_accessor(object.get(), "validationMessage", node_get_validation_message, node_ignore_setter);
        define_accessor(object.get(), "validity", node_get_validity, node_ignore_setter);
    }
    if (node.type == NodeType::Element && (node.tag_name == "input" || node.tag_name == "textarea")) {
        define_accessor(object.get(), "placeholder", node_get_placeholder, node_set_placeholder);
        define_accessor(object.get(), "defaultValue", node_get_default_value, node_set_default_value);
    }
    if (node.type == NodeType::Element && node.tag_name == "input") {
        define_accessor(object.get(), "defaultChecked", node_get_default_checked, node_set_default_checked);
    }
    if (node.type == NodeType::Element &&
        (node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea")) {
        define_accessor(object.get(), "required", node_get_required, node_set_required);
    }
    if (node.type == NodeType::Element && node.tag_name == "textarea") {
        define_accessor(object.get(), "rows", node_get_rows, node_set_rows);
        define_accessor(object.get(), "cols", node_get_cols, node_set_cols);
        define_accessor(object.get(), "wrap", node_get_wrap, node_set_wrap);
        define_accessor(object.get(), "textLength", node_get_text_length, node_ignore_setter);
    }
    if (node.type == NodeType::Element && node.tag_name == "select") {
        define_accessor(object.get(), "size", node_get_size, node_set_size);
    }
    if (is_progress_or_meter(node)) {
        define_accessor(object.get(), "max", node_get_max, node_set_max);
    }
    if (node.type == NodeType::Element && node.tag_name == "meter") {
        define_accessor(object.get(), "min", node_get_min, node_set_min);
        define_accessor(object.get(), "low", node_get_low, node_set_low);
        define_accessor(object.get(), "high", node_get_high, node_set_high);
        define_accessor(object.get(), "optimum", node_get_optimum, node_set_optimum);
    }
    if (node.type == NodeType::Element && node.tag_name == "progress") {
        define_accessor(object.get(), "position", node_get_position, node_ignore_setter);
    }
    if (node.type == NodeType::Element && node.tag_name == "option") {
        define_accessor(object.get(), "label", node_get_label, node_set_label);
        define_accessor(object.get(), "defaultSelected", node_get_default_selected, node_set_default_selected);
        define_accessor(object.get(), "value", node_get_option_value, node_set_option_value);
        define_accessor(object.get(), "text", node_get_option_text, node_set_option_text);
        define_accessor(object.get(), "index", node_get_option_index, node_ignore_setter);
    }
    if (node.type == NodeType::Element && node.tag_name == "optgroup") {
        define_accessor(object.get(), "label", node_get_label, node_set_label);
    }
    if (node.type == NodeType::Element && node.tag_name == "meta") {
        define_accessor(object.get(), "name", node_get_name, node_set_name);
        define_accessor(object.get(), "content", node_get_content, node_set_content);
        define_accessor(object.get(), "httpEquiv", node_get_httpEquiv, node_set_httpEquiv);
        define_accessor(object.get(), "media", node_get_media, node_set_media);
    }
    if (node.type == NodeType::Element && node.tag_name == "data") {
        define_accessor(object.get(), "value", node_get_value_attribute, node_set_value_attribute);
    }
    if (node.type == NodeType::Element && node.tag_name == "time") {
        define_accessor(object.get(), "dateTime", node_get_dateTime, node_set_dateTime);
    }
    if (node.type == NodeType::Element && node.tag_name == "a") {
        define_accessor(object.get(), "download", node_get_download, node_set_download);
        define_accessor(object.get(), "ping", node_get_ping, node_set_ping);
        define_accessor(object.get(), "rel", node_get_rel, node_set_rel);
        define_accessor(object.get(), "referrerPolicy", node_get_referrerPolicy, node_set_referrerPolicy);
        define_accessor(object.get(), "text", node_get_text_content, node_set_text_content);
    }
    if (node.type == NodeType::Element && node.tag_name == "img") {
        define_accessor(object.get(), "alt", node_get_alt, node_set_alt);
    }
    if (node.type == NodeType::Element && node.tag_name == "label") {
        define_accessor(object.get(), "htmlFor", node_get_htmlFor, node_set_htmlFor);
        define_accessor(object.get(), "control", node_get_label_control, node_ignore_setter);
    }
    set_method(object.get(), "appendChild", node_append_child);
    set_method(object.get(), "append", node_append);
    set_method(object.get(), "prepend", node_prepend);
    set_method(object.get(), "removeChild", node_remove_child);
    set_method(object.get(), "setAttribute", element_set_attribute);
    set_method(object.get(), "getAttribute", element_get_attribute);
    set_method(object.get(), "removeAttribute", element_remove_attribute);
    set_method(object.get(), "hasAttribute", element_has_attribute);
    set_method(object.get(), "toggleAttribute", element_toggle_attribute);
    set_method(object.get(), "remove", node_remove);
    set_method(object.get(), "addEventListener", node_add_event_listener);
    set_method(object.get(), "removeEventListener", node_remove_event_listener);
    set_method(object.get(), "click", node_click);
    set_method(object.get(), "matches", node_matches);
    set_method(object.get(), "closest", node_closest);
    set_method(object.get(), "querySelector", node_query_selector);
    set_method(object.get(), "querySelectorAll", node_query_selector_all);
    set_method(object.get(), "getBoundingClientRect", node_get_bounding_client_rect);
    if (form_control) {
        set_method(object.get(), "checkValidity", node_check_validity);
        set_method(object.get(), "reportValidity", node_report_validity);
        set_method(object.get(), "setCustomValidity", node_set_custom_validity);
    }
    if (node.type == NodeType::Element && node.tag_name == "form") {
        set_method(object.get(), "checkValidity", form_check_validity);
        set_method(object.get(), "reportValidity", form_report_validity);
        set_method(object.get(), "requestSubmit", form_request_submit);
        set_method(object.get(), "reset", form_reset);
    }
    if (node.type == NodeType::Element && node.tag_name == "dialog") {
        define_accessor(object.get(), "returnValue", dialog_get_return_value, dialog_set_return_value);
        set_method(object.get(), "showModal", dialog_show_modal);
        set_method(object.get(), "close", dialog_close);
    }
    if (node.type == NodeType::Element && node.tag_name == "canvas") {
        set_method(object.get(), "getContext", canvas_get_context);
    }

    if (node.type == NodeType::Element) {
        set_property(object.get(), "tagName", string_to_value(node.tag_name).get());
        set_property(object.get(), "nodeType", JerryValue(jerry_number(1)).get());
    } else {
        set_property(object.get(), "nodeType", JerryValue(jerry_number(3)).get());
    }

    if (document_methods) {
        define_accessor(object.get(), "hidden", document_get_hidden, node_ignore_setter);
        define_accessor(object.get(), "visibilityState", document_get_visibility_state, node_ignore_setter);
        define_accessor(object.get(), "readyState", document_get_ready_state, node_ignore_setter);
        define_accessor(object.get(), "defaultView", document_get_default_view, node_ignore_setter);
        define_accessor(object.get(), "head", document_get_head, node_ignore_setter);
        define_accessor(object.get(), "body", document_get_body, node_ignore_setter);
        define_accessor(object.get(), "title", document_get_title_attr, document_set_title_attr);
        define_accessor(object.get(), "dir", document_get_dir, document_set_dir);
        define_accessor(object.get(), "images", document_get_images, node_ignore_setter);
        define_accessor(object.get(), "embeds", document_get_embeds, node_ignore_setter);
        define_accessor(object.get(), "plugins", document_get_embeds, node_ignore_setter);
        define_accessor(object.get(), "links", document_get_links, node_ignore_setter);
        define_accessor(object.get(), "forms", document_get_forms, node_ignore_setter);
        define_accessor(object.get(), "scripts", document_get_scripts, node_ignore_setter);
        set_method(object.get(), "getElementById", document_get_element_by_id);
        set_method(object.get(), "getElementsByName", document_get_elements_by_name);
        set_method(object.get(), "querySelector", document_query_selector);
        set_method(object.get(), "querySelectorAll", document_query_selector_all);
        set_method(object.get(), "createElement", document_create_element);
        set_method(object.get(), "createTextNode", document_create_text_node);
        set_method(object.get(), "hasFocus", document_has_focus);
    }

    return object.release();
}

Node& insert_or_move_child(JerryScriptRuntime& runtime, Node& parent, Node& child, std::size_t index);

Node& append_or_move_child(JerryScriptRuntime& runtime, Node& parent, Node& child) {
    return insert_or_move_child(runtime, parent, child, parent.children.size());
}

Node& insert_or_move_child(JerryScriptRuntime& runtime, Node& parent, Node& child, std::size_t index) {
    if (&parent == &child || is_ancestor_of(child, parent)) {
        throw std::runtime_error("node insertion would create a cycle");
    }

    if (child.parent != nullptr) {
        Node* old_parent = child.parent;
        auto detached = old_parent->detach_child(child);
        if (!detached) {
            throw std::runtime_error("node insertion could not detach existing child");
        }
        return parent.insert_child(std::move(detached), index);
    }

    if (auto detached = ScriptRuntimeAccess::release_detached_node(runtime, child)) {
        return parent.insert_child(std::move(detached), index);
    }

    throw std::runtime_error("node insertion received a node outside this runtime");
}

jerry_value_t node_append_child(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* parent = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    Node* child = args_count > 0 ? native_node(args_p[0]) : nullptr;
    if (parent == nullptr || runtime == nullptr || child == nullptr) {
        return throw_type_error("appendChild requires a node child");
    }

    try {
        Node& appended = append_or_move_child(*runtime, *parent, *child);
        return make_node_wrapper(*runtime, appended, false);
    } catch (const std::exception& error) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, error.what());
    }
}

jerry_value_t node_append(const jerry_call_info_t* call_info_p,
                          const jerry_value_t args_p[],
                          const jerry_length_t args_count) {
    Node* parent = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (parent == nullptr || runtime == nullptr) {
        return throw_type_error("append called on an invalid node");
    }
    try {
        for (jerry_length_t index = 0; index < args_count; ++index) {
            if (Node* child = native_node(args_p[index])) {
                append_or_move_child(*runtime, *parent, *child);
            } else {
                parent->append_child(make_text(value_to_string(args_p[index])));
            }
        }
    } catch (const std::exception& error) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, error.what());
    }
    return jerry_undefined();
}

jerry_value_t node_prepend(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* parent = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (parent == nullptr || runtime == nullptr) {
        return throw_type_error("prepend called on an invalid node");
    }
    try {
        std::size_t insertion_index = 0;
        for (jerry_length_t index = 0; index < args_count; ++index, ++insertion_index) {
            if (Node* child = native_node(args_p[index])) {
                insert_or_move_child(*runtime, *parent, *child, insertion_index);
            } else {
                parent->insert_child(make_text(value_to_string(args_p[index])), insertion_index);
            }
        }
    } catch (const std::exception& error) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, error.what());
    }
    return jerry_undefined();
}

jerry_value_t node_remove_child(const jerry_call_info_t* call_info_p,
                                const jerry_value_t args_p[],
                                const jerry_length_t args_count) {
    Node* parent = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    Node* child = args_count > 0 ? native_node(args_p[0]) : nullptr;
    if (parent == nullptr || runtime == nullptr || child == nullptr) {
        return throw_type_error("removeChild requires a node child");
    }
    if (child->parent != parent) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, "removeChild child is not attached to this parent");
    }
    if (!ScriptRuntimeAccess::can_adopt_detached_node(*runtime)) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }

    auto detached = parent->detach_child(*child);
    if (!detached) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, "removeChild failed");
    }
    Node* adopted = ScriptRuntimeAccess::adopt_detached_node(*runtime, std::move(detached));
    if (adopted == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }
    return make_node_wrapper(*runtime, *adopted, false);
}

jerry_value_t element_set_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return throw_type_error("setAttribute requires an element and attribute name");
    }

    node->set_attribute(ascii_lowercase(value_to_string(args_p[0])),
                        args_count > 1 ? value_to_string(args_p[1]) : std::string());
    return jerry_undefined();
}

jerry_value_t element_get_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return throw_type_error("getAttribute requires an element and attribute name");
    }

    const auto it = node->attributes.find(ascii_lowercase(value_to_string(args_p[0])));
    if (it == node->attributes.end()) {
        return jerry_null();
    }
    return jerry_string_sz(it->second.c_str());
}

jerry_value_t element_remove_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return throw_type_error("removeAttribute requires an element and attribute name");
    }
    node->remove_attribute(ascii_lowercase(value_to_string(args_p[0])));
    return jerry_undefined();
}

jerry_value_t element_has_attribute(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return throw_type_error("hasAttribute requires an element and attribute name");
    }
    const std::string name = ascii_lowercase(value_to_string(args_p[0]));
    return jerry_boolean(!name.empty() && node->attributes.find(name) != node->attributes.end());
}

jerry_value_t element_toggle_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || args_count < 1) {
        return throw_type_error("toggleAttribute requires an element and attribute name");
    }
    const std::string name = ascii_lowercase(value_to_string(args_p[0]));
    if (name.empty()) {
        return throw_type_error("toggleAttribute requires a non-empty attribute name");
    }
    const bool present = node->attributes.find(name) != node->attributes.end();
    const bool should_be_present = args_count >= 2 ? jerry_value_to_boolean(args_p[1]) : !present;
    if (should_be_present && !present) {
        node->set_attribute(name, "");
    } else if (!should_be_present && present) {
        node->remove_attribute(name);
    }
    return jerry_boolean(should_be_present);
}

jerry_value_t node_remove(const jerry_call_info_t* call_info_p,
                          const jerry_value_t[],
                          const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || node->parent == nullptr) {
        return jerry_undefined();
    }
    if (!ScriptRuntimeAccess::can_adopt_detached_node(*runtime)) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }
    Node* parent = node->parent;
    auto detached = parent->detach_child(*node);
    if (!detached || ScriptRuntimeAccess::adopt_detached_node(*runtime, std::move(detached)) == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }
    return jerry_undefined();
}

jerry_value_t node_matches(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || args_count < 1) {
        return jerry_boolean(false);
    }
    return jerry_boolean(simple_selector_matches(*node, value_to_string(args_p[0])));
}

jerry_value_t node_closest(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_null();
    }
    const std::string selector = value_to_string(args_p[0]);
    for (Node* current = node; current != nullptr; current = current->parent) {
        if (simple_selector_matches(*current, selector)) {
            return make_node_wrapper(*runtime, *current, false);
        }
    }
    return jerry_null();
}

Node* query_selector_first(Node& root, const std::string& selector, bool include_root) {
    if (include_root && simple_selector_matches(root, selector)) {
        return &root;
    }
    for (const auto& child : root.children) {
        if (Node* found = query_selector_first(*child, selector, true)) {
            return found;
        }
    }
    return nullptr;
}

void query_selector_all(Node& root,
                        const std::string& selector,
                        bool include_root,
                        std::vector<Node*>& output) {
    if (include_root && simple_selector_matches(root, selector)) {
        output.push_back(&root);
    }
    for (const auto& child : root.children) {
        query_selector_all(*child, selector, true, output);
    }
}

jerry_value_t node_query_selector(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_null();
    }
    Node* found = query_selector_first(*node, value_to_string(args_p[0]), false);
    if (found == nullptr) {
        return jerry_null();
    }
    return make_node_wrapper(*runtime, *found, false);
}

jerry_value_t node_query_selector_all(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_array(0);
    }
    std::vector<Node*> matches;
    query_selector_all(*node, value_to_string(args_p[0]), false, matches);
    JerryValue array(jerry_array(static_cast<jerry_length_t>(matches.size())));
    for (std::size_t index = 0; index < matches.size(); ++index) {
        JerryValue wrapped(make_node_wrapper(*runtime, *matches[index], false));
        JerryValue result(jerry_object_set_index(array.get(), static_cast<jerry_length_t>(index), wrapped.get()));
        (void) result;
    }
    return array.release();
}

jerry_value_t document_get_element_by_id(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr || args_count < 1) {
        return throw_type_error("getElementById requires an id");
    }

    Node* found = find_by_id(*document, value_to_string(args_p[0]));
    if (found == nullptr) {
        return jerry_null();
    }
    return make_node_wrapper(*runtime, *found, false);
}

jerry_value_t document_get_body(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr) {
        return jerry_null();
    }
    Node* body = find_first_element_by_tag(*document, "body");
    return body != nullptr ? make_node_wrapper(*runtime, *body, false) : jerry_null();
}

jerry_value_t document_get_head(const jerry_call_info_t* call_info_p,
                                const jerry_value_t[],
                                const jerry_length_t) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr) {
        return jerry_null();
    }
    Node* head = find_first_element_by_tag(*document, "head");
    return head != nullptr ? make_node_wrapper(*runtime, *head, false) : jerry_null();
}

Node* document_direction_element(Node& document) {
    if (Node* html = find_first_element_by_tag(document, "html")) {
        return html;
    }
    if (Node* body = find_first_element_by_tag(document, "body")) {
        return body;
    }
    return &document;
}

jerry_value_t document_get_title_attr(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t[],
                                      const jerry_length_t) {
    Node* document = native_node(call_info_p->this_value);
    if (document == nullptr) {
        return jerry_string_sz("");
    }
    Node* title = find_first_element_by_tag(*document, "title");
    return jerry_string_sz(title != nullptr ? title->text_content().c_str() : "");
}

jerry_value_t document_set_title_attr(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    if (document == nullptr) {
        return jerry_undefined();
    }
    Node* title = find_first_element_by_tag(*document, "title");
    if (title == nullptr) {
        Node& created = document->append_child(make_element("title"));
        title = &created;
    }
    title->set_text_content(args_count > 0 ? value_to_string(args_p[0]) : std::string());
    return jerry_undefined();
}

jerry_value_t document_get_dir(const jerry_call_info_t* call_info_p,
                               const jerry_value_t[],
                               const jerry_length_t) {
    Node* document = native_node(call_info_p->this_value);
    return jerry_string_sz(document != nullptr ? document_direction_element(*document)->attribute("dir").c_str() : "");
}

jerry_value_t document_set_dir(const jerry_call_info_t* call_info_p,
                               const jerry_value_t args_p[],
                               const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    if (document != nullptr) {
        document_direction_element(*document)->set_attribute(
            "dir", args_count > 0 ? value_to_string(args_p[0]) : std::string());
    }
    return jerry_undefined();
}

jerry_value_t document_query_selector(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_null();
    }
    Node* found = query_selector_first(*document, value_to_string(args_p[0]), true);
    if (found == nullptr) {
        return jerry_null();
    }
    return make_node_wrapper(*runtime, *found, false);
}

jerry_value_t document_query_selector_all(const jerry_call_info_t* call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_array(0);
    }
    std::vector<Node*> matches;
    query_selector_all(*document, value_to_string(args_p[0]), true, matches);
    JerryValue array(jerry_array(static_cast<jerry_length_t>(matches.size())));
    for (std::size_t index = 0; index < matches.size(); ++index) {
        JerryValue wrapped(make_node_wrapper(*runtime, *matches[index], false));
        JerryValue result(jerry_object_set_index(array.get(), static_cast<jerry_length_t>(index), wrapped.get()));
        (void) result;
    }
    return array.release();
}

jerry_value_t make_node_snapshot_array(JerryScriptRuntime& runtime, const std::vector<Node*>& matches) {
    JerryValue array(jerry_array(static_cast<jerry_length_t>(matches.size())));
    for (std::size_t index = 0; index < matches.size(); ++index) {
        JerryValue wrapped(make_node_wrapper(runtime, *matches[index], false));
        JerryValue result(jerry_object_set_index(array.get(), static_cast<jerry_length_t>(index), wrapped.get()));
        (void) result;
    }
    return array.release();
}

void collect_document_elements(Node& root, bool (*predicate)(const Node&), std::vector<Node*>& output) {
    std::vector<Node*> pending;
    for (auto it = root.children.rbegin(); it != root.children.rend(); ++it) {
        pending.push_back(it->get());
    }
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        if (predicate(*node)) {
            output.push_back(node);
        }
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

jerry_value_t document_collection_from_predicate(const jerry_call_info_t* call_info_p,
                                                 bool (*predicate)(const Node&)) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr) {
        return jerry_array(0);
    }
    std::vector<Node*> matches;
    collect_document_elements(*document, predicate, matches);
    return make_node_snapshot_array(*runtime, matches);
}

jerry_value_t document_get_images(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    return document_collection_from_predicate(call_info_p, document_collection_image);
}

jerry_value_t document_get_embeds(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    return document_collection_from_predicate(call_info_p, document_collection_embed);
}

jerry_value_t document_get_links(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    return document_collection_from_predicate(call_info_p, document_collection_link);
}

jerry_value_t document_get_forms(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    return document_collection_from_predicate(call_info_p, document_collection_form);
}

jerry_value_t document_get_scripts(const jerry_call_info_t* call_info_p,
                                   const jerry_value_t[],
                                   const jerry_length_t) {
    return document_collection_from_predicate(call_info_p, document_collection_script);
}

jerry_value_t document_get_elements_by_name(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count) {
    Node* document = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (document == nullptr || runtime == nullptr || args_count < 1) {
        return jerry_array(0);
    }
    const std::string name = value_to_string(args_p[0]);
    std::vector<Node*> matches;
    std::vector<Node*> pending;
    for (auto it = document->children.rbegin(); it != document->children.rend(); ++it) {
        pending.push_back(it->get());
    }
    while (!pending.empty()) {
        Node* node = pending.back();
        pending.pop_back();
        if (node->type == NodeType::Element && node->attribute("name") == name) {
            matches.push_back(node);
        }
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return make_node_snapshot_array(*runtime, matches);
}

jerry_value_t document_create_element(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr || args_count < 1) {
        return throw_type_error("createElement requires a tag name");
    }

    Node* node = ScriptRuntimeAccess::adopt_detached_node(
        *runtime, make_element(ascii_lowercase(value_to_string(args_p[0]))));
    if (node == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }
    return make_node_wrapper(*runtime, *node, false);
}

jerry_value_t document_create_text_node(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        return throw_type_error("createTextNode requires a document");
    }

    Node* node = ScriptRuntimeAccess::adopt_detached_node(
        *runtime, make_text(args_count > 0 ? value_to_string(args_p[0]) : std::string()));
    if (node == nullptr) {
        return jerry_throw_sz(JERRY_ERROR_RANGE, "detached node budget exceeded");
    }
    return make_node_wrapper(*runtime, *node, false);
}

jerry_value_t document_get_hidden(const jerry_call_info_t* call_info_p,
                                  const jerry_value_t[],
                                  const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    return jerry_boolean(runtime != nullptr && ScriptRuntimeAccess::system_state(*runtime).document_hidden);
}

jerry_value_t document_get_visibility_state(const jerry_call_info_t* call_info_p,
                                            const jerry_value_t[],
                                            const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    const bool hidden = runtime != nullptr && ScriptRuntimeAccess::system_state(*runtime).document_hidden;
    return jerry_string_sz(hidden ? "hidden" : "visible");
}

jerry_value_t document_get_ready_state(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t) {
    return jerry_string_sz("complete");
}

jerry_value_t document_get_default_view(const jerry_call_info_t*, const jerry_value_t[], const jerry_length_t) {
    JerryValue global(jerry_current_realm());
    return jerry_object_get_sz(global.get(), "window");
}

jerry_value_t document_has_focus(const jerry_call_info_t* call_info_p,
                                 const jerry_value_t[],
                                 const jerry_length_t) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    return jerry_boolean(runtime == nullptr || !ScriptRuntimeAccess::system_state(*runtime).document_hidden);
}

jerry_value_t node_click(const jerry_call_info_t* call_info_p,
                         const jerry_value_t[],
                         const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || node->type != NodeType::Element || is_disabled_form_control(*node)) {
        return jerry_undefined();
    }
    if (is_form_control(*node) && form_control_kind(*node) != FormControlKind::Range) {
        if (activate_form_control(*node)) {
            Event input("input", true, false);
            dispatch_event(*node, input);
            Event change("change", true, false);
            dispatch_event(*node, change);
        }
    }
    MouseEvent click("click", 0, 0);
    dispatch_event(*node, click);
    if (!click.default_prevented() && node->tag_name == "summary" &&
        node->parent != nullptr && node->parent->tag_name == "details") {
        Node* details = node->parent;
        if (has_attribute(*details, "open")) {
            details->remove_attribute("open");
        } else {
            details->set_attribute("open", "");
        }
        Event toggle("toggle", false, false);
        dispatch_event(*details, toggle);
    }
    if (!click.default_prevented()) {
        request_form_submit_from_control(*node);
        reset_form_from_control(*node);
    }
    return jerry_undefined();
}

jerry_value_t node_get_event_handler(const jerry_call_info_t* call_info_p, const char* type) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr) {
        return jerry_null();
    }
    return ScriptRuntimeAccess::get_script_event_handler(*runtime, *node, type);
}

jerry_value_t node_set_event_handler(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count,
                                     const char* type) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node != nullptr && runtime != nullptr) {
        ScriptRuntimeAccess::set_script_event_handler(
            *runtime,
            *node,
            type,
            args_count > 0 && jerry_value_is_function(args_p[0]) ? args_p[0] : jerry_undefined());
    }
    return jerry_undefined();
}

#define JELLYFRAME_DEFINE_NODE_EVENT_HANDLER_ACCESSOR(js_name, event_type) \
    jerry_value_t node_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return node_get_event_handler(call_info_p, event_type); \
    } \
    jerry_value_t node_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return node_set_event_handler(call_info_p, args_p, args_count, event_type); \
    }

JELLYFRAME_NODE_EVENT_HANDLER_LIST(JELLYFRAME_DEFINE_NODE_EVENT_HANDLER_ACCESSOR)

#undef JELLYFRAME_DEFINE_NODE_EVENT_HANDLER_ACCESSOR

JerryScriptRuntime* runtime_from_window_call(const jerry_call_info_t* call_info_p) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        runtime = native_runtime(call_info_p->function);
    }
    return runtime;
}

JerryValue window_event_target_from_call(const jerry_call_info_t* call_info_p) {
    JerryValue global(jerry_current_realm());
    JerryValue window(jerry_object_get_sz(global.get(), "window"));
    if (jerry_value_is_object(window.get())) {
        return window;
    }
    return JerryValue(jerry_value_copy(call_info_p->this_value));
}

jerry_value_t window_get_event_handler(const jerry_call_info_t* call_info_p, const char* type) {
    JerryScriptRuntime* runtime = runtime_from_window_call(call_info_p);
    if (runtime == nullptr) {
        return jerry_null();
    }
    return ScriptRuntimeAccess::get_window_event_handler(*runtime, type);
}

jerry_value_t window_set_event_handler(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count,
                                       const char* type) {
    JerryScriptRuntime* runtime = runtime_from_window_call(call_info_p);
    if (runtime != nullptr) {
        JerryValue target(window_event_target_from_call(call_info_p));
        ScriptRuntimeAccess::set_window_event_handler(
            *runtime,
            type,
            args_count > 0 && jerry_value_is_function(args_p[0]) ? args_p[0] : jerry_undefined(),
            target.get());
    }
    return jerry_undefined();
}

#define JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER_ACCESSOR(js_name, event_type) \
    jerry_value_t window_get_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t[], const jerry_length_t) { \
        return window_get_event_handler(call_info_p, event_type); \
    } \
    jerry_value_t window_set_##js_name(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count) { \
        return window_set_event_handler(call_info_p, args_p, args_count, event_type); \
    }

JELLYFRAME_WINDOW_EVENT_HANDLER_LIST(JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER_ACCESSOR)

#undef JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER_ACCESSOR

jerry_value_t node_add_event_listener(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return throw_type_error("addEventListener requires an event type and function");
    }

    ScriptRuntimeAccess::add_script_event_listener(
        *runtime,
        *node,
        value_to_string(args_p[0]),
        args_p[1],
        args_count > 2 ? listener_options_from_value(args_p[2]) : EventListenerOptions{});
    return jerry_undefined();
}

jerry_value_t node_remove_event_listener(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (node == nullptr || runtime == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return jerry_undefined();
    }

    ScriptRuntimeAccess::remove_script_event_listener(*runtime, *node, value_to_string(args_p[0]), args_p[1]);
    return jerry_undefined();
}

jerry_value_t window_add_event_listener(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        runtime = native_runtime(call_info_p->function);
    }
    if (runtime == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return throw_type_error("window.addEventListener requires an event type and function");
    }
    JerryValue target(jerry_value_copy(call_info_p->this_value));
    if (!jerry_value_is_object(target.get()) || native_runtime(target.get()) == nullptr) {
        JerryValue global(jerry_current_realm());
        target = JerryValue(jerry_object_get_sz(global.get(), "window"));
    }
    ScriptRuntimeAccess::add_window_event_listener(
        *runtime,
        value_to_string(args_p[0]),
        args_p[1],
        target.get(),
        args_count > 2 ? listener_options_from_value(args_p[2]) : EventListenerOptions{});
    return jerry_undefined();
}

jerry_value_t window_remove_event_listener(const jerry_call_info_t* call_info_p,
                                           const jerry_value_t args_p[],
                                           const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        runtime = native_runtime(call_info_p->function);
    }
    if (runtime == nullptr || args_count < 2 || !jerry_value_is_function(args_p[1])) {
        return jerry_undefined();
    }
    ScriptRuntimeAccess::remove_window_event_listener(*runtime, value_to_string(args_p[0]), args_p[1]);
    return jerry_undefined();
}

} // namespace

ScriptNodeBinding* JerryScriptRuntime::bind_script_node(Node& node) {
    auto* binding = new ScriptNodeBinding{this, &node, {}, true, false, false};
    node_bindings_.push_back(binding);
    if (std::find(observed_nodes_.begin(), observed_nodes_.end(), &node) == observed_nodes_.end()) {
        node.set_destroy_observer(script_node_destroyed, this);
        observed_nodes_.push_back(&node);
    }
    return binding;
}

Node* JerryScriptRuntime::resolve_script_node(const ScriptNodeBinding& binding) const {
    if (binding.runtime != this || !binding.active) {
        return nullptr;
    }
    return binding.node;
}

void JerryScriptRuntime::forget_script_node_binding(ScriptNodeBinding& binding) {
    layout_snapshot_bindings_.erase(
        std::remove(layout_snapshot_bindings_.begin(), layout_snapshot_bindings_.end(), &binding),
        layout_snapshot_bindings_.end());
    auto it = std::find(node_bindings_.begin(), node_bindings_.end(), &binding);
    if (it != node_bindings_.end()) {
        node_bindings_.erase(it);
    }
    binding.runtime = nullptr;
    binding.node = nullptr;
    binding.active = false;
}

void JerryScriptRuntime::invalidate_script_node(Node& node) {
    if (active_modal_dialog_ == &node) {
        active_modal_dialog_ = nullptr;
    }
    dialog_states_.erase(
        std::remove_if(dialog_states_.begin(), dialog_states_.end(),
                       [&node](const std::unique_ptr<ScriptDialogState>& state) {
                           return state == nullptr || state->node == &node;
                       }),
        dialog_states_.end());
    for (ScriptNodeBinding* binding : node_bindings_) {
        if (binding != nullptr && binding->node == &node) {
            binding->node = nullptr;
            binding->active = false;
            binding->layout_snapshot_requested = false;
            binding->has_layout_snapshot = false;
        }
    }
    layout_snapshot_bindings_.erase(
        std::remove_if(layout_snapshot_bindings_.begin(),
                       layout_snapshot_bindings_.end(),
                       [&node](const ScriptNodeBinding* binding) {
                           return binding == nullptr || binding->node == &node;
                       }),
        layout_snapshot_bindings_.end());
    for (Node*& observed : observed_nodes_) {
        if (observed == &node) {
            observed = nullptr;
        }
    }
}

void JerryScriptRuntime::clear_script_node_bindings() {
    for (Node* node : observed_nodes_) {
        if (node != nullptr) {
            node->clear_destroy_observer(script_node_destroyed, this);
        }
    }
    observed_nodes_.clear();
    layout_snapshot_bindings_.clear();
    for (ScriptNodeBinding* binding : node_bindings_) {
        if (binding != nullptr) {
            binding->runtime = nullptr;
            binding->node = nullptr;
            binding->active = false;
            binding->layout_snapshot_requested = false;
            binding->has_layout_snapshot = false;
        }
    }
    node_bindings_.clear();
}

void JerryScriptRuntime::capture_layout_snapshot(const LayoutBox& root,
                                                 int client_offset_x,
                                                 int client_offset_y) {
    if (layout_snapshot_bindings_.empty()) {
        return;
    }

    for (ScriptNodeBinding* binding : layout_snapshot_bindings_) {
        if (binding != nullptr) {
            binding->has_layout_snapshot = false;
        }
    }

    std::vector<const LayoutBox*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const LayoutBox* box = pending.back();
        pending.pop_back();
        if (box == nullptr) {
            continue;
        }
        for (ScriptNodeBinding* binding : layout_snapshot_bindings_) {
            if (binding != nullptr && binding->active && binding->node == box->node) {
                binding->layout_rect = box->rect;
                binding->layout_rect.x += client_offset_x;
                binding->layout_rect.y += client_offset_y;
                binding->has_layout_snapshot = true;
            }
        }
        for (const LayoutBoxPtr& child : box->children) {
            if (child != nullptr) {
                pending.push_back(child.get());
            }
        }
    }
}

ScriptLocalStorageBinding* JerryScriptRuntime::bind_script_local_storage(AppLocalStorageShadow& storage) {
    auto* binding = new ScriptLocalStorageBinding{this, &storage, true};
    local_storage_bindings_.push_back(binding);
    return binding;
}

AppLocalStorageShadow* JerryScriptRuntime::resolve_script_local_storage(
    const ScriptLocalStorageBinding& binding) const {
    if (binding.runtime != this || !binding.active) {
        return nullptr;
    }
    return binding.storage;
}

void JerryScriptRuntime::forget_script_local_storage_binding(ScriptLocalStorageBinding& binding) {
    auto it = std::find(local_storage_bindings_.begin(), local_storage_bindings_.end(), &binding);
    if (it != local_storage_bindings_.end()) {
        local_storage_bindings_.erase(it);
    }
    binding.runtime = nullptr;
    binding.storage = nullptr;
    binding.active = false;
}

void JerryScriptRuntime::clear_script_local_storage_bindings() {
    for (ScriptLocalStorageBinding* binding : local_storage_bindings_) {
        if (binding != nullptr) {
            binding->runtime = nullptr;
            binding->storage = nullptr;
            binding->active = false;
        }
    }
    local_storage_bindings_.clear();
}

JerryScriptRuntime::JerryScriptRuntime(JerryScriptRuntimeOptions options)
    : options_(options) {
    if (g_runtime_active) {
        throw std::runtime_error("only one JerryScriptRuntime can be active in this build");
    }

    jerry_init(JERRY_INIT_EMPTY);
    initialized_ = true;
    g_runtime_active = true;
}

JerryScriptRuntime::JerryScriptRuntime(const HostBudgets& budgets)
    : JerryScriptRuntime(JerryScriptRuntimeOptions{
          std::max<std::size_t>(1, budgets.max_timers),
          std::max<std::size_t>(1, budgets.max_event_listeners),
          std::max<std::size_t>(1, budgets.max_detached_dom_nodes),
          16,
          std::max<std::size_t>(1, budgets.max_active_animations),
          8,
          4,
          static_cast<std::uint32_t>(std::min<std::size_t>(
              budgets.max_script_execution_checks,
              std::numeric_limits<std::uint32_t>::max())),
          static_cast<std::uint32_t>(std::min<std::size_t>(
              budgets.script_execution_check_interval,
              std::numeric_limits<std::uint32_t>::max())),
      }) {}

JerryScriptRuntime::~JerryScriptRuntime() {
    if (initialized_) {
        clear_xml_http_requests();
        clear_audio_elements();
        clear_geolocation_requests();
        clear_canvas_gradients();
        clear_dialog_states();
        clear_script_event_listeners();
        clear_animation_frame_callbacks();
        clear_timers();
        clear_script_node_bindings();
        clear_script_local_storage_bindings();
        if (canvas_2d_ != nullptr) {
            canvas_2d_->clear();
        }
        jerry_cleanup();
        initialized_ = false;
        g_runtime_active = false;
    }
}

void JerryScriptRuntime::bind_document(Node& document) {
    clear_xml_http_requests();
    clear_audio_elements();
    clear_geolocation_requests();
    clear_canvas_gradients();
    clear_dialog_states();
    clear_script_event_listeners();
    clear_animation_frame_callbacks();
    clear_timers();
    clear_script_node_bindings();
    clear_script_local_storage_bindings();
    detached_nodes_.clear_detached_nodes();
    if (canvas_2d_ != nullptr) {
        canvas_2d_->clear();
    }
    bound_document_ = &document;
    route_fragment_.clear();
    route_history_.clear();
    route_history_index_ = 0;

    JerryValue global(jerry_current_realm());
    jerry_object_set_native_ptr(global.get(), &kRuntimeNativeInfo, this);
    JerryValue document_object(make_node_wrapper(*this, document, true));
    JerryValue window_object(jerry_object());
    JerryValue navigator_object(make_navigator_object(*this));
    JerryValue location_object(make_location_object(*this));
    JerryValue history_object(make_history_object(*this));
    jerry_object_set_native_ptr(window_object.get(), &kRuntimeNativeInfo, this);

    set_property(window_object.get(), "document", document_object.get());
    set_property(window_object.get(), "window", window_object.get());
    set_property(window_object.get(), "self", window_object.get());
    set_property(window_object.get(), "origin", string_to_value("null").get());
    set_bool_property(window_object.get(), "isSecureContext", false);
    set_bool_property(window_object.get(), "crossOriginIsolated", false);
    set_property(window_object.get(), "navigator", navigator_object.get());
    set_property(window_object.get(), "location", location_object.get());
    set_property(window_object.get(), "history", history_object.get());
    set_property(global.get(), "document", document_object.get());
    set_property(global.get(), "window", window_object.get());
    set_property(global.get(), "self", window_object.get());
    set_property(global.get(), "origin", string_to_value("null").get());
    set_bool_property(global.get(), "isSecureContext", false);
    set_bool_property(global.get(), "crossOriginIsolated", false);
    set_property(global.get(), "navigator", navigator_object.get());
    set_property(global.get(), "location", location_object.get());
    set_property(global.get(), "history", history_object.get());
    set_runtime_method(window_object.get(), "setTimeout", script_set_timeout, *this);
    set_runtime_method(window_object.get(), "clearTimeout", script_clear_timer, *this);
    set_runtime_method(window_object.get(), "setInterval", script_set_interval, *this);
    set_runtime_method(window_object.get(), "clearInterval", script_clear_timer, *this);
    set_runtime_method(window_object.get(), "requestAnimationFrame", script_request_animation_frame, *this);
    set_runtime_method(window_object.get(), "cancelAnimationFrame", script_cancel_animation_frame, *this);
    set_method(window_object.get(), "btoa", script_btoa);
    set_method(window_object.get(), "atob", script_atob);
    set_runtime_method(window_object.get(), "addEventListener", window_add_event_listener, *this);
    set_runtime_method(window_object.get(), "removeEventListener", window_remove_event_listener, *this);
#define JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER(js_name, event_type) \
    define_accessor(window_object.get(), #js_name, window_get_##js_name, window_set_##js_name);
    JELLYFRAME_WINDOW_EVENT_HANDLER_LIST(JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER)
#undef JELLYFRAME_DEFINE_WINDOW_EVENT_HANDLER
    set_runtime_method(global.get(), "setTimeout", script_set_timeout, *this);
    set_runtime_method(global.get(), "clearTimeout", script_clear_timer, *this);
    set_runtime_method(global.get(), "setInterval", script_set_interval, *this);
    set_runtime_method(global.get(), "clearInterval", script_clear_timer, *this);
    set_runtime_method(global.get(), "requestAnimationFrame", script_request_animation_frame, *this);
    set_runtime_method(global.get(), "cancelAnimationFrame", script_cancel_animation_frame, *this);
    set_method(global.get(), "btoa", script_btoa);
    set_method(global.get(), "atob", script_atob);
    JerryValue form_data_constructor(make_form_data_constructor(*this));
    set_property(window_object.get(), "FormData", form_data_constructor.get());
    set_property(global.get(), "FormData", form_data_constructor.get());
    set_runtime_method(global.get(), "addEventListener", window_add_event_listener, *this);
    set_runtime_method(global.get(), "removeEventListener", window_remove_event_listener, *this);
#define JELLYFRAME_DEFINE_GLOBAL_EVENT_HANDLER(js_name, event_type) \
    define_accessor(global.get(), #js_name, window_get_##js_name, window_set_##js_name);
    JELLYFRAME_WINDOW_EVENT_HANDLER_LIST(JELLYFRAME_DEFINE_GLOBAL_EVENT_HANDLER)
#undef JELLYFRAME_DEFINE_GLOBAL_EVENT_HANDLER
    JerryValue date_object(jerry_object_get_sz(global.get(), "Date"));
    if (jerry_value_is_object(date_object.get())) {
        set_runtime_method(date_object.get(), "now", script_date_now, *this);
    }
    if (network_fetch_ != nullptr) {
        JerryValue xhr_constructor(make_xml_http_request_constructor(*this));
        set_property(window_object.get(), "XMLHttpRequest", xhr_constructor.get());
        set_property(global.get(), "XMLHttpRequest", xhr_constructor.get());
    } else {
        delete_property(window_object.get(), "XMLHttpRequest");
        delete_property(global.get(), "XMLHttpRequest");
    }
    if (audio_host_.play != nullptr) {
        JerryValue audio_constructor(make_audio_constructor(*this));
        set_property(window_object.get(), "Audio", audio_constructor.get());
        set_property(global.get(), "Audio", audio_constructor.get());
    } else {
        delete_property(window_object.get(), "Audio");
        delete_property(global.get(), "Audio");
    }
    if (local_storage_ != nullptr) {
        JerryValue local_storage(make_local_storage_object(*this, *local_storage_));
        set_property(window_object.get(), "localStorage", local_storage.get());
        set_property(global.get(), "localStorage", local_storage.get());
    } else {
        delete_property(window_object.get(), "localStorage");
        delete_property(global.get(), "localStorage");
    }
}

#undef JELLYFRAME_NODE_EVENT_HANDLER_LIST
#undef JELLYFRAME_WINDOW_EVENT_HANDLER_LIST

void JerryScriptRuntime::bind_app_services(AppRuntimeHost& host, NetworkFetchMock& network) {
    app_host_ = &host;
    network_fetch_ = &network;
}

void JerryScriptRuntime::bind_location_service(AppRuntimeHost& host, AppLocationSnapshotMock& location) {
    app_host_ = &host;
    location_snapshot_ = &location;
}

void JerryScriptRuntime::bind_host_data_snapshot(const AppHostDataSnapshot& snapshot,
                                                 const AppHostDataAccessPolicy& policy) {
    host_data_snapshot_ = &snapshot;
    host_data_access_policy_ = std::make_unique<AppHostDataAccessPolicy>(policy);
}

void JerryScriptRuntime::bind_local_storage(AppLocalStorageShadow& storage) {
    local_storage_ = &storage;
}

void JerryScriptRuntime::bind_audio_host(ScriptAudioHost host) {
    audio_host_ = host;
}

void JerryScriptRuntime::bind_canvas_2d(Canvas2DRegistry& canvas) {
    canvas_2d_ = &canvas;
    clear_canvas_gradients();
    canvas_2d_->clear();
}

void JerryScriptRuntime::clear_canvas_2d() {
    clear_canvas_gradients();
    if (canvas_2d_ != nullptr) {
        canvas_2d_->clear();
    }
    canvas_2d_ = nullptr;
}

void JerryScriptRuntime::clear_app_services() {
    clear_xml_http_requests();
    clear_audio_elements();
    clear_geolocation_requests();
    clear_script_local_storage_bindings();
    app_host_ = nullptr;
    network_fetch_ = nullptr;
    location_snapshot_ = nullptr;
    host_data_snapshot_ = nullptr;
    host_data_access_policy_.reset();
    local_storage_ = nullptr;
    audio_host_ = {};
}

ScriptEvaluationResult JerryScriptRuntime::eval(std::string_view source, std::string_view source_name) {
    ScriptEvaluationResult output;

    JerryValue result(run_with_execution_budget(*this, [&]() {
        return evaluate_script(source, source_name).release();
    }));
    if (jerry_value_is_exception(result.get())) {
        JerryValue exception_value(jerry_exception_value(result.release(), true));
        output.ok = false;
        output.status = execution_watchdog_interrupted_
            ? ScriptEvaluationStatus::ExecutionBudgetExceeded
            : ScriptEvaluationStatus::Exception;
        output.error = value_to_string(exception_value.get());
        if (output.error.empty()) {
            output.error = "JavaScript exception";
        }
        return output;
    }

    output.ok = true;
    output.status = ScriptEvaluationStatus::Ok;
    output.value = value_to_string(result.get());
    return output;
}

bool JerryScriptRuntime::execution_watchdog_supported() const {
    return initialized_ && jerry_feature_enabled(JERRY_FEATURE_VM_EXEC_STOP);
}

bool JerryScriptRuntime::take_execution_watchdog_interrupt() {
    const bool interrupted = execution_watchdog_interrupt_pending_;
    execution_watchdog_interrupt_pending_ = false;
    return interrupted;
}

void JerryScriptRuntime::set_host_time_ms(std::uint64_t now_ms) {
    current_time_ms_ = now_ms;
}

void JerryScriptRuntime::set_system_state(ScriptSystemState state) {
    system_state_ = state;
}

ScriptSystemState JerryScriptRuntime::system_state() const {
    return system_state_;
}

bool JerryScriptRuntime::dispatch_visibility_change() {
    if (bound_document_ == nullptr) {
        return false;
    }
    Event event("visibilitychange", false, false);
    dispatch_event(*bound_document_, event);
    return true;
}

bool JerryScriptRuntime::request_modal_cancel() {
    Node* dialog = active_modal_dialog_;
    if (dialog == nullptr || dialog->type != NodeType::Element || dialog->tag_name != "dialog") {
        return false;
    }

    Event event("cancel", false, true);
    dispatch_event(*dialog, event);
    if (!event.default_prevented() && active_modal_dialog_ == dialog) {
        close_dialog(*dialog, std::string(), false);
    }
    return true;
}

Node* JerryScriptRuntime::active_modal_dialog() const {
    return active_modal_dialog_;
}

bool JerryScriptRuntime::dispatch_audio_event(std::uint32_t audio_id, ScriptAudioEventKind kind) {
    if (audio_id == 0) {
        return false;
    }
    for (const auto& audio : audio_elements_) {
        if (audio->active && audio->id == audio_id) {
            return dispatch_audio_event_to_element(*audio, kind);
        }
    }
    return false;
}

std::size_t JerryScriptRuntime::pump_timers(std::uint64_t now_ms, std::size_t max_callbacks) {
    current_time_ms_ = now_ms;
    std::size_t callbacks = 0;
    const std::size_t initial_count = timers_.size();
    for (std::size_t index = 0; index < initial_count && callbacks < max_callbacks; ++index) {
        ScriptTimer& timer = *timers_[index];
        if (!timer.active || timer.due_ms > now_ms || timer.callback == 0) {
            continue;
        }

        JerryValue callback(jerry_value_copy(timer.callback));
        if (timer.repeat) {
            const std::uint32_t next_delay = std::max<std::uint32_t>(1, timer.delay_ms);
            timer.due_ms = now_ms + next_delay;
        } else {
            timer.active = false;
            jerry_value_free(timer.callback);
            timer.callback = 0;
        }

        JerryValue result(run_with_execution_budget(*this, [&]() {
            return jerry_call(callback.get(), jerry_undefined(), nullptr, 0);
        }));
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
        ++callbacks;
    }

    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const std::unique_ptr<ScriptTimer>& timer) {
        return !timer->active;
    }), timers_.end());
    return callbacks;
}

std::size_t JerryScriptRuntime::pump_animation_frame(std::uint64_t now_ms, std::size_t max_callbacks) {
    current_time_ms_ = now_ms;
    if (animation_frame_callbacks_.empty() || max_callbacks == 0) {
        return 0;
    }
    std::vector<jerry_value_t> callbacks;
    callbacks.reserve(std::min(max_callbacks, animation_frame_callbacks_.size()));
    std::size_t pumped = 0;
    for (const auto& entry : animation_frame_callbacks_) {
        if (pumped >= max_callbacks || !entry->active || entry->callback == 0) {
            continue;
        }
        callbacks.push_back(jerry_value_copy(entry->callback));
        entry->active = false;
        jerry_value_free(entry->callback);
        entry->callback = 0;
        ++pumped;
    }
    animation_frame_callbacks_.erase(
        std::remove_if(animation_frame_callbacks_.begin(),
                       animation_frame_callbacks_.end(),
                       [](const std::unique_ptr<ScriptAnimationFrameCallback>& callback) {
                           return !callback->active;
                       }),
        animation_frame_callbacks_.end());

    const jerry_value_t timestamp = jerry_number(static_cast<double>(now_ms));
    for (jerry_value_t raw_callback : callbacks) {
        JerryValue callback(raw_callback);
        JerryValue result(run_with_execution_budget(*this, [&]() {
            return jerry_call(callback.get(), jerry_undefined(), &timestamp, 1);
        }));
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
    }
    jerry_value_free(timestamp);
    return pumped;
}

bool JerryScriptRuntime::handle_host_completion(const HostServiceCompletion& completion) {
    if (app_host_ == nullptr) {
        return false;
    }
    if (completion.kind == HostServiceJobKind::LocationSnapshot && location_snapshot_ != nullptr) {
        for (const auto& request : geolocation_requests_) {
            if (!request->active || request->job_id != completion.job_id) {
                continue;
            }
            if (completion.status == HostServiceStatus::Completed && completion.handle != 0) {
                const AppLocationSnapshotRecord* snapshot = location_snapshot_->snapshot(completion.handle);
                if (snapshot != nullptr && request->success_callback != 0 &&
                    jerry_value_is_function(request->success_callback)) {
                    JerryValue position(make_geolocation_position_object(*snapshot));
                    const jerry_value_t arg = position.get();
                    JerryValue callback(jerry_value_copy(request->success_callback));
                    JerryValue result(run_with_execution_budget(*this, [&]() {
                        return jerry_call(callback.get(), jerry_undefined(), &arg, 1);
                    }));
                    if (jerry_value_is_exception(result.get())) {
                        JerryValue exception_value(jerry_exception_value(result.release(), true));
                        (void) exception_value;
                    }
                } else {
                    call_geolocation_error(*this,
                                           request->error_callback,
                                           AppDeviceFailureReason::SampleUnavailable);
                }
                location_snapshot_->release_snapshot(*app_host_, completion.handle);
            } else {
                call_geolocation_error(*this,
                                       request->error_callback,
                                       classify_app_device_failure(AppServiceSubmitStatus::Accepted,
                                                                   completion.status,
                                                                   completion.error_code));
            }
            if (request->success_callback != 0) {
                jerry_value_free(request->success_callback);
                request->success_callback = 0;
            }
            if (request->error_callback != 0) {
                jerry_value_free(request->error_callback);
                request->error_callback = 0;
            }
            request->active = false;
            geolocation_requests_.erase(
                std::remove_if(geolocation_requests_.begin(),
                               geolocation_requests_.end(),
                               [](const std::unique_ptr<ScriptGeolocationRequest>& entry) {
                                   return !entry->active;
                               }),
                geolocation_requests_.end());
            return true;
        }
    }
    if (network_fetch_ == nullptr) {
        return false;
    }
    for (const auto& xhr : xml_http_requests_) {
        if (!xhr->active) {
            continue;
        }
        if (xhr->request.handle_completion(*app_host_, *network_fetch_, completion)) {
            dispatch_xhr_events(*xhr);
            return true;
        }
    }
    return false;
}

bool JerryScriptRuntime::handle_system_event(const AppSystemEvent& event) {
    ScriptSystemState next = system_state_;
    bool handled = true;
    bool visibility_changed = false;
    bool network_changed = false;
    switch (event.kind) {
    case AppSystemEventKind::NetworkStatusChanged:
        next.navigator_online = event.snapshot.network_online;
        network_changed = next.navigator_online != system_state_.navigator_online;
        break;
    case AppSystemEventKind::ScreenStateChanged:
        next.document_hidden = event.snapshot.low_power_mode || !event.snapshot.screen_on;
        visibility_changed = next.document_hidden != system_state_.document_hidden;
        break;
    case AppSystemEventKind::LowPowerModeChanged:
        next.document_hidden = event.snapshot.low_power_mode || !event.snapshot.screen_on;
        visibility_changed = next.document_hidden != system_state_.document_hidden;
        break;
    case AppSystemEventKind::TimeChanged:
        current_time_ms_ = event.snapshot.unix_time_ms;
        handled = false;
        break;
    case AppSystemEventKind::TimezoneChanged:
    case AppSystemEventKind::BatteryChanged:
        handled = false;
        break;
    }
    if (!handled) {
        return false;
    }
    system_state_ = next;
    if (network_changed) {
        dispatch_window_event(system_state_.navigator_online ? "online" : "offline");
    }
    if (visibility_changed) {
        dispatch_visibility_change();
    }
    return true;
}

bool JerryScriptRuntime::has_pending_timers() const {
    for (const auto& timer : timers_) {
        if (timer->active) {
            return true;
        }
    }
    return false;
}

bool JerryScriptRuntime::has_pending_animation_frames() const {
    for (const auto& callback : animation_frame_callbacks_) {
        if (callback->active) {
            return true;
        }
    }
    return false;
}

std::uint64_t JerryScriptRuntime::next_timer_due_ms() const {
    std::uint64_t due = std::numeric_limits<std::uint64_t>::max();
    for (const auto& timer : timers_) {
        if (timer->active) {
            due = std::min(due, timer->due_ms);
        }
    }
    return due == std::numeric_limits<std::uint64_t>::max() ? 0 : due;
}

std::size_t JerryScriptRuntime::detached_node_count() const {
    return detached_nodes_.detached_node_count();
}

ScriptRuntimeStatistics JerryScriptRuntime::statistics() const {
    ScriptRuntimeStatistics output;
    output.timer_count = timers_.size();
    output.animation_frame_callback_count = animation_frame_callbacks_.size();
    output.event_listener_count = event_listeners_.size();
    output.xml_http_request_count =
        static_cast<std::size_t>(std::count_if(xml_http_requests_.begin(),
                                               xml_http_requests_.end(),
                                               [](const std::unique_ptr<ScriptXmlHttpRequest>& xhr) {
                                                   return xhr->active;
                                               }));
    output.audio_element_count =
        static_cast<std::size_t>(std::count_if(audio_elements_.begin(),
                                               audio_elements_.end(),
                                               [](const std::unique_ptr<ScriptAudioElement>& audio) {
                                                   return audio->active;
                                               }));
    output.geolocation_request_count = geolocation_requests_.size();
    output.detached_nodes = detached_nodes_.detached_statistics();
    return output;
}

bool JerryScriptRuntime::can_adopt_detached_node() const {
    return detached_nodes_.detached_node_count() < options_.max_detached_nodes;
}

Node* JerryScriptRuntime::adopt_detached_node(std::unique_ptr<Node> node) {
    if (!node || !can_adopt_detached_node()) {
        return nullptr;
    }
    return detached_nodes_.adopt_detached_node(std::move(node));
}

std::unique_ptr<Node> JerryScriptRuntime::release_detached_node(Node& node) {
    return detached_nodes_.release_detached_node(node);
}

void JerryScriptRuntime::add_script_event_listener(Node& node,
                                                   std::string type,
                                                   std::uint32_t callback_value,
                                                   EventListenerOptions options) {
    event_listeners_.erase(std::remove_if(event_listeners_.begin(), event_listeners_.end(),
        [](const std::unique_ptr<ScriptEventListener>& listener) {
            return !listener->active;
        }), event_listeners_.end());
    if (event_listeners_.size() >= std::max<std::size_t>(1, options_.max_event_listeners)) {
        return;
    }
    auto listener = std::make_unique<ScriptEventListener>();
    listener->runtime = this;
    listener->node = &node;
    listener->type = std::move(type);
    listener->callback = jerry_value_copy(callback_value);
    listener->active = true;
    listener->options = options;
    listener->options.cleanup = script_node_event_listener_removed;
    listener->options.cleanup_context = listener.get();

    ScriptEventListener* raw = listener.get();
    listener->listener_id = node.add_event_listener(listener->type, [raw](Event& event) {
        if (raw == nullptr || !raw->active || raw->runtime == nullptr) {
            return;
        }

        JerryValue this_value(event.current_target() != nullptr
            ? make_node_wrapper(*raw->runtime, *const_cast<Node*>(event.current_target()), false)
            : jerry_undefined());
        JerryValue event_object(make_event_object(*raw->runtime, event));
        const jerry_value_t event_arg = event_object.get();
        JerryValue result(run_with_execution_budget(*raw->runtime, [&]() {
            return jerry_call(raw->callback, this_value.get(), &event_arg, 1);
        }));
        invalidate_event_object(event_object.get());
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
    }, listener->options);

    event_listeners_.push_back(std::move(listener));
}

void JerryScriptRuntime::remove_script_event_listener(Node& node, std::string type, std::uint32_t callback_value) {
    for (const auto& listener : event_listeners_) {
        if (!listener->active || listener->node != &node || listener->type != type ||
            !same_js_value(listener->callback, callback_value)) {
            continue;
        }

        listener->active = false;
        if (listener->listener_id != 0) {
            node.remove_event_listener(listener->listener_id);
            listener->listener_id = 0;
        }
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
        }
        return;
    }
}

void JerryScriptRuntime::set_script_event_handler(Node& node, std::string type, std::uint32_t callback_value) {
    for (const auto& listener : event_listeners_) {
        if (!listener->active || !listener->property_handler || listener->node != &node ||
            listener->type != type) {
            continue;
        }
        listener->active = false;
        if (listener->listener_id != 0) {
            node.remove_event_listener(listener->listener_id);
            listener->listener_id = 0;
        }
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
        }
        break;
    }
    event_listeners_.erase(std::remove_if(event_listeners_.begin(), event_listeners_.end(),
        [](const std::unique_ptr<ScriptEventListener>& listener) {
            return !listener->active;
        }), event_listeners_.end());
    if (!jerry_value_is_function(callback_value) ||
        event_listeners_.size() >= std::max<std::size_t>(1, options_.max_event_listeners)) {
        return;
    }

    auto listener = std::make_unique<ScriptEventListener>();
    listener->runtime = this;
    listener->node = &node;
    listener->type = std::move(type);
    listener->callback = jerry_value_copy(callback_value);
    listener->active = true;
    listener->property_handler = true;
    listener->options.cleanup = script_node_event_listener_removed;
    listener->options.cleanup_context = listener.get();

    ScriptEventListener* raw = listener.get();
    listener->listener_id = node.add_event_listener(listener->type, [raw](Event& event) {
        if (raw == nullptr || !raw->active || raw->runtime == nullptr || raw->callback == 0) {
            return;
        }

        JerryValue this_value(event.current_target() != nullptr
            ? make_node_wrapper(*raw->runtime, *const_cast<Node*>(event.current_target()), false)
            : jerry_undefined());
        JerryValue event_object(make_event_object(*raw->runtime, event));
        const jerry_value_t event_arg = event_object.get();
        JerryValue result(run_with_execution_budget(*raw->runtime, [&]() {
            return jerry_call(raw->callback, this_value.get(), &event_arg, 1);
        }));
        invalidate_event_object(event_object.get());
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
    }, listener->options);

    event_listeners_.push_back(std::move(listener));
}

std::uint32_t JerryScriptRuntime::get_script_event_handler(Node& node, const std::string& type) const {
    for (const auto& listener : event_listeners_) {
        if (listener->active && listener->property_handler && listener->node == &node &&
            listener->type == type && listener->callback != 0) {
            return jerry_value_copy(listener->callback);
        }
    }
    return jerry_null();
}

void JerryScriptRuntime::add_window_event_listener(std::string type,
                                                   std::uint32_t callback_value,
                                                   std::uint32_t target_value,
                                                   EventListenerOptions options) {
    event_listeners_.erase(std::remove_if(event_listeners_.begin(), event_listeners_.end(),
        [](const std::unique_ptr<ScriptEventListener>& listener) {
            return !listener->active;
        }), event_listeners_.end());
    if (event_listeners_.size() >= std::max<std::size_t>(1, options_.max_event_listeners)) {
        return;
    }
    auto listener = std::make_unique<ScriptEventListener>();
    listener->runtime = this;
    listener->type = std::move(type);
    listener->callback = jerry_value_copy(callback_value);
    listener->target_object = jerry_value_copy(target_value);
    listener->options = options;
    listener->active = true;
    event_listeners_.push_back(std::move(listener));
}

void JerryScriptRuntime::remove_window_event_listener(std::string type, std::uint32_t callback_value) {
    for (const auto& listener : event_listeners_) {
        if (!listener->active || listener->node != nullptr || listener->type != type ||
            !same_js_value(listener->callback, callback_value)) {
            continue;
        }
        listener->active = false;
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
        }
        if (listener->target_object != 0) {
            jerry_value_free(listener->target_object);
            listener->target_object = 0;
        }
        return;
    }
}

void JerryScriptRuntime::set_window_event_handler(std::string type,
                                                  std::uint32_t callback_value,
                                                  std::uint32_t target_value) {
    for (const auto& listener : event_listeners_) {
        if (!listener->active || !listener->property_handler || listener->node != nullptr ||
            listener->type != type) {
            continue;
        }
        listener->active = false;
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
        }
        if (listener->target_object != 0) {
            jerry_value_free(listener->target_object);
            listener->target_object = 0;
        }
        break;
    }
    event_listeners_.erase(std::remove_if(event_listeners_.begin(), event_listeners_.end(),
        [](const std::unique_ptr<ScriptEventListener>& listener) {
            return !listener->active;
        }), event_listeners_.end());
    if (!jerry_value_is_function(callback_value) ||
        event_listeners_.size() >= std::max<std::size_t>(1, options_.max_event_listeners)) {
        return;
    }
    auto listener = std::make_unique<ScriptEventListener>();
    listener->runtime = this;
    listener->type = std::move(type);
    listener->callback = jerry_value_copy(callback_value);
    listener->target_object = jerry_value_copy(target_value);
    listener->active = true;
    listener->property_handler = true;
    event_listeners_.push_back(std::move(listener));
}

std::uint32_t JerryScriptRuntime::get_window_event_handler(const std::string& type) const {
    for (const auto& listener : event_listeners_) {
        if (listener->active && listener->property_handler && listener->node == nullptr &&
            listener->type == type && listener->callback != 0) {
            return jerry_value_copy(listener->callback);
        }
    }
    return jerry_null();
}

std::string JerryScriptRuntime::location_hash() const {
    return route_fragment_.empty() ? std::string() : "#" + route_fragment_;
}

void JerryScriptRuntime::set_location_hash(std::string value) {
    if (!value.empty() && value.front() == '#') {
        value.erase(0, 1);
    }
    if (value == route_fragment_) {
        return;
    }
    push_route_history(std::move(value));
    dispatch_window_event("hashchange");
}

std::size_t JerryScriptRuntime::route_history_length() const {
    return route_history_.empty() ? 1U : route_history_.size();
}

void JerryScriptRuntime::push_route_history(std::string value) {
    if (route_history_.empty()) {
        route_history_.push_back(route_fragment_);
        route_history_index_ = 0;
    }
    route_history_.erase(route_history_.begin() + static_cast<std::ptrdiff_t>(route_history_index_ + 1),
                         route_history_.end());
    route_history_.push_back(std::move(value));
    route_history_index_ = route_history_.size() - 1;
    const std::size_t max_entries = std::max<std::size_t>(1, options_.max_route_history_entries);
    if (route_history_.size() > max_entries) {
        route_history_.erase(route_history_.begin());
        --route_history_index_;
    }
    route_fragment_ = route_history_[route_history_index_];
}

void JerryScriptRuntime::replace_route_history(std::string value) {
    if (route_history_.empty()) {
        route_fragment_ = std::move(value);
        return;
    }
    route_history_[route_history_index_] = std::move(value);
    route_fragment_ = route_history_[route_history_index_];
}

bool JerryScriptRuntime::traverse_route_history(int delta) {
    if (delta == 0 || route_history_.empty()) {
        return false;
    }
    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(route_history_index_) + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(route_history_.size())) {
        return false;
    }
    const std::string previous = route_fragment_;
    route_history_index_ = static_cast<std::size_t>(target);
    route_fragment_ = route_history_[route_history_index_];
    dispatch_window_event("popstate");
    if (route_fragment_ != previous) {
        dispatch_window_event("hashchange");
    }
    return true;
}

void JerryScriptRuntime::dispatch_window_event(const char* type) {
    struct PendingWindowEventCallback {
        jerry_value_t callback = 0;
        jerry_value_t target = 0;
    };
    std::vector<PendingWindowEventCallback> callbacks;
    for (const auto& listener : event_listeners_) {
        if (!listener->active || listener->node != nullptr || listener->callback == 0 ||
            listener->type != type) {
            continue;
        }
        callbacks.push_back(PendingWindowEventCallback{
            jerry_value_copy(listener->callback),
            listener->target_object != 0 ? jerry_value_copy(listener->target_object) : jerry_undefined(),
        });
        if (listener->options.once) {
            listener->active = false;
            jerry_value_free(listener->callback);
            listener->callback = 0;
            if (listener->target_object != 0) {
                jerry_value_free(listener->target_object);
                listener->target_object = 0;
            }
        }
    }
    event_listeners_.erase(std::remove_if(event_listeners_.begin(),
                                          event_listeners_.end(),
                                          [](const std::unique_ptr<ScriptEventListener>& listener) {
                                              return !listener->active;
                                          }),
                           event_listeners_.end());

    for (PendingWindowEventCallback& entry : callbacks) {
        JerryValue callback(entry.callback);
        JerryValue this_value(entry.target);
        JerryValue event_object(make_window_event_object(type, this_value.get()));
        const jerry_value_t event_arg = event_object.get();
        JerryValue result(run_with_execution_budget(*this, [&]() {
            return jerry_call(callback.get(), this_value.get(), &event_arg, 1);
        }));
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
    }
}

void JerryScriptRuntime::clear_script_event_listeners() {
    for (const auto& listener : event_listeners_) {
        if (listener->active && listener->node != nullptr && listener->listener_id != 0) {
            listener->node->remove_event_listener(listener->listener_id);
        }
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
        }
        if (listener->target_object != 0) {
            jerry_value_free(listener->target_object);
            listener->target_object = 0;
        }
        listener->active = false;
        listener->listener_id = 0;
    }
    event_listeners_.clear();
}

std::uint32_t JerryScriptRuntime::add_timer(std::uint32_t callback_value,
                                            std::uint32_t delay_ms,
                                            bool repeat) {
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const std::unique_ptr<ScriptTimer>& timer) {
        return !timer->active;
    }), timers_.end());
    if (timers_.size() >= std::max<std::size_t>(1, options_.max_timers)) {
        return 0;
    }
    auto timer = std::make_unique<ScriptTimer>();
    timer->id = next_timer_id_++;
    if (next_timer_id_ == 0) {
        next_timer_id_ = 1;
    }
    timer->due_ms = current_time_ms_ + delay_ms;
    timer->delay_ms = delay_ms;
    timer->callback = jerry_value_copy(callback_value);
    timer->repeat = repeat;
    timer->active = true;
    const std::uint32_t id = timer->id;
    timers_.push_back(std::move(timer));
    return id;
}

void JerryScriptRuntime::clear_timer(std::uint32_t id) {
    if (id == 0) {
        return;
    }
    for (const auto& timer : timers_) {
        if (!timer->active || timer->id != id) {
            continue;
        }
        timer->active = false;
        if (timer->callback != 0) {
            jerry_value_free(timer->callback);
            timer->callback = 0;
        }
        return;
    }
}

void JerryScriptRuntime::clear_timers() {
    for (const auto& timer : timers_) {
        timer->active = false;
        if (timer->callback != 0) {
            jerry_value_free(timer->callback);
            timer->callback = 0;
        }
    }
    timers_.clear();
}

std::uint32_t JerryScriptRuntime::add_animation_frame_callback(std::uint32_t callback_value) {
    animation_frame_callbacks_.erase(
        std::remove_if(animation_frame_callbacks_.begin(),
                       animation_frame_callbacks_.end(),
                       [](const std::unique_ptr<ScriptAnimationFrameCallback>& callback) {
                           return !callback->active;
                       }),
        animation_frame_callbacks_.end());
    if (animation_frame_callbacks_.size() >= std::max<std::size_t>(1, options_.max_animation_frame_callbacks)) {
        return 0;
    }
    auto callback = std::make_unique<ScriptAnimationFrameCallback>();
    callback->id = next_animation_frame_id_++;
    if (next_animation_frame_id_ == 0) {
        next_animation_frame_id_ = 1;
    }
    callback->callback = jerry_value_copy(callback_value);
    callback->active = true;
    const std::uint32_t id = callback->id;
    animation_frame_callbacks_.push_back(std::move(callback));
    return id;
}

void JerryScriptRuntime::cancel_animation_frame_callback(std::uint32_t id) {
    if (id == 0) {
        return;
    }
    for (const auto& callback : animation_frame_callbacks_) {
        if (!callback->active || callback->id != id) {
            continue;
        }
        callback->active = false;
        if (callback->callback != 0) {
            jerry_value_free(callback->callback);
            callback->callback = 0;
        }
        break;
    }
    animation_frame_callbacks_.erase(
        std::remove_if(animation_frame_callbacks_.begin(),
                       animation_frame_callbacks_.end(),
                       [](const std::unique_ptr<ScriptAnimationFrameCallback>& callback) {
                           return !callback->active;
                       }),
        animation_frame_callbacks_.end());
}

void JerryScriptRuntime::clear_animation_frame_callbacks() {
    for (const auto& callback : animation_frame_callbacks_) {
        callback->active = false;
        if (callback->callback != 0) {
            jerry_value_free(callback->callback);
            callback->callback = 0;
        }
    }
    animation_frame_callbacks_.clear();
}

ScriptXmlHttpRequest* JerryScriptRuntime::create_xml_http_request() {
    xml_http_requests_.erase(std::remove_if(xml_http_requests_.begin(), xml_http_requests_.end(),
        [](const std::unique_ptr<ScriptXmlHttpRequest>& xhr) {
            return !xhr->active && !xhr->js_object_alive;
        }), xml_http_requests_.end());
    const auto active_count =
        static_cast<std::size_t>(std::count_if(xml_http_requests_.begin(),
                                               xml_http_requests_.end(),
                                               [](const std::unique_ptr<ScriptXmlHttpRequest>& xhr) {
                                                   return xhr->active;
                                               }));
    if (active_count >= std::max<std::size_t>(1, options_.max_xml_http_requests)) {
        return nullptr;
    }
    auto xhr = std::make_unique<ScriptXmlHttpRequest>();
    xhr->runtime = this;
    xhr->active = true;
    ScriptXmlHttpRequest* raw = xhr.get();
    xml_http_requests_.push_back(std::move(xhr));
    return raw;
}

void JerryScriptRuntime::clear_xml_http_requests() {
    for (const auto& xhr : xml_http_requests_) {
        if (xhr->active && app_host_ != nullptr) {
            xhr->request.abort(*app_host_);
        }
        if (xhr->object != 0) {
            jerry_value_free(xhr->object);
            xhr->object = 0;
        }
        for (jerry_value_t& callback : xhr->callbacks) {
            if (callback != 0) {
                jerry_value_free(callback);
                callback = 0;
            }
        }
        xhr->active = false;
        xhr->runtime = nullptr;
    }
    xml_http_requests_.erase(std::remove_if(xml_http_requests_.begin(), xml_http_requests_.end(),
        [](const std::unique_ptr<ScriptXmlHttpRequest>& xhr) {
            return !xhr->js_object_alive;
        }), xml_http_requests_.end());
}

ScriptAudioElement* JerryScriptRuntime::create_audio_element(std::string src) {
    audio_elements_.erase(std::remove_if(audio_elements_.begin(), audio_elements_.end(),
        [](const std::unique_ptr<ScriptAudioElement>& audio) {
            return !audio->active && !audio->js_object_alive;
        }), audio_elements_.end());
    const auto active_count =
        static_cast<std::size_t>(std::count_if(audio_elements_.begin(),
                                               audio_elements_.end(),
                                               [](const std::unique_ptr<ScriptAudioElement>& audio) {
                                                   return audio->active;
                                               }));
    if (active_count >= std::max<std::size_t>(1, options_.max_audio_elements)) {
        return nullptr;
    }
    auto audio = std::make_unique<ScriptAudioElement>();
    audio->runtime = this;
    audio->id = next_audio_id_++;
    if (next_audio_id_ == 0) {
        next_audio_id_ = 1;
    }
    audio->src = std::move(src);
    audio->active = true;
    ScriptAudioElement* raw = audio.get();
    audio_elements_.push_back(std::move(audio));
    return raw;
}

void JerryScriptRuntime::clear_audio_elements() {
    for (const auto& audio : audio_elements_) {
        if (audio->object != 0) {
            jerry_value_free(audio->object);
            audio->object = 0;
        }
        for (jerry_value_t& callback : audio->property_callbacks) {
            if (callback != 0) {
                jerry_value_free(callback);
                callback = 0;
            }
        }
        for (jerry_value_t& callback : audio->event_listeners) {
            if (callback != 0) {
                jerry_value_free(callback);
                callback = 0;
            }
        }
        audio->active = false;
        audio->runtime = nullptr;
    }
    audio_elements_.erase(std::remove_if(audio_elements_.begin(), audio_elements_.end(),
        [](const std::unique_ptr<ScriptAudioElement>& audio) {
            return !audio->js_object_alive;
        }), audio_elements_.end());
}

ScriptGeolocationRequest* JerryScriptRuntime::create_geolocation_request(std::uint32_t job_id,
                                                                         std::uint32_t success_callback,
                                                                         std::uint32_t error_callback) {
    geolocation_requests_.erase(
        std::remove_if(geolocation_requests_.begin(),
                       geolocation_requests_.end(),
                       [](const std::unique_ptr<ScriptGeolocationRequest>& request) {
                           return !request->active;
                       }),
        geolocation_requests_.end());
    if (geolocation_requests_.size() >= options_.max_geolocation_requests) {
        return nullptr;
    }
    auto request = std::make_unique<ScriptGeolocationRequest>();
    request->job_id = job_id;
    request->success_callback = jerry_value_copy(success_callback);
    if (error_callback != 0) {
        request->error_callback = jerry_value_copy(error_callback);
    }
    request->active = true;
    ScriptGeolocationRequest* raw = request.get();
    geolocation_requests_.push_back(std::move(request));
    return raw;
}

void JerryScriptRuntime::clear_geolocation_requests() {
    for (const auto& request : geolocation_requests_) {
        if (request->success_callback != 0) {
            jerry_value_free(request->success_callback);
            request->success_callback = 0;
        }
        if (request->error_callback != 0) {
            jerry_value_free(request->error_callback);
            request->error_callback = 0;
        }
        request->active = false;
    }
    geolocation_requests_.clear();
}

ScriptCanvasGradient* JerryScriptRuntime::create_canvas_gradient(std::uint32_t gradient_id) {
    if (gradient_id == 0) {
        return nullptr;
    }
    auto gradient = std::make_unique<ScriptCanvasGradient>();
    gradient->id = gradient_id;
    ScriptCanvasGradient* raw = gradient.get();
    canvas_gradients_.push_back(std::move(gradient));
    return raw;
}

void JerryScriptRuntime::clear_canvas_gradients() {
    canvas_gradients_.clear();
}

ScriptDialogState* JerryScriptRuntime::dialog_state_for(Node& node, bool create) {
    for (const auto& state : dialog_states_) {
        if (state != nullptr && state->node == &node) {
            return state.get();
        }
    }
    if (!create || dialog_states_.size() >= 8) {
        return nullptr;
    }
    auto state = std::make_unique<ScriptDialogState>();
    state->node = &node;
    ScriptDialogState* raw = state.get();
    dialog_states_.push_back(std::move(state));
    return raw;
}

const ScriptDialogState* JerryScriptRuntime::dialog_state_for(const Node& node) const {
    for (const auto& state : dialog_states_) {
        if (state != nullptr && state->node == &node) {
            return state.get();
        }
    }
    return nullptr;
}

bool JerryScriptRuntime::show_modal_dialog(Node& node) {
    if (node.type != NodeType::Element || node.tag_name != "dialog" || active_modal_dialog_ != nullptr ||
        node.attributes.find("open") != node.attributes.end() || dialog_state_for(node, true) == nullptr) {
        return false;
    }
    active_modal_dialog_ = &node;
    node.set_attribute("open", "");
    return true;
}

void JerryScriptRuntime::close_dialog(Node& node, std::string return_value, bool update_return_value) {
    if (node.type != NodeType::Element || node.tag_name != "dialog" ||
        node.attributes.find("open") == node.attributes.end()) {
        return;
    }
    if (update_return_value) {
        set_dialog_return_value(node, std::move(return_value));
    }
    node.remove_attribute("open");
    if (active_modal_dialog_ == &node) {
        active_modal_dialog_ = nullptr;
    }
    Event event("close", false, false);
    dispatch_event(node, event);
}

void JerryScriptRuntime::set_dialog_open(Node& node, bool open) {
    if (node.type != NodeType::Element || node.tag_name != "dialog") {
        return;
    }
    if (open) {
        node.set_attribute("open", "");
        return;
    }
    node.remove_attribute("open");
    if (active_modal_dialog_ == &node) {
        active_modal_dialog_ = nullptr;
    }
}

std::string JerryScriptRuntime::dialog_return_value(const Node& node) const {
    const ScriptDialogState* state = dialog_state_for(node);
    return state != nullptr ? state->return_value : std::string();
}

void JerryScriptRuntime::set_dialog_return_value(Node& node, std::string value) {
    if (ScriptDialogState* state = dialog_state_for(node, true)) {
        state->return_value = std::move(value);
    }
}

void JerryScriptRuntime::clear_dialog_states() {
    dialog_states_.clear();
    active_modal_dialog_ = nullptr;
}

} // namespace jellyframe
