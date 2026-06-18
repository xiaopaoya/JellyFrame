#include "core/form_control.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

bool has_attribute(const Node& node, const std::string& name) {
    return node.attributes.find(name) != node.attributes.end();
}

int parse_int_attribute(const Node& node, const std::string& name, int fallback) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || errno == ERANGE) {
        return fallback;
    }
    return static_cast<int>(parsed);
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

const Node* option_at(const Node& node, int wanted_index, int& current_index) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            if (current_index == wanted_index) {
                return current;
            }
            ++current_index;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return nullptr;
}

int count_options(const Node& node) {
    int count = 0;
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            ++count;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return count;
}

int first_selected_option_index(const Node& node, int& current_index) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            if (has_attribute(*current, "selected")) {
                return current_index;
            }
            ++current_index;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return -1;
}

std::string option_text(const Node& option) {
    std::string text;
    append_descendant_text(option, text);
    return text;
}

std::string option_value(const Node& option) {
    const std::string& value = option.attribute("value");
    return value.empty() ? option_text(option) : value;
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

int option_index_by_value(const Node& node, const std::string& value, int& current_index) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "option") {
            if (option_value(*current) == value) {
                return current_index;
            }
            ++current_index;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return -1;
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
            state.value = std::to_string(state.min + (state.max - state.min) / 2);
        }
        break;
    case FormControlKind::TextArea:
        append_descendant_text(node, state.value);
        break;
    case FormControlKind::Select: {
        int current_index = 0;
        state.selected_index = first_selected_option_index(node, current_index);
        if (state.selected_index < 0 && current_index > 0) {
            state.selected_index = 0;
        }
        int option_index = 0;
        const Node* option = option_at(node, state.selected_index, option_index);
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

int state_int_value(const FormControlState& state) {
    char* end = nullptr;
    const long parsed = std::strtol(state.value.c_str(), &end, 10);
    if (end == state.value.c_str()) {
        return state.min;
    }
    return static_cast<int>(parsed);
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
    const std::string& type = node.attribute("type");
    if (type == "checkbox") {
        return FormControlKind::Checkbox;
    }
    if (type == "radio") {
        return FormControlKind::Radio;
    }
    if (type == "range") {
        return FormControlKind::Range;
    }
    if (type == "date" || type == "datetime-local") {
        return FormControlKind::Date;
    }
    if (type == "time") {
        return FormControlKind::Time;
    }
    if (type == "color") {
        return FormControlKind::Color;
    }
    if (type == "file") {
        return FormControlKind::File;
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
        if (node.form_control_state) {
            selected = node.form_control_state->selected_index;
        } else {
            int current_index = 0;
            selected = first_selected_option_index(node, current_index);
            if (selected < 0) {
                selected = 0;
            }
        }
        int current_index = 0;
        const Node* option = option_at(node, selected, current_index);
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
    if (is_disabled_form_control(node) || !is_text_entry_control(node) || text.empty()) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    state.value.append(text.data(), text.size());
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool backspace_control(Node& node) {
    if (is_disabled_form_control(node) || !is_text_entry_control(node)) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.value.empty()) {
        return false;
    }
    state.value.pop_back();
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool complete_text_control_from_datalist(Node& node) {
    if (is_disabled_form_control(node) || !is_text_entry_control(node)) {
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
        mark_dirty(node, DomDirtyPaint);
        return true;
    }
    if (state.kind == FormControlKind::Radio) {
        if (state.checked) {
            return false;
        }
        state.checked = true;
        state.dirty = true;
        mark_dirty(node, DomDirtyPaint);
        return true;
    }
    if (state.kind == FormControlKind::Select) {
        const int option_count = count_options(node);
        if (option_count <= 0) {
            return false;
        }
        return set_form_control_selected_index(node, (state.selected_index + 1) % option_count);
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
    const int clamped_x = std::max(0, std::min(local_x, width));
    const int raw = state.min + ((state.max - state.min) * clamped_x + width / 2) / width;
    const int stepped = state.min + ((raw - state.min + state.step / 2) / state.step) * state.step;
    const int value = std::max(state.min, std::min(stepped, state.max));
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
        int current_index = 0;
        const Node* option = option_at(node, selected, current_index);
        return option != nullptr ? option_value(*option) : std::string{};
    }
    return ensure_form_control_state(node).value;
}

bool set_form_control_value(Node& node, std::string value) {
    if (is_disabled_form_control(node) || !is_form_control(node)) {
        return false;
    }
    if (form_control_kind(node) == FormControlKind::Select) {
        int current_index = 0;
        const int index = option_index_by_value(node, value, current_index);
        if (index >= 0) {
            return set_form_control_selected_index(node, index);
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
    mark_dirty(node, DomDirtyPaint);
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
    const int option_count = count_options(node);
    if (option_count <= 0) {
        selected_index = -1;
    } else {
        selected_index = std::max(0, std::min(selected_index, option_count - 1));
    }

    FormControlState& state = ensure_form_control_state(node);
    if (state.selected_index == selected_index) {
        return false;
    }
    state.selected_index = selected_index;
    int current_index = 0;
    const Node* option = option_at(node, selected_index, current_index);
    state.value = option != nullptr ? option_value(*option) : std::string{};
    state.dirty = true;
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool step_select_control(Node& node, int delta) {
    if (is_disabled_form_control(node) || form_control_kind(node) != FormControlKind::Select || delta == 0) {
        return false;
    }
    const int option_count = count_options(node);
    if (option_count <= 0) {
        return false;
    }
    const int current = std::max(0, form_control_selected_index(node));
    const int next = std::max(0, std::min(option_count - 1, current + delta));
    return set_form_control_selected_index(node, next);
}

} // namespace jellyframe
