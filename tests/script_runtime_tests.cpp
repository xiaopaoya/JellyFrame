#include "script/jerryscript_runtime.h"

#include "core/dom.h"
#include "core/html_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace wearweb;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Node* find_first_by_tag(Node& node, const std::string& tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void expression_returns_value() {
    JerryScriptRuntime runtime;
    const ScriptEvaluationResult result = runtime.eval("1 + 2", "expression.js");

    check(result.ok, "expression evaluates successfully");
    check(result.value == "3", "expression result is stringified");
}

void exception_returns_error_text() {
    JerryScriptRuntime runtime;
    const ScriptEvaluationResult result = runtime.eval("throw new Error('boom')", "exception.js");

    check(!result.ok, "exception is reported as failure");
    check(!result.error.empty(), "exception has error text");
}

void runtime_can_restart() {
    for (int i = 0; i < 3; ++i) {
        JerryScriptRuntime runtime;
        const ScriptEvaluationResult result = runtime.eval("'run-' + " + std::to_string(i));
        check(result.ok, "runtime restart eval succeeds");
    }
}

void document_get_element_by_id_updates_text_content() {
    HtmlParser parser;
    auto document = parser.parse("<body><h1 id='title'>Old</h1></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var title = document.getElementById('title');"
        "title.textContent = 'Changed';"
        "title.textContent");

    check(result.ok, "DOM textContent script succeeds");
    check(result.value == "Changed", "DOM textContent result");
    check(document->text_content().find("Changed") != std::string::npos, "DOM text updated");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "DOM mutation marks layout dirty");
}

void document_create_and_append_element() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'></main></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var p = document.createElement('p');"
        "p.setAttribute('class', 'note');"
        "p.appendChild(document.createTextNode('Hello from JS'));"
        "app.appendChild(p);"
        "p.getAttribute('class')");

    check(result.ok, "DOM append script succeeds");
    check(result.value == "note", "getAttribute returns set value");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph != nullptr, "created paragraph attached");
    check(paragraph->attribute("class") == "note", "created paragraph attribute");
    check(paragraph->text_content() == "Hello from JS", "created paragraph text");
}

void remove_child_keeps_wrapper_usable() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'><p id='note'>Keep me</p></main></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var note = document.getElementById('note');"
        "var removed = app.removeChild(note);"
        "removed.textContent = 'Detached';"
        "removed.textContent");

    check(result.ok, "removeChild script succeeds");
    check(result.value == "Detached", "removed wrapper remains usable");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph == nullptr, "removed paragraph detached from DOM");
}

void javascript_click_listener_mutates_dom() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>0</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "button.addEventListener('click', function (event) {"
        "  this.textContent = event.type + ':' + event.clientX + ',' + event.clientY;"
        "});"
        "'listener-ready'");

    check(result.ok, "event listener registration succeeds");

    MouseEvent event("click", 12, 34);
    dispatch_event(*button, event);

    check(button->text_content() == "click:12,34", "JS event listener mutates DOM");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "JS event mutation marks layout dirty");
}

void javascript_event_prevent_default_and_remove_listener_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>0</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "var count = 0;"
        "function onClick(event) {"
        "  count += 1;"
        "  event.preventDefault();"
        "  button.textContent = String(count);"
        "}"
        "button.addEventListener('click', onClick);"
        "button.removeEventListener('click', function () {});"
        "'ready'");

    check(result.ok, "preventDefault listener registration succeeds");

    MouseEvent first("click", 1, 1);
    check(!dispatch_event(*button, first), "JS preventDefault affects dispatch result");
    check(button->text_content() == "1", "listener ran once");

    const ScriptEvaluationResult removed = runtime.eval(
        "button.removeEventListener('click', onClick);"
        "'removed'");
    check(removed.ok, "removeEventListener succeeds");

    MouseEvent second("click", 2, 2);
    check(dispatch_event(*button, second), "removed listener no longer prevents default");
    check(button->text_content() == "1", "removed listener no longer mutates DOM");
}

} // namespace

int main() {
    try {
        expression_returns_value();
        exception_returns_error_text();
        runtime_can_restart();
        document_get_element_by_id_updates_text_content();
        document_create_and_append_element();
        remove_child_keeps_wrapper_usable();
        javascript_click_listener_mutates_dom();
        javascript_event_prevent_default_and_remove_listener_work();
    } catch (const std::exception& error) {
        std::cerr << "script runtime test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "script runtime tests passed\n";
    return 0;
}
