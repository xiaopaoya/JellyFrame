#include "script/jerryscript_runtime.h"

#include "app_runtime/app_services.h"
#include "render_core/document_script.h"
#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

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

void inline_document_script_mutates_dom() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><button id='count'>0</button>"
        "<script>"
        "var n = 0;"
        "document.getElementById('count').addEventListener('click', function () {"
        "  n += 1;"
        "  document.getElementById('count').textContent = String(n);"
        "});"
        "</script></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    const std::vector<DocumentScript> scripts = collect_classic_scripts(*document);
    check(scripts.size() == 1, "inline document script collected");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(scripts[0].source, scripts[0].name);
    check(result.ok, "inline document script evaluates");

    MouseEvent click("click", 1, 1);
    dispatch_event(*button, click);
    check(button->text_content() == "1", "inline script listener mutates DOM after click");
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
    check(runtime.detached_node_count() == 0, "attached JS nodes leave detached owner");
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
    check(runtime.detached_node_count() == 1, "removed child remains runtime-owned detached node");
    const ScriptRuntimeStatistics statistics = runtime.statistics();
    check(statistics.detached_nodes.root_count == 1, "detached statistics count removed root");
    check(statistics.detached_nodes.aggregate.node_count == 2, "detached statistics include subtree");
}

void javascript_detached_node_budget_is_bounded() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'></main></body>");

    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{64, 512, 1});
    runtime.bind_document(*document);
    const ScriptEvaluationResult create_result = runtime.eval(
        "var first = document.createElement('p');"
        "var secondOk = true;"
        "try { document.createElement('section'); } catch (e) { secondOk = false; }"
        "String(secondOk) + ':' + first.tagName");

    check(create_result.ok, "detached budget script succeeds");
    check(create_result.value == "false:p", "second detached node is rejected");
    check(runtime.detached_node_count() == 1, "detached node count stays bounded");

    const ScriptEvaluationResult attach_result = runtime.eval(
        "document.getElementById('app').appendChild(first);"
        "var second = document.createElement('section');"
        "second.tagName");
    check(attach_result.ok, "attaching releases detached budget");
    check(attach_result.value == "section", "new detached node can be created after attach");
    check(runtime.detached_node_count() == 1, "only second node remains detached");
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

void javascript_form_properties_mutate_control_state() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<input id='name' value='old'>"
        "<input id='ok' type='checkbox'>"
        "<select id='mode'><option value='a'>Alpha</option><option value='b'>Beta</option></select>"
        "</body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var name = document.getElementById('name');"
        "var ok = document.getElementById('ok');"
        "var mode = document.getElementById('mode');"
        "name.value = 'Ada';"
        "ok.checked = true;"
        "mode.selectedIndex = 1;"
        "name.value + ':' + ok.checked + ':' + mode.value + ':' + mode.selectedIndex");

    check(result.ok, "form property script succeeds");
    check(result.value == "Ada:true:b:1", "form properties stringify expected state");
}

void javascript_embedded_ui_helpers_support_event_delegation() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><main id='app'>"
        "<button id='plus' data-op='+'><span id='label'>+</span></button>"
        "<button data-op='-'>-</button>"
        "</main></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var label = document.getElementById('label');"
        "var button = label.closest('[data-op]');"
        "button.dataset.op + ':' + app.children.length + ':' + "
        "button.parentElement.matches('#app') + ':' + label.matches('span')");

    check(result.ok, "embedded UI helper script succeeds");
    check(result.value == "+:2:true:true", "dataset children parentElement matches closest work");
}

void javascript_element_style_hidden_and_disabled_properties_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='save'>Save</button><p id='panel'>Panel</p></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var save = document.getElementById('save');"
        "var panel = document.getElementById('panel');"
        "save.disabled = true;"
        "panel.hidden = true;"
        "panel.style.display = 'none';"
        "panel.style.backgroundColor = '#ffffff';"
        "save.disabled + ':' + panel.hidden + ':' + panel.getAttribute('style')");

    check(result.ok, "style hidden disabled script succeeds");
    check(result.value.find("true:true:") == 0, "boolean attributes reflect through properties");
    check(result.value.find("display: none") != std::string::npos, "style display write serialized");
    check(result.value.find("background-color: #ffffff") != std::string::npos, "style background write serialized");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "style/hidden/disabled mark layout dirty");
}

