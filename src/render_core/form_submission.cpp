#include "render_core/form_submission.h"

#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/text_scan.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

bool has_attribute(const Node& node, std::string_view name) {
    return node.attributes.find(std::string(name)) != node.attributes.end();
}

bool is_descendant_of(const Node& node, const Node& ancestor) {
    for (const Node* current = &node; current != nullptr; current = current->parent) {
        if (current == &ancestor) {
            return true;
        }
    }
    return false;
}

std::size_t utf8_codepoint_count(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size(); ++count) {
        consume_utf8_codepoint(text, index);
    }
    return count;
}

int nonnegative_integer_attribute(const Node& node, std::string_view name) {
    const std::string& text = node.attribute(std::string(name));
    if (text.empty()) {
        return -1;
    }
    int value = -1;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && value >= 0 ? value : -1;
}

bool is_successful_control(const Node& node) {
    if (!is_form_control(node) || is_disabled_form_control(node) || node.attribute("name").empty()) {
        return false;
    }
    const FormControlKind kind = form_control_kind(node);
    if (kind == FormControlKind::Button || kind == FormControlKind::File) {
        return false;
    }
    return (kind != FormControlKind::Checkbox && kind != FormControlKind::Radio) ||
        form_control_checked(node);
}

bool radio_group_checked(const Node& form, const std::string& name) {
    std::vector<const Node*> pending;
    pending.push_back(&form);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current != &form && form_control_kind(*current) == FormControlKind::Radio &&
            current->attribute("name") == name && form_control_checked(*current)) {
            return true;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return false;
}

bool already_reported_radio_group(const std::vector<std::string>& groups, const std::string& name) {
    return std::find(groups.begin(), groups.end(), name) != groups.end();
}

bool ascii_equals_ignore_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace

SubmitEvent::SubmitEvent(const Node* submitter)
    : Event("submit", true, true),
      submitter_(submitter) {}

const Node* SubmitEvent::submitter() const {
    return submitter_;
}

Node* form_owner(Node& node) {
    for (Node* current = &node; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element && current->tag_name == "form") {
            return current;
        }
    }
    return nullptr;
}

const Node* form_owner(const Node& node) {
    return form_owner(const_cast<Node&>(node));
}

bool is_form_submitter(const Node& node) {
    if (node.type != NodeType::Element || is_disabled_form_control(node)) {
        return false;
    }
    if (node.tag_name == "button") {
        const std::string& type = node.attribute("type");
        return type.empty() || ascii_equals_ignore_case(type, "submit");
    }
    return node.tag_name == "input" &&
        (ascii_equals_ignore_case(node.attribute("type"), "submit") ||
         ascii_equals_ignore_case(node.attribute("type"), "image"));
}

bool is_form_resetter(const Node& node) {
    if (node.type != NodeType::Element || is_disabled_form_control(node)) {
        return false;
    }
    return (node.tag_name == "button" || node.tag_name == "input") &&
        ascii_equals_ignore_case(node.attribute("type"), "reset");
}

bool form_control_will_validate(const Node& node) {
    if (!is_form_control(node) || is_disabled_form_control(node)) {
        return false;
    }
    const FormControlKind kind = form_control_kind(node);
    return kind != FormControlKind::Button && kind != FormControlKind::File;
}

FormControlValidationResult validate_form_control(const Node& node) {
    FormControlValidationResult result;
    if (!form_control_will_validate(node)) {
        return result;
    }

    if (const FormControlState* state = form_control_state_if_created(node);
        state != nullptr && !state->custom_validation_message.empty()) {
        result.custom_error = true;
        return result;
    }

    const FormControlKind kind = form_control_kind(node);
    const std::string value = form_control_value(node);
    if (has_attribute(node, "required")) {
        if (kind == FormControlKind::Checkbox) {
            result.value_missing = !form_control_checked(node);
        } else if (kind == FormControlKind::Radio) {
            const Node* owner = form_owner(node);
            result.value_missing = owner != nullptr
                ? !radio_group_checked(*owner, node.attribute("name"))
                : !form_control_checked(node);
        } else {
            result.value_missing = value.empty();
        }
    }
    if (!result.value_missing && (kind == FormControlKind::Text || kind == FormControlKind::TextArea)) {
        const std::size_t length = utf8_codepoint_count(value);
        const int min_length = nonnegative_integer_attribute(node, "minlength");
        const int max_length = nonnegative_integer_attribute(node, "maxlength");
        result.too_short = min_length >= 0 && length < static_cast<std::size_t>(min_length);
        result.too_long = !result.too_short && max_length >= 0 && length > static_cast<std::size_t>(max_length);
    }
    return result;
}

bool check_form_control_validity(Node& node) {
    const FormControlValidationResult validation = validate_form_control(node);
    if (!validation.valid()) {
        Event invalid("invalid", false, true);
        dispatch_event(node, invalid);
    }
    return validation.valid();
}

std::string form_control_validation_message(const Node& node) {
    const FormControlValidationResult validation = validate_form_control(node);
    if (validation.custom_error) {
        const FormControlState* state = form_control_state_if_created(node);
        return state != nullptr ? state->custom_validation_message : std::string{};
    }
    if (validation.value_missing) {
        return "Please fill out this field.";
    }
    if (validation.too_short) {
        return "Value is too short.";
    }
    if (validation.too_long) {
        return "Value is too long.";
    }
    return {};
}

