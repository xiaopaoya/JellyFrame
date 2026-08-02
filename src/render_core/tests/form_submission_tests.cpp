#include "render_core/form_control.h"
#include "render_core/feature_config.h"
#include "render_core/form_submission.h"
#include "render_core/html_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
void validation_and_form_data_follow_control_state() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form id='account'>"
        "<input id='name' name='name' required minlength='3'>"
        "<input id='agree' name='agree' type='checkbox' required value='yes'>"
        "<input id='red' name='color' type='radio' required value='red'>"
        "<input id='blue' name='color' type='radio' value='blue'>"
        "<select id='mode' name='mode' required><option value=''>Pick</option><option value='fast'>Fast</option></select>"
        "<textarea id='note' name='note' maxlength='4'>hello</textarea>"
        "<button id='send' name='action' value='save'>Save</button>"
        "</form></body>");

    Node* form = find_by_id(*document, "account");
    Node* name = find_by_id(*document, "name");
    Node* agree = find_by_id(*document, "agree");
    Node* blue = find_by_id(*document, "blue");
    Node* mode = find_by_id(*document, "mode");
    Node* note = find_by_id(*document, "note");
    Node* send = find_by_id(*document, "send");
    check(form != nullptr && name != nullptr && agree != nullptr && blue != nullptr && mode != nullptr &&
              note != nullptr && send != nullptr,
          "form fixture nodes exist");

    FormValidationResult validation = validate_form(*form);
    check(validation.issues.size() == 5, "required and length constraints are reported");

    int invalid_events = 0;
    for (const FormValidationIssue& issue : validation.issues) {
        issue.control->add_event_listener("invalid", [&](Event&) { ++invalid_events; });
    }
    FormSubmitResult rejected = request_form_submit(*form, send);
    check(!rejected.validation.valid() && !rejected.submitted, "invalid forms do not submit");
    check(invalid_events == 5, "each invalid control receives an invalid event");

    check(set_form_control_value(*name, "Ada"), "text value updates");
    check(set_form_control_checked(*agree, true), "checkbox value updates");
    check(set_form_control_checked(*blue, true), "radio value updates");
    check(set_form_control_selected_index(*mode, 1), "select value updates");
    check(set_form_control_value(*note, "note"), "textarea value updates");
    check(validate_form(*form).valid(), "updated controls pass validation");

    const std::vector<FormDataEntry> plain_data = collect_form_data(*form);
    check(plain_data.size() == 5, "successful controls exclude submitter");
    check(plain_data[0].name == "name" && plain_data[0].value == "Ada", "form data keeps DOM order for text");
    check(plain_data[1].name == "agree" && plain_data[1].value == "yes", "form data includes checked checkbox");
    check(plain_data[2].name == "color" && plain_data[2].value == "blue", "form data includes checked radio");
    check(plain_data[3].name == "mode" && plain_data[3].value == "fast", "form data includes selected option");
    check(plain_data[4].name == "note" && plain_data[4].value == "note", "form data includes textarea state");

    const std::vector<FormDataEntry> submit_data = collect_form_data(*form, send);
    check(submit_data.size() == 6 && submit_data.back().name == "action" && submit_data.back().value == "save",
          "submitter name/value is appended after successful controls");

    std::vector<FormDataEntry> limited_data;
    check(!collect_form_data_limited(*form, limited_data, FormDataCollectionLimits{1, 64}),
          "bounded FormData collection rejects excessive entries");
    check(limited_data.empty(), "rejected bounded FormData collection leaves no partial entries");
}

void request_submit_is_cancellable_and_exposes_submitter() {
    HtmlParser parser;
    auto document = parser.parse("<body><form id='form'><button id='send'>Send</button></form></body>");
    Node* form = find_by_id(*document, "form");
    Node* send = find_by_id(*document, "send");
    check(form != nullptr && send != nullptr, "submit event fixture exists");

    int submits = 0;
    form->add_event_listener("submit", [&](Event& event) {
        ++submits;
        const auto* submit = dynamic_cast<const SubmitEvent*>(&event);
        check(submit != nullptr && submit->submitter() == send, "submit event preserves submitter");
        event.prevent_default();
    });

    FormSubmitResult result = request_form_submit_from_control(*send);
    check(result.validation.valid(), "simple form is valid");
    check(result.data.empty(), "submit dispatch does not materialize unused FormData");
    check(submits == 1, "submit event dispatches once");
    check(!result.submitted && result.default_prevented, "preventDefault cancels default submit action");
}