void javascript_input_event_reads_live_value() {
    HtmlParser parser;
    auto document = parser.parse("<body><input id='name'><p id='status'></p></body>");
    Node* input = find_first_by_tag(*document, "input");
    check(input != nullptr, "input exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var name = document.getElementById('name');"
        "var status = document.getElementById('status');"
        "name.addEventListener('input', function () { status.textContent = this.value; });"
        "'ready'");
    check(result.ok, "input listener registration succeeds");

    check(append_text_to_control(*input, "42"), "native text input updates control state");
    Event event("input", true, false);
    dispatch_event(*input, event);

    check(document->text_content().find("42") != std::string::npos, "JS input listener reads value");
}

void javascript_timeout_runs_when_host_pumps_time() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>wait</p></body>");

    JerryScriptRuntime runtime;
    runtime.set_host_time_ms(100);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var done = 0;"
        "setTimeout(function () { done = 1; status.textContent = 'done'; }, 50);"
        "'ready'");
    check(result.ok, "timeout registration succeeds");
    check(runtime.has_pending_timers(), "timeout is pending");
    check(runtime.next_timer_due_ms() == 150, "timeout due time is host-relative");

    check(runtime.pump_timers(149) == 0, "timeout does not run early");
    check(document->text_content().find("wait") != std::string::npos, "DOM unchanged before timeout");
    check(runtime.pump_timers(150) == 1, "timeout runs when due");
    check(!runtime.has_pending_timers(), "one-shot timeout is cleared after running");
    check(document->text_content().find("done") != std::string::npos, "timeout callback mutates DOM");
    check(runtime.eval("done").value == "1", "timeout callback updates JS state");
}

void javascript_clear_timeout_cancels_callback() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>safe</p></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var id = setTimeout(function () { status.textContent = 'bad'; }, 1);"
        "clearTimeout(id);"
        "'cancelled'");
    check(result.ok, "clearTimeout script succeeds");
    check(!runtime.has_pending_timers(), "cleared timeout is removed from pending timers");
    check(runtime.pump_timers(10) == 0, "cleared timeout does not run");
    check(document->text_content().find("safe") != std::string::npos, "cleared timeout leaves DOM unchanged");
}

void javascript_interval_repeats_and_can_clear_itself() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>0</p></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var count = 0;"
        "var id = setInterval(function () {"
        "  count += 1;"
        "  status.textContent = String(count);"
        "  if (count == 2) clearInterval(id);"
        "}, 10);"
        "'interval-ready'");
    check(result.ok, "setInterval script succeeds");

    check(runtime.pump_timers(10) == 1, "interval first tick runs");
    check(runtime.pump_timers(20) == 1, "interval second tick runs");
    check(runtime.pump_timers(30) == 0, "cleared interval no longer runs");
    check(!runtime.has_pending_timers(), "cleared interval is no longer pending");
    check(document->text_content().find("2") != std::string::npos, "interval callback updates DOM twice");
    check(runtime.eval("count").value == "2", "interval callback updates JS state twice");
}

std::size_t complete_network_and_dispatch(AppRuntimeHost& host,
                                          NetworkFetchMock& network,
                                          JerryScriptRuntime& runtime) {
    network.complete_next(host);
    std::vector<HostServiceCompletion> completions;
    const AppCompletionPumpResult pumped = host.pump_frame_completions(completions);
    (void) pumped;
    std::size_t handled = 0;
    for (const HostServiceCompletion& completion : completions) {
        if (runtime.handle_host_completion(completion)) {
            ++handled;
        }
    }
    return handled;
}

void javascript_xml_http_request_get_completes_from_host_service() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>wait</p></body>");

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.xhr", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    check(network.add_fixture(NetworkFetchFixture{"app://weather", 200, "application/json", "{\"temp\":21}"}),
          "XHR fixture added");

    JerryScriptRuntime runtime;
    runtime.bind_app_services(host, network);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var log = '';"
        "var xhr = new XMLHttpRequest();"
        "xhr.onreadystatechange = function () { if (xhr.readyState == 4) log += 'done:' + xhr.status + ';'; };"
        "xhr.onload = function (event) { status.textContent = event.type + ':' + xhr.responseText; };"
        "xhr.onloadend = function () { log += 'end'; };"
        "xhr.open('GET', 'app://weather', true);"
        "xhr.send();"
        "'sent'");
    check(result.ok, "XHR script evaluates");
    check(result.value == "sent", "XHR send script result");

    check(complete_network_and_dispatch(host, network, runtime) == 1, "XHR completion dispatched");
    check(document->text_content().find("load:{\"temp\":21}") != std::string::npos, "XHR load updates DOM");
    check(runtime.eval("log").value.find("done:200") != std::string::npos, "XHR readyState/status observable");
    check(runtime.eval("log").value.find("end") != std::string::npos, "XHR loadend observable");
}

