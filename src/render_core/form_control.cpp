#include "render_core/form_control.h"

#include "render_core/text_scan.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

bool has_attribute(const Node& node, const std::string& name) {
    return node.attributes.find(name) != node.attributes.end();
}

bool ascii_equals_ignore_case(std::string_view value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char actual = static_cast<unsigned char>(value[index]);
        const unsigned char wanted = static_cast<unsigned char>(expected[index]);
        if (std::tolower(actual) != std::tolower(wanted)) {
            return false;
        }
    }
    return true;
}

int parse_int_attribute(const Node& node, const std::string& name, int fallback) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE || parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

int max_length_for(const Node& node) {
    const int value = parse_int_attribute(node, "maxlength", -1);
    return value >= 0 ? value : -1;
}

std::size_t codepoint_count(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size();) {
        consume_utf8_codepoint(text, index);
        ++count;
    }
    return count;
}

std::string_view clamp_append_text(const std::string& current, std::string_view text, int max_length) {
    if (max_length < 0) {
        return text;
    }
    const std::size_t current_count = codepoint_count(current);
    if (current_count >= static_cast<std::size_t>(max_length)) {
        return {};
    }
    const std::size_t remaining = static_cast<std::size_t>(max_length) - current_count;
    std::size_t index = 0;
    for (std::size_t count = 0; count < remaining && index < text.size(); ++count) {
        consume_utf8_codepoint(text, index);
    }
    return text.substr(0, index);
}

void remove_last_utf8_codepoint(std::string& text) {
    if (text.empty()) {
        return;
    }
    std::size_t previous = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        previous = index;
        consume_utf8_codepoint(text, index);
    }
    text.resize(previous);
}