void reset_restores_authored_control_defaults_and_is_cancellable() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form id='form'><input id='name' value='Ada'><input id='enabled' type='checkbox' checked>"
        "<select id='mode'><option value='day' selected>Day</option><option value='night'>Night</option></select>"
        "<textarea id='note'>Memo</textarea></form></body>");
    Node* form = find_by_id(*document, "form");
    Node* name = find_by_id(*document, "name");
    Node* enabled = find_by_id(*document, "enabled");
    Node* mode = find_by_id(*document, "mode");
    Node* note = find_by_id(*document, "note");
    check(form != nullptr && name != nullptr && enabled != nullptr && mode != nullptr && note != nullptr,
          "form reset fixture exists");

    check(set_form_control_value(*name, "Grace"), "reset fixture text changes");
    check(set_form_control_checked(*enabled, false), "reset fixture checkbox changes");
    check(set_form_control_selected_index(*mode, 1), "reset fixture selection changes");
    check(set_form_control_value(*note, "Draft"), "reset fixture textarea changes");

    const std::size_t blocker = form->add_event_listener("reset", [](Event& event) { event.prevent_default(); });
    check(!reset_form(*form), "preventDefault cancels form reset");
    check(form_control_value(*name) == "Grace" && !form_control_checked(*enabled) &&
              form_control_selected_index(*mode) == 1 && form_control_value(*note) == "Draft",
          "cancelled reset keeps current control state");
    form->remove_event_listener(blocker);

    check(reset_form(*form), "uncancelled form reset succeeds");
    check(form_control_value(*name) == "Ada" && form_control_checked(*enabled) &&
              form_control_selected_index(*mode) == 0 && form_control_value(*note) == "Memo",
          "reset restores defaults from attributes and textarea text");
}

void control_validation_subset_is_lazy_and_dispatches_invalid() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form><input id='name' required minlength='3'><input id='disabled' required disabled>"
        "<button id='send'>Send</button></form></body>");
    Node* name = find_by_id(*document, "name");
    Node* disabled = find_by_id(*document, "disabled");
    Node* send = find_by_id(*document, "send");
    check(name != nullptr && disabled != nullptr && send != nullptr, "control validation fixture exists");

    check(form_control_will_validate(*name), "enabled text input participates in validation");
    check(!form_control_will_validate(*disabled), "disabled input is barred from validation");
    check(!form_control_will_validate(*send), "submit button is barred from validation");

    FormControlValidationResult validation = validate_form_control(*name);
    check(validation.value_missing && !validation.valid(), "required empty text input reports valueMissing");
    check(form_control_validation_message(*name) == "Please fill out this field.", "required message is stable");
    int invalid_events = 0;
    name->add_event_listener("invalid", [&](Event&) { ++invalid_events; });
    check(!check_form_control_validity(*name) && invalid_events == 1, "control checkValidity dispatches invalid once");

    check(set_form_control_value(*name, "Al"), "short value applies");
    validation = validate_form_control(*name);
    check(validation.too_short && !validation.value_missing, "minlength reports tooShort");
    check(set_form_control_custom_validity(*name, "Choose a full name."), "custom message applies");
    validation = validate_form_control(*name);
    check(validation.custom_error && !validation.valid(), "custom error overrides intrinsic result");
    check(form_control_validation_message(*name) == "Choose a full name.", "custom message is exposed");
    check(set_form_control_custom_validity(*name, ""), "custom message clears");
    check(!validate_form_control(*name).custom_error, "cleared custom message restores intrinsic validation");
}

void input_type_tokens_are_ascii_case_insensitive() {
    HtmlParser parser;
    auto document = parser.parse("<body><form id='form'><input id='send' type='SUBMIT'></form></body>");
    Node* form = find_by_id(*document, "form");
    Node* send = find_by_id(*document, "send");
    check(form != nullptr && send != nullptr, "case-insensitive type fixture exists");
    check(form_control_kind(*send) == FormControlKind::Button, "uppercase submit is a button control");
    check(is_form_submitter(*send), "uppercase submit activates form submission");
    check(request_form_submit_from_control(*send).submitted, "uppercase submit follows submit default action");
}
#else
void advanced_form_apis_are_safe_no_ops_when_disabled() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form id='form'><input id='name' name='name' required><button id='send'>Send</button></form></body>");
    Node* form = find_by_id(*document, "form");
    Node* name = find_by_id(*document, "name");
    Node* send = find_by_id(*document, "send");
    check(form != nullptr && name != nullptr && send != nullptr, "forms-off fixture exists");

    check(!form_control_will_validate(*name), "forms-off controls do not expose validation");
    check(validate_form_control(*name).valid(), "forms-off control validation is valid");
    check(check_form_validity(*form), "forms-off form validation is valid");
    std::vector<FormDataEntry> entries{{"stale", "entry"}};
    check(!collect_form_data_limited(*form, entries, FormDataCollectionLimits{}),
          "forms-off FormData collection is unavailable");
    check(entries.empty(), "forms-off FormData collection clears output");
    check(!request_form_submit(*form, send).submitted, "forms-off requestSubmit has no default action");
    check(!reset_form(*form), "forms-off reset has no default action");
}
#endif

} // namespace

int main() {
    try {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        validation_and_form_data_follow_control_state();
        request_submit_is_cancellable_and_exposes_submitter();
        reset_restores_authored_control_defaults_and_is_cancellable();
        control_validation_subset_is_lazy_and_dispatches_invalid();
        input_type_tokens_are_ascii_case_insensitive();
#else
        advanced_form_apis_are_safe_no_ops_when_disabled();
#endif
    } catch (const std::exception& error) {
        std::cerr << "form submission test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "form submission tests passed\n";
    return 0;
}
