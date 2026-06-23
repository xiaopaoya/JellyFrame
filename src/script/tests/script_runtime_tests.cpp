#include "script/jerryscript_runtime.h"

#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "render_core/document_script.h"
#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"

#include <cstdint>
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
    check(result.status == ScriptEvaluationStatus::Ok, "expression status is ok");
    check(result.value == "3", "expression result is stringified");
}

void exception_returns_error_text() {
    JerryScriptRuntime runtime;
    const ScriptEvaluationResult result = runtime.eval("throw new Error('boom')", "exception.js");

    check(!result.ok, "exception is reported as failure");
    check(result.status == ScriptEvaluationStatus::Exception, "exception status is exception");
    check(!result.error.empty(), "exception has error text");
}

void execution_watchdog_allows_normal_scripts() {
    JerryScriptRuntimeOptions options;
    options.max_execution_check_count = 64;
    options.execution_check_interval = 1;
    JerryScriptRuntime runtime(options);
    const ScriptEvaluationResult result = runtime.eval("var total = 0; for (var i = 0; i < 8; ++i) total += i; total");

    check(result.ok, "watchdog allows bounded script");
    check(result.value == "28", "watchdog bounded script result");
}

void execution_watchdog_interrupts_infinite_eval_when_supported() {
    JerryScriptRuntimeOptions options;
    options.max_execution_check_count = 4;
    options.execution_check_interval = 1;
    JerryScriptRuntime runtime(options);
    if (!runtime.execution_watchdog_supported()) {
        return;
    }

    const ScriptEvaluationResult loop = runtime.eval("while (true) {}", "loop.js");
    check(!loop.ok, "watchdog interrupts infinite eval");
    check(loop.status == ScriptEvaluationStatus::ExecutionBudgetExceeded,
          "watchdog eval status is execution budget exceeded");
    check(loop.error.find("script execution budget exceeded") != std::string::npos,
          "watchdog reports stable budget error text");
    check(runtime.take_execution_watchdog_interrupt(), "watchdog eval sets sticky interrupt flag");

    const ScriptEvaluationResult after = runtime.eval("1 + 1");
    check(after.ok && after.value == "2", "runtime remains usable after watchdog interrupt");
    check(!runtime.take_execution_watchdog_interrupt(), "watchdog sticky flag is cleared after take");
}

void execution_watchdog_interrupts_timer_callback_when_supported() {
    JerryScriptRuntimeOptions options;
    options.max_execution_check_count = 64;
    options.execution_check_interval = 1;
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    JerryScriptRuntime runtime(options);
    runtime.bind_document(*document);
    if (!runtime.execution_watchdog_supported()) {
        return;
    }

    const ScriptEvaluationResult armed = runtime.eval(
        "var alive = 0;"
        "setTimeout(function () { while (true) {} }, 0);"
        "'armed'");
    check(armed.ok, "watchdog timer script arms");
    check(runtime.pump_timers(0) == 1, "watchdog timer callback returns after interrupt");
    check(runtime.take_execution_watchdog_interrupt(), "watchdog timer callback sets sticky interrupt flag");

    const ScriptEvaluationResult after = runtime.eval("alive = 7; alive");
    check(after.ok && after.value == "7", "runtime remains usable after interrupted timer callback");
}