void append_descendant_text(const Node& node, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            output += current->text;
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

using OptionList = std::vector<const Node*>;

void collect_options(const Node& node, OptionList& options) {
    options.clear();
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            options.push_back(current);
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

const Node* option_at(const OptionList& options, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < options.size()
        ? options[static_cast<std::size_t>(index)]
        : nullptr;
}

int first_selected_option_index(const OptionList& options) {
    for (std::size_t index = 0; index < options.size(); ++index) {
        if (has_attribute(*options[index], "selected")) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::string option_value(const Node& option);

bool set_selected_index_with_options(Node& node, int selected_index, const OptionList& options) {
    if (options.empty()) {
        selected_index = -1;
    } else {
        selected_index = std::max(0, std::min(selected_index,
                                              static_cast<int>(options.size() - 1)));
    }

    FormControlState& state = ensure_form_control_state(node);
    if (state.selected_index == selected_index) {
        return false;
    }
    state.selected_index = selected_index;
    const Node* option = option_at(options, selected_index);
    state.value = option != nullptr ? option_value(*option) : std::string{};
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

int option_count(const OptionList& options) {
    return static_cast<int>(std::min<std::size_t>(options.size(),
                                                  static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

std::string option_text(const Node& option) {
    std::string text;
    append_descendant_text(option, text);
    return text;
}

std::string option_value(const Node& option) {
    const std::string& value = option.attribute("value");
    return has_attribute(option, "value") ? value : option_text(option);
}

int option_index_by_value(const OptionList& options, const std::string& value) {
    for (std::size_t index = 0; index < options.size(); ++index) {
        if (option_value(*options[index]) == value) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const Node* root_of(const Node& node) {
    const Node* root = &node;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    return root;
}

const Node* find_element_by_id(const Node& node, const std::string& id) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->attribute("id") == id) {
            return current;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return nullptr;
}

bool ascii_starts_with_case_insensitive(const std::string& value, const std::string& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

bool first_datalist_option_value(const Node& node, const std::string& prefix, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            const std::string value = option_value(*current);
            if (!value.empty() && (prefix.empty() || ascii_starts_with_case_insensitive(value, prefix))) {
                output = value;
                return true;
            }
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return false;
}

FormControlState make_initial_state(const Node& node) {
    FormControlState state;
    state.kind = form_control_kind(node);
    switch (state.kind) {
    case FormControlKind::Checkbox:
    case FormControlKind::Radio:
        state.checked = has_attribute(node, "checked");
        state.value = node.attribute("value").empty() ? "on" : node.attribute("value");
        break;
    case FormControlKind::Range:
        state.min = parse_int_attribute(node, "min", 0);
        state.max = parse_int_attribute(node, "max", 100);
        state.step = std::max(1, parse_int_attribute(node, "step", 1));
        if (state.max < state.min) {
            state.max = state.min;
        }
        state.value = node.attribute("value");
        if (state.value.empty()) {
            const std::int64_t span = static_cast<std::int64_t>(state.max) - state.min;
            state.value = std::to_string(static_cast<std::int64_t>(state.min) + span / 2);
        }
        break;
    case FormControlKind::TextArea:
        append_descendant_text(node, state.value);
        break;
    case FormControlKind::Select: {
        OptionList options;
        collect_options(node, options);
        state.selected_index = first_selected_option_index(options);
        if (state.selected_index < 0 && !options.empty()) {
            state.selected_index = 0;
        }
        const Node* option = option_at(options, state.selected_index);
        state.value = option != nullptr ? option_value(*option) : std::string{};
        break;
    }
    case FormControlKind::Text:
    case FormControlKind::Date:
    case FormControlKind::Time:
    case FormControlKind::Color:
    case FormControlKind::File:
        state.value = node.attribute("value");
        break;
    case FormControlKind::Button:
    case FormControlKind::None:
        break;
    }
    return state;
}

} // namespace

FormControlKind form_control_kind(const Node& node) {
    if (node.type != NodeType::Element) {
        return FormControlKind::None;
    }
    if (node.tag_name == "button") {
        return FormControlKind::Button;
    }
    if (node.tag_name == "textarea") {
        return FormControlKind::TextArea;
    }
    if (node.tag_name == "select") {
        return FormControlKind::Select;
    }
    if (node.tag_name != "input") {
        return FormControlKind::None;
    }
    const std::string_view type = node.attribute("type");
    if (ascii_equals_ignore_case(type, "checkbox")) {
        return FormControlKind::Checkbox;
    }
    if (ascii_equals_ignore_case(type, "radio")) {
        return FormControlKind::Radio;
    }
    if (ascii_equals_ignore_case(type, "range")) {
        return FormControlKind::Range;
    }
    if (ascii_equals_ignore_case(type, "date") ||
        ascii_equals_ignore_case(type, "datetime-local")) {
        return FormControlKind::Date;
    }
    if (ascii_equals_ignore_case(type, "time")) {
        return FormControlKind::Time;
    }
    if (ascii_equals_ignore_case(type, "color")) {
        return FormControlKind::Color;
    }
    if (ascii_equals_ignore_case(type, "file")) {
        return FormControlKind::File;
    }
    if (ascii_equals_ignore_case(type, "button") ||
        ascii_equals_ignore_case(type, "image") ||
        ascii_equals_ignore_case(type, "reset") ||
        ascii_equals_ignore_case(type, "submit")) {
        return FormControlKind::Button;
    }
    return FormControlKind::Text;
}

bool is_form_control(const Node& node) {
    return form_control_kind(node) != FormControlKind::None;
}

bool is_disabled_form_control(const Node& node) {
    return is_form_control(node) && has_attribute(node, "disabled");
}

bool is_text_entry_control(const Node& node) {
    const FormControlKind kind = form_control_kind(node);
    return kind == FormControlKind::Text || kind == FormControlKind::TextArea ||
        kind == FormControlKind::Date || kind == FormControlKind::Time || kind == FormControlKind::Color;
}

bool is_readonly_text_control(const Node& node) {
    return is_text_entry_control(node) && has_attribute(node, "readonly");
}

FormControlState& ensure_form_control_state(const Node& node) {
    if (!node.form_control_state) {
        node.form_control_state = std::make_unique<FormControlState>(make_initial_state(node));
    }
    return *node.form_control_state;
}

const FormControlState* form_control_state_if_created(const Node& node) {
    return node.form_control_state.get();
}

std::string form_control_display_text(const Node& node) {
    if (node.form_control_state && node.form_control_state->kind != FormControlKind::Select) {
        return node.form_control_state->value;
    }
    const FormControlKind kind = form_control_kind(node);
    if (kind == FormControlKind::Select) {
        int selected = 0;
        OptionList options;
        collect_options(node, options);
        if (node.form_control_state) {
            selected = node.form_control_state->selected_index;
        } else {
            selected = first_selected_option_index(options);
            if (selected < 0) {
                selected = 0;
            }
        }
        const Node* option = option_at(options, selected);
        return option != nullptr ? option_text(*option) : std::string{};
    }
    if (kind == FormControlKind::TextArea) {
        std::string text;
        append_descendant_text(node, text);
        return text;
    }
    return node.attribute("value");
}

bool append_text_to_control(Node& node, std::string_view text) {
    if (is_disabled_form_control(node) || is_readonly_text_control(node) ||
        !is_text_entry_control(node) || text.empty()) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    text = clamp_append_text(state.value, text, max_length_for(node));
    if (text.empty()) {
        return false;
    }
    state.value.append(text.data(), text.size());
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool backspace_control(Node& node) {
    if (is_disabled_form_control(node) || is_readonly_text_control(node) || !is_text_entry_control(node)) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.value.empty()) {
        return false;
    }
    remove_last_utf8_codepoint(state.value);
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool complete_text_control_from_datalist(Node& node) {
    if (is_disabled_form_control(node) || is_readonly_text_control(node) || !is_text_entry_control(node)) {
        return false;
    }
    const std::string& list_id = node.attribute("list");
    if (list_id.empty()) {
        return false;
    }
    const Node* root = root_of(node);
    const Node* datalist = find_element_by_id(*root, list_id);
    if (datalist == nullptr || datalist->tag_name != "datalist") {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    std::string completed;
    if (!first_datalist_option_value(*datalist, state.value, completed) || completed == state.value) {
        return false;
    }
    if (const int max_length = max_length_for(node); max_length >= 0) {
        completed = std::string(clamp_append_text(std::string(), completed, max_length));
        if (completed == state.value) {
            return false;
        }
    }
    state.value = std::move(completed);
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool activate_form_control(Node& node) {
    if (is_disabled_form_control(node)) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.kind == FormControlKind::Checkbox) {
        state.checked = !state.checked;
        state.dirty = true;
        mark_dirty(node, DomDirtyStyle | DomDirtyPaint);
        return true;
    }
    if (state.kind == FormControlKind::Radio) {
        if (state.checked) {
            return false;
        }
        state.checked = true;
        state.dirty = true;
        mark_dirty(node, DomDirtyStyle | DomDirtyPaint);
        return true;
    }
    if (state.kind == FormControlKind::Select) {
        OptionList options;
        collect_options(node, options);
        const int options_count = option_count(options);
        if (options_count <= 0) {
            return false;
        }
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        return set_select_popup_open(node, !state.select_popup_open);
#else
        return set_selected_index_with_options(node, (state.selected_index + 1) % options_count, options);
#endif
    }
    return false;
}

bool set_range_value_from_local_x(Node& node, int local_x, int width) {
    if (is_disabled_form_control(node)) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.kind != FormControlKind::Range || width <= 0 || state.max <= state.min) {
        return false;
    }
    const std::int64_t clamped_x = std::max(0, std::min(local_x, width));
    const std::int64_t range = static_cast<std::int64_t>(state.max) - state.min;
    const std::int64_t raw = static_cast<std::int64_t>(state.min) +
        (range * clamped_x + width / 2) / width;
    const std::int64_t stepped = static_cast<std::int64_t>(state.min) +
        ((raw - state.min + state.step / 2) / state.step) * state.step;
    const std::int64_t value = std::max<std::int64_t>(state.min, std::min<std::int64_t>(stepped, state.max));
    const std::string next = std::to_string(value);
    if (state.value == next) {
        return false;
    }
    state.value = next;
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

std::string form_control_value(const Node& node) {
    if (!is_form_control(node)) {
        return {};
    }
    if (form_control_kind(node) == FormControlKind::Select) {
        const int selected = form_control_selected_index(node);
        OptionList options;
        collect_options(node, options);
        const Node* option = option_at(options, selected);
        return option != nullptr ? option_value(*option) : std::string{};
    }
    return ensure_form_control_state(node).value;
}

bool set_form_control_value(Node& node, std::string value) {
    if (is_disabled_form_control(node) || !is_form_control(node)) {
        return false;
    }
    if (form_control_kind(node) == FormControlKind::Select) {
        OptionList options;
        collect_options(node, options);
        const int index = option_index_by_value(options, value);
        if (index >= 0) {
            return set_selected_index_with_options(node, index, options);
        }
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.value == value) {
        return false;
    }
    state.value = std::move(value);
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool form_control_checked(const Node& node) {
    const FormControlKind kind = form_control_kind(node);
    if (kind != FormControlKind::Checkbox && kind != FormControlKind::Radio) {
        return false;
    }
    return ensure_form_control_state(node).checked;
}

bool set_form_control_checked(Node& node, bool checked) {
    if (is_disabled_form_control(node)) {
        return false;
    }
    const FormControlKind kind = form_control_kind(node);
    if (kind != FormControlKind::Checkbox && kind != FormControlKind::Radio) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.checked == checked) {
        return false;
    }
    state.checked = checked;
    state.dirty = true;
    mark_dirty(node, DomDirtyStyle | DomDirtyPaint);
    return true;
}

int form_control_selected_index(const Node& node) {
    if (form_control_kind(node) != FormControlKind::Select) {
        return -1;
    }
    return ensure_form_control_state(node).selected_index;
}

bool set_form_control_selected_index(Node& node, int selected_index) {
    if (is_disabled_form_control(node) || form_control_kind(node) != FormControlKind::Select) {
        return false;
    }
    OptionList options;
    collect_options(node, options);
    return set_selected_index_with_options(node, selected_index, options);
}

bool step_select_control(Node& node, int delta) {
    if (is_disabled_form_control(node) || form_control_kind(node) != FormControlKind::Select || delta == 0) {
        return false;
    }
    OptionList options;
    collect_options(node, options);
    const int options_count = option_count(options);
    if (options_count <= 0) {
        return false;
    }
    const int current = std::max(0, form_control_selected_index(node));
    const int next = std::max(0, std::min(options_count - 1, current + delta));
    return set_selected_index_with_options(node, next, options);
}

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
bool select_popup_is_open(const Node& node) {
    return form_control_kind(node) == FormControlKind::Select &&
        node.form_control_state != nullptr && node.form_control_state->select_popup_open;
}

bool set_select_popup_open(Node& node, bool open) {
    if (is_disabled_form_control(node) || form_control_kind(node) != FormControlKind::Select) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.select_popup_open == open) {
        return false;
    }
    state.select_popup_open = open;
    // The popup is a transient layer. Its old and new bounds are merged by the
    // dirty-region pass; the DOM and layout trees remain reusable.
    mark_dirty(node, DomDirtyOverlay | DomDirtyPaint);
    return true;
}

void form_control_collect_options(const Node& node, std::vector<const Node*>& options) {
    if (form_control_kind(node) != FormControlKind::Select) {
        options.clear();
        return;
    }
    collect_options(node, options);
}

std::string form_control_option_text_from_node(const Node& option) {
    return option_text(option);
}

bool form_control_option_is_disabled_node(const Node& option) {
    if (has_attribute(option, "disabled")) {
        return true;
    }
    return option.parent != nullptr && option.parent->tag_name == "optgroup" &&
        has_attribute(*option.parent, "disabled");
}

int form_control_option_count(const Node& node) {
    std::vector<const Node*> options;
    form_control_collect_options(node, options);
    return option_count(options);
}

const Node* form_control_option_at(const Node& node, int option_index) {
    if (option_index < 0 || form_control_kind(node) != FormControlKind::Select) {
        return nullptr;
    }
    OptionList options;
    form_control_collect_options(node, options);
    return option_at(options, option_index);
}

std::string form_control_option_text(const Node& node, int option_index) {
    const Node* option = form_control_option_at(node, option_index);
    return option == nullptr ? std::string{} : option_text(*option);
}

bool form_control_option_disabled(const Node& node, int option_index) {
    const Node* option = form_control_option_at(node, option_index);
    return option != nullptr && form_control_option_is_disabled_node(*option);
}

SelectPopupGeometry select_popup_geometry(const Rect& select_rect,
                                         const Rect& viewport,
    int option_count,
    int row_height) {
    SelectPopupGeometry geometry;
    geometry.row_height = std::max(1, row_height);
    if (option_count <= 0 || viewport.width <= 0 || viewport.height <= 0) {
        return geometry;
    }
    const int available_rows = std::max(1, viewport.height / geometry.row_height);
    geometry.visible_option_count = std::min(option_count, available_rows);
    const int width = std::max(1, std::min(select_rect.width, viewport.width));
    const int height = geometry.visible_option_count * geometry.row_height;
    const int x = std::max(viewport.x, std::min(select_rect.x, safe_edge(viewport.x, viewport.width - width)));
    const int below = safe_edge(select_rect.y, select_rect.height);
    const int above = select_rect.y - height;
    int y = below;
    if (safe_edge(y, height) > safe_edge(viewport.y, viewport.height) && above >= viewport.y) {
        y = above;
    } else if (safe_edge(y, height) > safe_edge(viewport.y, viewport.height)) {
        y = std::max(viewport.y, safe_edge(viewport.y, viewport.height - height));
    }
    geometry.rect = Rect{x, y, width, height};
    return geometry;
}
#endif

} // namespace jellyframe
