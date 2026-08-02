#include "render_core/form_submission.h"

#include "render_core/dom.h"

namespace jellyframe {

SubmitEvent::SubmitEvent(const Node*)
    : Event("submit", true, true) {}

const Node* SubmitEvent::submitter() const {
    return nullptr;
}

Node* form_owner(Node&) { return nullptr; }

const Node* form_owner(const Node&) { return nullptr; }

bool is_form_submitter(const Node&) { return false; }

bool is_form_resetter(const Node&) { return false; }

bool form_control_will_validate(const Node&) { return false; }

FormControlValidationResult validate_form_control(const Node&) { return {}; }

bool check_form_control_validity(Node&) { return true; }

std::string form_control_validation_message(const Node&) { return {}; }

bool set_form_control_custom_validity(Node&, std::string) { return false; }

FormValidationResult validate_form(const Node&) { return {}; }

bool check_form_validity(Node&) { return true; }

std::vector<FormDataEntry> collect_form_data(const Node&, const Node*) { return {}; }

bool collect_form_data_limited(const Node&,
                               std::vector<FormDataEntry>& entries,
                               FormDataCollectionLimits,
                               const Node*) {
    entries.clear();
    return false;
}

FormSubmitResult request_form_submit(Node&, const Node*) { return {}; }

FormSubmitResult request_form_submit_from_control(Node&) { return {}; }

bool reset_form(Node&) { return false; }

bool reset_form_from_control(Node&) { return false; }

} // namespace jellyframe