void host_budgets_enable_script_execution_watchdog_when_supported() {
    HostBudgets budgets;
    budgets.max_script_execution_checks = 64;
    budgets.script_execution_check_interval = 1;
    JerryScriptRuntime runtime(budgets);
    if (!runtime.execution_watchdog_supported()) {
        return;
    }

    const ScriptEvaluationResult loop = runtime.eval("for (;;) {}", "budget-loop.js");
    check(!loop.ok, "HostBudgets script watchdog interrupts infinite eval");
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

void javascript_request_animation_frame_is_host_pumped() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>0</p></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var status = document.getElementById('status');"
        "var stamp = 0;"
        "var id = requestAnimationFrame(function (time) {"
        "  stamp = time;"
        "  status.textContent = 'frame';"
        "});"
        "String(id > 0)");
    check(result.ok && result.value == "true", "requestAnimationFrame registration succeeds");
    check(runtime.has_pending_animation_frames(), "animation frame callback is pending");
    check(runtime.statistics().animation_frame_callback_count == 1, "animation callback is counted");
    check(runtime.pump_timers(16) == 0, "timer pump does not run animation frame callbacks");
    check(runtime.pump_animation_frame(32, 4) == 1, "animation frame pump runs callback");
    check(!runtime.has_pending_animation_frames(), "one-shot animation frame callback is cleared");
    check(document->text_content().find("frame") != std::string::npos, "animation callback mutates DOM");
    check(runtime.eval("String(stamp)").value == "32", "animation callback receives host timestamp");
}

void javascript_cancel_animation_frame_cancels_callback() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var fired = 0;"
        "var id = requestAnimationFrame(function () { fired = 1; });"
        "cancelAnimationFrame(id);"
        "String(id > 0)");
    check(result.ok && result.value == "true", "cancelAnimationFrame setup succeeds");
    check(!runtime.has_pending_animation_frames(), "cancelled animation callback is removed");
    check(runtime.pump_animation_frame(16, 4) == 0, "cancelled animation callback does not run");
    check(runtime.eval("String(fired)").value == "0", "cancelled animation leaves JS state unchanged");
}

void javascript_animation_frame_budget_is_bounded() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{64, 512, 256, 16, 2});
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var fired = 0;"
        "var a = requestAnimationFrame(function () { fired += 1; });"
        "var b = requestAnimationFrame(function () { fired += 10; });"
        "var c = requestAnimationFrame(function () { fired += 100; });"
        "String(a > 0) + ':' + String(b > 0) + ':' + String(c)");
    check(result.ok, "animation budget script succeeds");
    check(result.value == "true:true:0", "animation callback budget rejects third callback");
    check(runtime.pump_animation_frame(16, 1) == 1, "animation frame pump respects per-frame callback cap");
    check(runtime.has_pending_animation_frames(), "remaining animation callback stays pending");
    check(runtime.eval("String(fired)").value == "1", "only first animation callback ran");
    check(runtime.pump_animation_frame(32, 1) == 1, "second animation callback runs on later frame");
    check(runtime.eval("String(fired)").value == "11", "second animation callback updates state");
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
    check(network.add_fixture(NetworkFetchFixture{"/data/weather.json", 200, "application/json", "{\"temp\":21}"}),
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
        "xhr.open('GET', '/data/weather.json', true);"
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
        "xhr.open('GET', '/data/missing.json', true);"
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

void javascript_local_storage_is_exposed_only_when_bound() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval("typeof localStorage");
    check(result.ok, "localStorage absence script evaluates");
    check(result.value == "undefined", "localStorage is absent when no non-blocking shadow is bound");
}

void javascript_local_storage_subset_uses_bound_shadow() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    AppLocalStorageShadow storage(AppPrivateKvPolicy{true, 16, 32, 4, 128});
    JerryScriptRuntime runtime;
    runtime.bind_local_storage(storage);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "localStorage.setItem('theme', 'dark');"
        "localStorage.setItem('mode', 42);"
        "var first = localStorage.key(0.9);"
        "var missing = localStorage.getItem('missing');"
        "var before = localStorage.length + ':' + first + ':' + localStorage.getItem('mode') + ':' + missing;"
        "localStorage.removeItem('mode');"
        "var afterRemove = localStorage.length + ':' + localStorage.getItem('mode');"
        "localStorage.clear();"
        "before + '|' + afterRemove + '|' + localStorage.length");
    check(result.ok, "localStorage subset script evaluates");
    check(result.value == "2:theme:42:null|1:null|0", "localStorage subset follows expected Web Storage shape");
    check(storage.length() == 0, "localStorage JS writes through to bound shadow");
}

