#include "render_core/form_control.h"
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
    check(submits == 1, "submit event dispatches once");
    check(!result.submitted && result.default_prevented, "preventDefault cancels default submit action");
}

} // namespace

int main() {
    try {
        validation_and_form_data_follow_control_state();
        request_submit_is_cancellable_and_exposes_submitter();
    } catch (const std::exception& error) {
        std::cerr << "form submission test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "form submission tests passed\n";
    return 0;
}
