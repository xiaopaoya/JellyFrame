#include "render_core/form_submission.h"

#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/text_scan.h"

#include <algorithm>
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
    return kind != FormControlKind::Checkbox && kind != FormControlKind::Radio || form_control_checked(node);
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
        return type.empty() || type == "submit";
    }
    return node.tag_name == "input" && (node.attribute("type") == "submit" || node.attribute("type") == "image");
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
        if (current != &form && is_form_control(*current) && !is_disabled_form_control(*current)) {
            const FormControlKind kind = form_control_kind(*current);
            const std::string value = form_control_value(*current);
            if (has_attribute(*current, "required")) {
                bool missing = false;
                if (kind == FormControlKind::Checkbox) {
                    missing = !form_control_checked(*current);
                } else if (kind == FormControlKind::Radio) {
                    const std::string& name = current->attribute("name");
                    if (!already_reported_radio_group(reported_radio_groups, name)) {
                        missing = !radio_group_checked(form, name);
                        reported_radio_groups.push_back(name);
                    }
                } else if (kind != FormControlKind::Button && kind != FormControlKind::File) {
                    missing = value.empty();
                }
                if (missing) {
                    result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::ValueMissing});
                    continue;
                }
            }
            if (kind == FormControlKind::Text || kind == FormControlKind::TextArea) {
                const std::size_t length = utf8_codepoint_count(value);
                const int min_length = nonnegative_integer_attribute(*current, "minlength");
                const int max_length = nonnegative_integer_attribute(*current, "maxlength");
                if (min_length >= 0 && length < static_cast<std::size_t>(min_length)) {
                    result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::TooShort});
                } else if (max_length >= 0 && length > static_cast<std::size_t>(max_length)) {
                    result.issues.push_back(FormValidationIssue{const_cast<Node*>(current), FormValidationFailure::TooLong});
                }
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

std::vector<FormDataEntry> collect_form_data(const Node& form, const Node* submitter) {
    std::vector<FormDataEntry> entries;
    if (form.type != NodeType::Element || form.tag_name != "form") {
        return entries;
    }
    std::vector<const Node*> pending;
    pending.push_back(&form);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current != &form && is_successful_control(*current)) {
            entries.push_back(FormDataEntry{current->attribute("name"), form_control_value(*current)});
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    if (submitter != nullptr && is_descendant_of(*submitter, form) && is_form_submitter(*submitter) &&
        !submitter->attribute("name").empty()) {
        entries.push_back(FormDataEntry{submitter->attribute("name"), submitter->attribute("value")});
    }
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

} // namespace jellyframe