void javascript_local_storage_quota_error_is_reported() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    AppLocalStorageShadow storage(AppPrivateKvPolicy{true, 4, 4, 1, 8});
    JerryScriptRuntime runtime;
    runtime.bind_local_storage(storage);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var quotaOk = true;"
        "try { localStorage.setItem('k', 'value-too-large'); } catch (e) { quotaOk = false; }"
        "String(quotaOk) + ':' + localStorage.length");
    check(result.ok, "localStorage quota script evaluates");
    check(result.value == "false:0", "localStorage quota rejects oversized value");
}

struct FakeAudioHost {
    std::uint32_t audio_id = 0;
    std::string src;
    double volume = -1.0;
    int calls = 0;
    bool fail = false;
};

bool fake_audio_play(void* user, std::uint32_t audio_id, std::string_view src, double volume, std::string*) {
    auto* host = static_cast<FakeAudioHost*>(user);
    if (host == nullptr) {
        return false;
    }
    if (host->fail) {
        return false;
    }
    host->audio_id = audio_id;
    host->src = std::string(src);
    host->volume = volume;
    ++host->calls;
    return true;
}

void javascript_audio_subset_uses_bound_host() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    {
        JerryScriptRuntime runtime;
        runtime.bind_document(*document);
        ScriptEvaluationResult result = runtime.eval(
            "var bareOk = true;"
            "var unboundOk = true;"
            "try { Audio('/audio/tone.wav'); } catch (e) { bareOk = false; }"
            "try { new Audio('/audio/tone.wav').play(); } catch (e) { unboundOk = false; }"
            "String(typeof Audio) + ':' + String(bareOk) + ':' + String(unboundOk)");
        check(result.ok, "Audio constructor absence/host test evaluates");
        check(result.value == "function:false:false", "Audio requires new and bound host for play");
    }

    {
        FakeAudioHost host;
        JerryScriptRuntime runtime_with_audio;
        runtime_with_audio.bind_audio_host(ScriptAudioHost{fake_audio_play, &host});
        runtime_with_audio.bind_document(*document);
        ScriptEvaluationResult result = runtime_with_audio.eval(
            "var tone = new Audio('/audio/tone.wav');"
            "tone.volume = 2;"
            "var high = tone.volume;"
            "tone.volume = -1;"
            "var low = tone.volume;"
            "tone.volume = 0.35;"
            "tone.play();"
            "tone.src + ':' + high + ':' + low + ':' + tone.volume");
        check(result.ok, "Audio host script evaluates");
        check(result.value == "/audio/tone.wav:1:0:0.35", "Audio src/volume subset follows expected shape");
        check(host.calls == 1, "Audio host was called once");
        check(host.src == "/audio/tone.wav", "Audio host receives src");
        check(host.volume > 0.34 && host.volume < 0.36, "Audio host receives clamped volume");
        check(runtime_with_audio.statistics().audio_element_count == 1, "Audio statistics count one element");

        result = runtime_with_audio.eval(
            "var eventLog = '';"
            "function removed() { eventLog += 'x'; }"
            "tone.onended = function (event) { eventLog += event.type + ':' + String(event.target === tone) + ';'; };"
            "tone.addEventListener('ended', function (event) { eventLog += event.type + ':' + String(this === tone) + ';'; });"
            "tone.addEventListener('error', removed);"
            "tone.removeEventListener('error', removed);"
            "'armed'");
        check(result.ok, "Audio event callbacks install");
        check(runtime_with_audio.dispatch_audio_event(host.audio_id, ScriptAudioEventKind::Ended),
              "Audio ended event dispatch reports handled");
        result = runtime_with_audio.eval("eventLog");
        check(result.ok && result.value == "ended:true;ended:true;",
              "Audio ended dispatches property and listener callbacks");
    }

    FakeAudioHost failing_host;
    failing_host.fail = true;
    JerryScriptRuntime runtime_with_error_audio;
    runtime_with_error_audio.bind_audio_host(ScriptAudioHost{fake_audio_play, &failing_host});
    runtime_with_error_audio.bind_document(*document);
    ScriptEvaluationResult result = runtime_with_error_audio.eval(
        "var failed = '';"
        "var missing = new Audio('/missing.wav');"
        "missing.onerror = function (event) { failed += event.type + ':' + String(event.currentTarget === missing); };"
        "try { missing.play(); } catch (error) { failed += ':thrown'; }"
        "failed");
    check(result.ok && result.value == "error:true:thrown", "Audio play rejection dispatches error before throwing");
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