void javascript_xml_http_request_error_callback_runs_on_missing_fixture() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>wait</p></body>");

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.xhr-error", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});

    JerryScriptRuntime runtime;
    runtime.bind_app_services(host, network);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var xhr = new XMLHttpRequest();"
        "xhr.onerror = function (event) { status.textContent = event.type + ':' + xhr.status; };"
        "xhr.open('GET', 'app://missing', true);"
        "xhr.send();"
        "'sent'");
    check(result.ok, "XHR error script evaluates");

    check(complete_network_and_dispatch(host, network, runtime) == 1, "XHR error completion dispatched");
    check(document->text_content().find("error:0") != std::string::npos, "XHR error updates DOM");
}

void javascript_xml_http_request_budget_is_bounded() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{64, 512, 256, 1});
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var bareOk = true;"
        "try { XMLHttpRequest(); } catch (e) { bareOk = false; }"
        "var first = new XMLHttpRequest();"
        "var secondOk = true;"
        "try { var second = new XMLHttpRequest(); } catch (e) { secondOk = false; }"
        "String(bareOk) + ':' + String(secondOk)");
    check(result.ok, "XHR budget script evaluates");
    check(result.value == "false:false", "XHR requires new and rejects second object");
    check(runtime.statistics().xml_http_request_count == 1, "XHR statistics count one live object");
}

void javascript_xml_http_request_constructor_is_shared_with_window() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval("String(XMLHttpRequest === window.XMLHttpRequest)");
    check(result.ok, "XHR constructor identity script evaluates");
    check(result.value == "true", "global and window share the XHR constructor");
}

void javascript_runtime_respects_timer_and_listener_budgets() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>Go</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{1, 1});
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "var fired = 0;"
        "var first = setTimeout(function () { fired += 1; }, 1);"
        "var second = setTimeout(function () { fired += 10; }, 1);"
        "button.addEventListener('click', function () { fired += 100; });"
        "button.addEventListener('click', function () { fired += 1000; });"
        "String(first > 0) + ':' + String(second)");
    check(result.ok, "budget script succeeds");
    check(result.value == "true:0", "timer budget rejects second timer");

    check(runtime.pump_timers(1) == 1, "only one timer callback runs");
    Event click("click", true, true);
    dispatch_event(*button, click);
    check(runtime.eval("String(fired)").value == "101", "listener budget keeps only first listener");
}

} // namespace

int main() {
    try {
        expression_returns_value();
        exception_returns_error_text();
        runtime_can_restart();
        inline_document_script_mutates_dom();
        document_get_element_by_id_updates_text_content();
        document_create_and_append_element();
        remove_child_keeps_wrapper_usable();
        javascript_detached_node_budget_is_bounded();
        javascript_click_listener_mutates_dom();
        javascript_event_prevent_default_and_remove_listener_work();
        javascript_form_properties_mutate_control_state();
        javascript_embedded_ui_helpers_support_event_delegation();
        javascript_element_style_hidden_and_disabled_properties_work();
        javascript_input_event_reads_live_value();
        javascript_timeout_runs_when_host_pumps_time();
        javascript_clear_timeout_cancels_callback();
        javascript_interval_repeats_and_can_clear_itself();
        javascript_xml_http_request_get_completes_from_host_service();
        javascript_xml_http_request_error_callback_runs_on_missing_fixture();
        javascript_xml_http_request_budget_is_bounded();
        javascript_xml_http_request_constructor_is_shared_with_window();
        javascript_runtime_respects_timer_and_listener_budgets();
    } catch (const std::exception& error) {
        std::cerr << "script runtime test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "script runtime tests passed\n";
    return 0;
}