bool set_form_control_custom_validity(Node& node, std::string message) {
    if (!is_form_control(node)) {
        return false;
    }
    const FormControlState* existing = form_control_state_if_created(node);
    if (message.empty() && (existing == nullptr || existing->custom_validation_message.empty())) {
        return false;
    }
    FormControlState& state = ensure_form_control_state(node);
    if (state.custom_validation_message == message) {
        return false;
    }
    state.custom_validation_message = std::move(message);
    return true;
}

FormValidationResult validate_form(const Node& form) {
    FormValidationResult result;
    if (form.type != NodeType::Element || form.tag_name != "form") {
        return result;
    }

    std::vector<const Node*> pending;
    std::vector<std::string> reported_radio_groups;
    pending.push_back(&form);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current != &form && form_control_will_validate(*current)) {
            const FormControlValidationResult validation = validate_form_control(*current);
            const FormControlKind kind = form_control_kind(*current);
            if (validation.value_missing && kind == FormControlKind::Radio) {
                const std::string& name = current->attribute("name");
                if (!already_reported_radio_group(reported_radio_groups, name)) {
                    result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::ValueMissing});
                    reported_radio_groups.push_back(name);
                }
            } else if (validation.custom_error) {
                result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::CustomError});
            } else if (validation.value_missing) {
                result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::ValueMissing});
            } else if (validation.too_short) {
                result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::TooShort});
            } else if (validation.too_long) {
                result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::TooLong});
            }
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return result;
}

bool check_form_validity(Node& form) {
    const FormValidationResult validation = validate_form(form);
    for (const FormValidationIssue& issue : validation.issues) {
        if (issue.control != nullptr) {
            Event invalid("invalid", false, true);
            dispatch_event(*issue.control, invalid);
        }
    }
    return validation.valid();
}

bool collect_form_data_limited(const Node& form,
                               std::vector<FormDataEntry>& entries,
                               FormDataCollectionLimits limits,
                               const Node* submitter) {
    entries.clear();
    if (form.type != NodeType::Element || form.tag_name != "form") {
        return true;
    }

    std::size_t bytes = 0;
    const auto append = [&](std::string name, std::string value) {
        if (limits.max_entries != 0 && entries.size() >= limits.max_entries) {
            return false;
        }
        if (limits.max_bytes != 0) {
            if (bytes > limits.max_bytes || name.size() > limits.max_bytes - bytes) {
                return false;
            }
            const std::size_t after_name = bytes + name.size();
            if (value.size() > limits.max_bytes - after_name) {
                return false;
            }
            bytes = after_name + value.size();
        }
        entries.push_back(FormDataEntry{std::move(name), std::move(value)});
        return true;
    };

    std::vector<const Node*> pending;
    pending.push_back(&form);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current != &form && is_successful_control(*current)) {
            if (!append(current->attribute("name"), form_control_value(*current))) {
                entries.clear();
                return false;
            }
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    if (submitter != nullptr && is_descendant_of(*submitter, form) && is_form_submitter(*submitter) &&
        !submitter->attribute("name").empty()) {
        if (!append(submitter->attribute("name"), submitter->attribute("value"))) {
            entries.clear();
            return false;
        }
    }
    return true;
}

std::vector<FormDataEntry> collect_form_data(const Node& form, const Node* submitter) {
    std::vector<FormDataEntry> entries;
    collect_form_data_limited(form, entries, {}, submitter);
    return entries;
}

FormSubmitResult request_form_submit(Node& form, const Node* submitter) {
    FormSubmitResult result;
    if (form.type != NodeType::Element || form.tag_name != "form") {
        return result;
    }
    result.validation = validate_form(form);
    if (!result.validation.valid()) {
        for (const FormValidationIssue& issue : result.validation.issues) {
            if (issue.control != nullptr) {
                Event invalid("invalid", false, true);
                dispatch_event(*issue.control, invalid);
            }
        }
        return result;
    }

    result.data = collect_form_data(form, submitter);
    SubmitEvent event(submitter);
    result.submitted = dispatch_event(form, event);
    result.default_prevented = event.default_prevented();
    return result;
}

FormSubmitResult request_form_submit_from_control(Node& control) {
    Node* form = form_owner(control);
    return form != nullptr && is_form_submitter(control) ? request_form_submit(*form, &control) : FormSubmitResult{};
}

bool reset_form(Node& form) {
    if (form.type != NodeType::Element || form.tag_name != "form") {
        return false;
    }

    Event event("reset", true, true);
    if (!dispatch_event(form, event)) {
        return false;
    }

    std::vector<Node*> pending;
    pending.push_back(&form);
    while (!pending.empty()) {
        Node* current = pending.back();
        pending.pop_back();
        if (current != &form && is_form_control(*current)) {
            // Initial control state is derived lazily from DOM attributes/text.
            // Dropping an existing state restores that authored default without a snapshot.
            current->form_control_state.reset();
            mark_dirty(*current, DomDirtyStyle | DomDirtyPaint);
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return true;
}

bool reset_form_from_control(Node& control) {
    Node* form = form_owner(control);
    return form != nullptr && is_form_resetter(control) && reset_form(*form);
}

} // namespace jellyframe