void javascript_system_state_exposes_web_adjacent_subset() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>ready</p></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result =
        runtime.eval("String(document.hidden) + ':' + document.visibilityState + ':' + String(navigator.onLine)");
    check(result.ok, "system state initial script succeeds");
    check(result.value == "false:visible:false", "system state defaults are exposed");

    AppSystemStateSnapshot network_snapshot;
    network_snapshot.network_online = true;
    result = runtime.eval(
        "var networkEvents = '';"
        "function removedNetworkListener() { networkEvents += 'x'; }"
        "window.addEventListener('online', function (event) {"
        "  networkEvents += event.type + ':' + String(event.target === window) + ';';"
        "});"
        "addEventListener('offline', function (event) { networkEvents += event.type + ';'; }, { once: true });"
        "window.addEventListener('online', removedNetworkListener);"
        "window.removeEventListener('online', removedNetworkListener);"
        "'armed'");
    check(result.ok, "window network listeners install");
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::NetworkStatusChanged, network_snapshot}),
          "network system event handled");
    result = runtime.eval("String(navigator.onLine) + ':' + networkEvents");
    check(result.ok && result.value == "true:online:true;", "navigator.onLine and window online event update");

    network_snapshot.network_online = false;
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::NetworkStatusChanged, network_snapshot}),
          "offline system event handled");
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::NetworkStatusChanged, network_snapshot}),
          "unchanged offline event handled without redispatch");
    result = runtime.eval("String(navigator.onLine) + ':' + networkEvents");
    check(result.ok && result.value == "false:online:true;offline;",
          "window offline event fires once and only on state change");

    result = runtime.eval(
        "var visibilityEvents = 0;"
        "document.addEventListener('visibilitychange', function () {"
        "  visibilityEvents += document.hidden ? 1 : 10;"
        "});"
        "'armed'");
    check(result.ok, "visibility listener installs");

    AppSystemStateSnapshot hidden_snapshot;
    hidden_snapshot.screen_on = false;
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::ScreenStateChanged, hidden_snapshot}),
          "screen system event handled");
    result = runtime.eval("String(document.hidden) + ':' + document.visibilityState + ':' + String(visibilityEvents)");
    check(result.ok && result.value == "true:hidden:1", "document visibility state updates");
}

} // namespace

int main() {
    try {
        expression_returns_value();
        exception_returns_error_text();
        execution_watchdog_allows_normal_scripts();
        execution_watchdog_interrupts_infinite_eval_when_supported();
        execution_watchdog_interrupts_timer_callback_when_supported();
        host_budgets_enable_script_execution_watchdog_when_supported();
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
        javascript_request_animation_frame_is_host_pumped();
        javascript_cancel_animation_frame_cancels_callback();
        javascript_animation_frame_budget_is_bounded();
        javascript_xml_http_request_get_completes_from_host_service();
        javascript_xml_http_request_error_callback_runs_on_missing_fixture();
        javascript_xml_http_request_budget_is_bounded();
        javascript_xml_http_request_constructor_is_shared_with_window();
        javascript_local_storage_is_exposed_only_when_bound();
        javascript_local_storage_subset_uses_bound_shadow();
        javascript_local_storage_quota_error_is_reported();
        javascript_audio_subset_uses_bound_host();
        javascript_runtime_respects_timer_and_listener_budgets();
        javascript_system_state_exposes_web_adjacent_subset();
    } catch (const std::exception& error) {
        std::cerr << "script runtime test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "script runtime tests passed\n";
    return 0;
}
