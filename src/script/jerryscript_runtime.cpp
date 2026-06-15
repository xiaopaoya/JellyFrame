#include "script/jerryscript_runtime.h"

#include "core/form_control.h"
#include "core/style.h"

#include <jerryscript.h>

#include <algorithm>
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
    bool active = false;
};

struct ScriptTimer {
    std::uint32_t id = 0;
    std::uint64_t due_ms = 0;
    std::uint32_t delay_ms = 0;
    jerry_value_t callback = 0;
    bool repeat = false;
    bool active = false;
};

struct ScriptRuntimeAccess {
    static Node& adopt_detached_node(JerryScriptRuntime& runtime, std::unique_ptr<Node> node) {
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

    static void remove_script_event_listener(JerryScriptRuntime& runtime,
                                             Node& node,
                                             std::string type,
                                             jerry_value_t callback) {
        runtime.remove_script_event_listener(node, std::move(type), callback);
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

};

namespace {

bool g_runtime_active = false;

const jerry_object_native_info_t kNodeNativeInfo = {nullptr, 0, 0};
const jerry_object_native_info_t kRuntimeNativeInfo = {nullptr, 0, 0};
const jerry_object_native_info_t kEventNativeInfo = {nullptr, 0, 0};

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

JerryValue string_to_value(const std::string& value) {
    return JerryValue(jerry_string_sz(value.c_str()));
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

Node* native_node(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<Node*>(jerry_object_get_native_ptr(object, &kNodeNativeInfo));
}

JerryScriptRuntime* native_runtime(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<JerryScriptRuntime*>(jerry_object_get_native_ptr(object, &kRuntimeNativeInfo));
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

bool has_attribute(const Node& node, const std::string& name) {
    return node.attributes.find(name) != node.attributes.end();
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

Event* native_event(const jerry_value_t object) {
    if (!jerry_value_is_object(object)) {
        return nullptr;
    }
    return static_cast<Event*>(jerry_object_get_native_ptr(object, &kEventNativeInfo));
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

jerry_value_t event_prevent_default(const jerry_call_info_t* call_info_p,
                                    const jerry_value_t[],
                                    const jerry_length_t) {
    Event* event = native_event(call_info_p->this_value);
    if (event == nullptr) {
        return throw_type_error("preventDefault called on non-event object");
    }
    event->prevent_default();
    return jerry_undefined();
}

jerry_value_t event_stop_propagation(const jerry_call_info_t* call_info_p,
                                     const jerry_value_t[],
                                     const jerry_length_t) {
    Event* event = native_event(call_info_p->this_value);
    if (event == nullptr) {
        return throw_type_error("stopPropagation called on non-event object");
    }
    event->stop_propagation();
    return jerry_undefined();
}

jerry_value_t event_stop_immediate_propagation(const jerry_call_info_t* call_info_p,
                                               const jerry_value_t[],
                                               const jerry_length_t) {
    Event* event = native_event(call_info_p->this_value);
    if (event == nullptr) {
        return throw_type_error("stopImmediatePropagation called on non-event object");
    }
    event->stop_immediate_propagation();
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

void set_number_property(jerry_value_t object, const char* name, double value);
void set_bool_property(jerry_value_t object, const char* name, bool value);
void set_property(jerry_value_t object, const char* name, jerry_value_t value);
void set_method(jerry_value_t object, const char* name, jerry_external_handler_t handler);
jerry_value_t make_node_wrapper(JerryScriptRuntime& runtime, Node& node, bool document_methods);

jerry_value_t make_event_object(JerryScriptRuntime& runtime, Event& event) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kEventNativeInfo, &event);
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

    if (auto* mouse = dynamic_cast<MouseEvent*>(&event)) {
        set_number_property(object.get(), "clientX", mouse->client_x);
        set_number_property(object.get(), "clientY", mouse->client_y);
        set_number_property(object.get(), "button", mouse->button);
        set_number_property(object.get(), "buttons", mouse->buttons);
        set_bool_property(object.get(), "altKey", mouse->alt_key);
        set_bool_property(object.get(), "ctrlKey", mouse->ctrl_key);
        set_bool_property(object.get(), "metaKey", mouse->meta_key);
        set_bool_property(object.get(), "shiftKey", mouse->shift_key);
    }
    if (auto* wheel = dynamic_cast<WheelEvent*>(&event)) {
        set_number_property(object.get(), "deltaX", wheel->delta_x);
        set_number_property(object.get(), "deltaY", wheel->delta_y);
        set_number_property(object.get(), "deltaMode", wheel->delta_mode);
    }

    set_method(object.get(), "preventDefault", event_prevent_default);
    set_method(object.get(), "stopPropagation", event_stop_propagation);
    set_method(object.get(), "stopImmediatePropagation", event_stop_immediate_propagation);
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

jerry_value_t node_get_value(const jerry_call_info_t* call_info_p,
                             const jerry_value_t[],
                             const jerry_length_t) {
    Node* node = native_node(call_info_p->this_value);
    if (node == nullptr || !is_form_control(*node)) {
        return jerry_undefined();
    }
    return jerry_string_sz(form_control_value(*node).c_str());
}

jerry_value_t node_set_value(const jerry_call_info_t* call_info_p,
                             const jerry_value_t args_p[],
                             const jerry_length_t args_count) {
    Node* node = native_node(call_info_p->this_value);
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
jerry_value_t document_create_element(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t document_create_text_node(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count);
jerry_value_t node_add_event_listener(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count);
jerry_value_t node_remove_event_listener(const jerry_call_info_t* call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count);
jerry_value_t node_matches(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count);
jerry_value_t node_closest(const jerry_call_info_t* call_info_p,
                           const jerry_value_t args_p[],
                           const jerry_length_t args_count);
jerry_value_t element_remove_attribute(const jerry_call_info_t* call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count);

void set_property(jerry_value_t object, const char* name, jerry_value_t value) {
    JerryValue result(jerry_object_set_sz(object, name, value));
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

jerry_value_t make_dataset_object(JerryScriptRuntime& runtime, Node& node) {
    (void) runtime;
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
JELLYFRAME_STYLE_ACCESSOR(textAlign, "text-align")
JELLYFRAME_STYLE_ACCESSOR(fontWeight, "font-weight")
JELLYFRAME_STYLE_ACCESSOR(width, "width")
JELLYFRAME_STYLE_ACCESSOR(height, "height")

#undef JELLYFRAME_STYLE_ACCESSOR

jerry_value_t make_style_object(JerryScriptRuntime& runtime, Node& node) {
    (void) runtime;
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kNodeNativeInfo, &node);
    define_accessor(object.get(), "display", style_get_display, style_set_display);
    define_accessor(object.get(), "color", style_get_color, style_set_color);
    define_accessor(object.get(), "background", style_get_background, style_set_background);
    define_accessor(object.get(), "backgroundColor", style_get_backgroundColor, style_set_backgroundColor);
    define_accessor(object.get(), "textAlign", style_get_textAlign, style_set_textAlign);
    define_accessor(object.get(), "fontWeight", style_get_fontWeight, style_set_fontWeight);
    define_accessor(object.get(), "width", style_get_width, style_set_width);
    define_accessor(object.get(), "height", style_get_height, style_set_height);
    return object.release();
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

jerry_value_t make_node_wrapper(JerryScriptRuntime& runtime, Node& node, bool document_methods) {
    JerryValue object(jerry_object());
    jerry_object_set_native_ptr(object.get(), &kNodeNativeInfo, &node);
    jerry_object_set_native_ptr(object.get(), &kRuntimeNativeInfo, &runtime);

    define_accessor(object.get(), "textContent", node_get_text_content, node_set_text_content);
    define_accessor(object.get(), "parentElement", node_get_parent_element, node_ignore_setter);
    define_accessor(object.get(), "children", node_get_children, node_ignore_setter);
    define_accessor(object.get(), "dataset", node_get_dataset, node_ignore_setter);
    define_accessor(object.get(), "style", node_get_style_object, node_ignore_setter);
    define_accessor(object.get(), "hidden", node_get_hidden, node_set_hidden);
    define_accessor(object.get(), "disabled", node_get_disabled, node_set_disabled);
    if (is_form_control(node)) {
        define_accessor(object.get(), "value", node_get_value, node_set_value);
        define_accessor(object.get(), "checked", node_get_checked, node_set_checked);
        define_accessor(object.get(), "selectedIndex", node_get_selected_index, node_set_selected_index);
    }
    set_method(object.get(), "appendChild", node_append_child);
    set_method(object.get(), "removeChild", node_remove_child);
    set_method(object.get(), "setAttribute", element_set_attribute);
    set_method(object.get(), "getAttribute", element_get_attribute);
    set_method(object.get(), "removeAttribute", element_remove_attribute);
    set_method(object.get(), "addEventListener", node_add_event_listener);
    set_method(object.get(), "removeEventListener", node_remove_event_listener);
    set_method(object.get(), "matches", node_matches);
    set_method(object.get(), "closest", node_closest);

    if (node.type == NodeType::Element) {
        set_property(object.get(), "tagName", string_to_value(node.tag_name).get());
        set_property(object.get(), "nodeType", JerryValue(jerry_number(1)).get());
    } else {
        set_property(object.get(), "nodeType", JerryValue(jerry_number(3)).get());
    }

    if (document_methods) {
        set_method(object.get(), "getElementById", document_get_element_by_id);
        set_method(object.get(), "createElement", document_create_element);
        set_method(object.get(), "createTextNode", document_create_text_node);
    }

    return object.release();
}

Node& append_or_move_child(JerryScriptRuntime& runtime, Node& parent, Node& child) {
    if (&parent == &child || is_ancestor_of(child, parent)) {
        throw std::runtime_error("appendChild would create a cycle");
    }

    if (child.parent != nullptr) {
        Node* old_parent = child.parent;
        auto detached = old_parent->detach_child(child);
        if (!detached) {
            throw std::runtime_error("appendChild could not detach existing child");
        }
        return parent.append_child(std::move(detached));
    }

    if (auto detached = ScriptRuntimeAccess::release_detached_node(runtime, child)) {
        return parent.append_child(std::move(detached));
    }

    throw std::runtime_error("appendChild received a node outside this runtime");
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

    auto detached = parent->detach_child(*child);
    if (!detached) {
        return jerry_throw_sz(JERRY_ERROR_TYPE, "removeChild failed");
    }
    Node& adopted = ScriptRuntimeAccess::adopt_detached_node(*runtime, std::move(detached));
    return make_node_wrapper(*runtime, adopted, false);
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

jerry_value_t document_create_element(const jerry_call_info_t* call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr || args_count < 1) {
        return throw_type_error("createElement requires a tag name");
    }

    Node& node = ScriptRuntimeAccess::adopt_detached_node(
        *runtime, make_element(ascii_lowercase(value_to_string(args_p[0]))));
    return make_node_wrapper(*runtime, node, false);
}

jerry_value_t document_create_text_node(const jerry_call_info_t* call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count) {
    JerryScriptRuntime* runtime = native_runtime(call_info_p->this_value);
    if (runtime == nullptr) {
        return throw_type_error("createTextNode requires a document");
    }

    Node& node = ScriptRuntimeAccess::adopt_detached_node(
        *runtime, make_text(args_count > 0 ? value_to_string(args_p[0]) : std::string()));
    return make_node_wrapper(*runtime, node, false);
}

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

} // namespace

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
      }) {}

JerryScriptRuntime::~JerryScriptRuntime() {
    if (initialized_) {
        clear_script_event_listeners();
        clear_timers();
        jerry_cleanup();
        initialized_ = false;
        g_runtime_active = false;
    }
}

void JerryScriptRuntime::bind_document(Node& document) {
    clear_script_event_listeners();
    clear_timers();
    detached_nodes_.clear();

    JerryValue global(jerry_current_realm());
    jerry_object_set_native_ptr(global.get(), &kRuntimeNativeInfo, this);
    JerryValue document_object(make_node_wrapper(*this, document, true));
    JerryValue window_object(jerry_object());
    jerry_object_set_native_ptr(window_object.get(), &kRuntimeNativeInfo, this);

    set_property(window_object.get(), "document", document_object.get());
    set_property(window_object.get(), "window", window_object.get());
    set_property(global.get(), "document", document_object.get());
    set_property(global.get(), "window", window_object.get());
    set_runtime_method(window_object.get(), "setTimeout", script_set_timeout, *this);
    set_runtime_method(window_object.get(), "clearTimeout", script_clear_timer, *this);
    set_runtime_method(window_object.get(), "setInterval", script_set_interval, *this);
    set_runtime_method(window_object.get(), "clearInterval", script_clear_timer, *this);
    set_runtime_method(global.get(), "setTimeout", script_set_timeout, *this);
    set_runtime_method(global.get(), "clearTimeout", script_clear_timer, *this);
    set_runtime_method(global.get(), "setInterval", script_set_interval, *this);
    set_runtime_method(global.get(), "clearInterval", script_clear_timer, *this);
}

ScriptEvaluationResult JerryScriptRuntime::eval(std::string_view source, std::string_view source_name) {
    ScriptEvaluationResult output;

    JerryValue result = evaluate_script(source, source_name);
    if (jerry_value_is_exception(result.get())) {
        JerryValue exception_value(jerry_exception_value(result.release(), true));
        output.ok = false;
        output.error = value_to_string(exception_value.get());
        if (output.error.empty()) {
            output.error = "JavaScript exception";
        }
        return output;
    }

    output.ok = true;
    output.value = value_to_string(result.get());
    return output;
}

void JerryScriptRuntime::set_host_time_ms(std::uint64_t now_ms) {
    current_time_ms_ = now_ms;
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

        JerryValue result(jerry_call(callback.get(), jerry_undefined(), nullptr, 0));
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

bool JerryScriptRuntime::has_pending_timers() const {
    for (const auto& timer : timers_) {
        if (timer->active) {
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

Node& JerryScriptRuntime::adopt_detached_node(std::unique_ptr<Node> node) {
    detached_nodes_.push_back(std::move(node));
    return *detached_nodes_.back();
}

std::unique_ptr<Node> JerryScriptRuntime::release_detached_node(Node& node) {
    for (auto it = detached_nodes_.begin(); it != detached_nodes_.end(); ++it) {
        if (it->get() != &node) {
            continue;
        }

        std::unique_ptr<Node> released = std::move(*it);
        detached_nodes_.erase(it);
        return released;
    }
    return nullptr;
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
        JerryValue result(jerry_call(raw->callback, this_value.get(), &event_arg, 1));
        if (jerry_value_is_exception(result.get())) {
            JerryValue exception_value(jerry_exception_value(result.release(), true));
            (void) exception_value;
        }
    }, options);

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

void JerryScriptRuntime::clear_script_event_listeners() {
    for (const auto& listener : event_listeners_) {
        if (listener->active && listener->node != nullptr && listener->listener_id != 0) {
            listener->node->remove_event_listener(listener->listener_id);
        }
        if (listener->callback != 0) {
            jerry_value_free(listener->callback);
            listener->callback = 0;
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

} // namespace jellyframe
