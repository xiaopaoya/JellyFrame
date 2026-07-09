#pragma once

#include "render_core/event.h"

#include <string>
#include <vector>

namespace jellyframe {

struct Node;

enum class FormValidationFailure {
    ValueMissing,
    TooShort,
    TooLong,
};

struct FormValidationIssue {
    Node* control = nullptr;
    FormValidationFailure failure = FormValidationFailure::ValueMissing;
};

struct FormValidationResult {
    std::vector<FormValidationIssue> issues;

    bool valid() const { return issues.empty(); }
};

struct FormDataEntry {
    std::string name;
    std::string value;
};

class SubmitEvent final : public Event {
public:
    explicit SubmitEvent(const Node* submitter = nullptr);

    const Node* submitter() const;

private:
    const Node* submitter_ = nullptr;
};

struct FormSubmitResult {
    FormValidationResult validation;
    std::vector<FormDataEntry> data;
    bool submitted = false;
    bool default_prevented = false;
};

Node* form_owner(Node& node);
const Node* form_owner(const Node& node);
bool is_form_submitter(const Node& node);
FormValidationResult validate_form(const Node& form);
bool check_form_validity(Node& form);
std::vector<FormDataEntry> collect_form_data(const Node& form, const Node* submitter = nullptr);
FormSubmitResult request_form_submit(Node& form, const Node* submitter = nullptr);
FormSubmitResult request_form_submit_from_control(Node& control);

} // namespace jellyframe
