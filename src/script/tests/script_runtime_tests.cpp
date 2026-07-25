#include "script/jerryscript_runtime.h"

#include "app_runtime/app_device_services.h"
#include "app_runtime/app_host_data.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "render_core/canvas2d.h"
#include "render_core/document_script.h"
#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"

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

void host_budgets_map_to_script_runtime_options_without_field_drift() {
    HostBudgets budgets;
    budgets.max_timers = 3;
    budgets.max_event_listeners = 5;
    budgets.max_detached_dom_nodes = 7;
    budgets.max_active_animations = 11;
    budgets.max_script_execution_checks = 13;
    budgets.script_execution_check_interval = 17;

    const JerryScriptRuntimeOptions options = jerryscript_runtime_options_from_host_budgets(budgets);
    check(options.max_timers == 3, "HostBudgets maps timer cap exactly");
    check(options.max_event_listeners == 5, "HostBudgets maps listener cap exactly");
    check(options.max_detached_nodes == 7, "HostBudgets maps detached-node cap exactly");
    check(options.max_animation_frame_callbacks == 11, "HostBudgets maps animation cap exactly");
    check(options.max_execution_check_count == 13, "HostBudgets maps watchdog check cap exactly");
    check(options.execution_check_interval == 17, "HostBudgets maps watchdog interval exactly");
    check(options.max_route_history_entries == 16, "unrelated route-history cap keeps its default");

    budgets.max_script_execution_checks = 0;
    budgets.script_execution_check_interval = 0;
    const JerryScriptRuntimeOptions zero_options = jerryscript_runtime_options_from_host_budgets(budgets);
    check(zero_options.max_execution_check_count == 0 && zero_options.execution_check_interval == 0,
          "zero watchdog budgets remain disabled instead of shifting into another option");
}

void runtime_can_restart() {
    for (int i = 0; i < 3; ++i) {
        JerryScriptRuntime runtime;
        const ScriptEvaluationResult result = runtime.eval("'run-' + " + std::to_string(i));
        check(result.ok, "runtime restart eval succeeds");
    }
}

void base64_helpers_follow_html_binary_string_subset() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "btoa('Jelly') + ':' + atob('SmVsbHk=') + ':' + "
        "atob(' U20 ') + ':' + window.btoa('\\u00ff') + ':' + "
        "atob('AA==').charCodeAt(0) + ':' + atob('/w==').charCodeAt(0)");

    check(result.ok, "base64 helpers evaluate");
    check(result.value == "SmVsbHk=:Jelly:Sm:/w==:0:255", "base64 helper values match expected subset");

    result = runtime.eval(
        "var bad = 0;"
        "try { btoa('\\u0100'); } catch (e) { bad += 1; }"
        "try { atob('%%%'); } catch (e) { bad += 1; }"
        "try { btoa(); } catch (e) { bad += 1; }"
        "try { atob(); } catch (e) { bad += 1; }"
        "bad");
    check(result.ok && result.value == "4", "base64 helpers reject invalid input");
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

void javascript_append_and_prepend_mix_text_and_nodes() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'><i>tail</i></main></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);

    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var first = document.createElement('b'); first.textContent = 'A';"
        "var second = document.createElement('em'); second.textContent = 'B';"
        "app.append('x', first, 9);"
        "app.prepend(second, 'y');"
        "app.textContent + ':' + app.children.length + ':' + "
        "app.children[0].tagName + ':' + app.children[1].tagName");

    check(result.ok, "append and prepend subset script succeeds");
    check(result.value == "BytailxA9:3:em:i",
          "append and prepend preserve argument order while mixing text and nodes");
    check(runtime.detached_node_count() == 0,
          "append and prepend attach created nodes without retaining detached roots");
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

void javascript_listener_on_destroyed_subtree_is_invalidated_before_runtime_cleanup() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'><button id='gone'>Tap</button></main></body>");
    auto second_document = parser.parse("<body><p>Next</p></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var gone = document.getElementById('gone');"
        "gone.addEventListener('click', function () { app.textContent = 'bad'; });"
        "app.textContent = 'replacement';"
        "'done'");
    check(result.ok, "listener subtree destruction script succeeds");

    runtime.bind_document(*second_document);
    check(runtime.statistics().event_listener_count == 0, "destroyed node listener is cleared on rebind");
}

void javascript_cached_destroyed_node_wrappers_are_invalidated() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><main id='app'>"
        "<button id='gone' class='old' style='display:block'>Tap</button>"
        "<canvas id='chart' width='8' height='8'></canvas>"
        "</main></body>");

    Canvas2DRegistry canvas;
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(canvas);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.getElementById('app');"
        "var gone = document.getElementById('gone');"
        "var oldStyle = gone.style;"
        "var oldClasses = gone.classList;"
        "var ctx = document.getElementById('chart').getContext('2d');"
        "app.textContent = 'replacement';"
        "var textResult = 'ok';"
        "try { gone.textContent; } catch (e) { textResult = 'invalid'; }"
        "var attrResult = 'ok';"
        "try { gone.setAttribute('data-x', '1'); } catch (e) { attrResult = 'invalid'; }"
        "oldStyle.display = 'none';"
        "oldClasses.add('new');"
        "ctx.fillRect(0, 0, 2, 2);"
        "textResult + ':' + attrResult + ':' + String(gone.parentElement === null)");

    check(result.ok, "destroyed cached wrappers script succeeds");
    check(result.value == "invalid:invalid:true", "destroyed node wrappers are inert");
    check(find_first_by_tag(*document, "button") == nullptr, "destroyed button is gone");
    check(find_first_by_tag(*document, "canvas") == nullptr, "destroyed canvas is gone");
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

void javascript_generic_click_event_does_not_fake_mouse_coordinates() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>0</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "button.addEventListener('click', function (event) {"
        "  button.textContent = event.type + ':' + String(event.clientX);"
        "});"
        "'listener-ready'");

    check(result.ok, "generic click listener registration succeeds");
    Event click("click");
    dispatch_event(*button, click);
    check(button->text_content() == "click:undefined", "generic click remains a base Event in JS");
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

void javascript_event_object_survives_after_dispatch_as_snapshot() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>0</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "var saved = null;"
        "button.addEventListener('click', function (event) {"
        "  saved = event;"
        "  event.preventDefault();"
        "});"
        "'ready'");
    check(result.ok, "event snapshot listener registration succeeds");

    Event click("click", true, true);
    check(!dispatch_event(*button, click), "live event preventDefault still affects dispatch");

    const ScriptEvaluationResult escaped = runtime.eval(
        "saved.stopPropagation();"
        "saved.stopImmediatePropagation();"
        "saved.preventDefault();"
        "saved.type + ':' + String(saved.defaultPrevented)");
    check(escaped.ok, "escaped event methods remain safe after dispatch");
    check(escaped.value == "click:true", "escaped event keeps snapshot fields");
}

void javascript_event_handler_properties_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>0</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "button exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "var log = '';"
        "function first() { log += 'first;'; }"
        "function second(event) { this.textContent = event.type; log += this.textContent + ';'; }"
        "button.onclick = first;"
        "var firstGetter = button.onclick === first;"
        "button.onclick = second;"
        "var secondGetter = button.onclick === second;"
        "String(firstGetter) + ':' + String(secondGetter)");
    check(result.ok && result.value == "true:true", "event handler property getter reflects active callback");
    check(runtime.statistics().event_listener_count == 1, "property handler replaces old handler");

    Event click("click", true, true);
    dispatch_event(*button, click);
    result = runtime.eval("log");
    check(result.ok && result.value == "click;", "event handler property runs with element this value");
    check(button->text_content() == "click", "event handler this value mutates target node");

    result = runtime.eval("button.onclick = null; String(button.onclick)");
    check(result.ok && result.value == "null", "event handler property can be cleared");
    check(runtime.statistics().event_listener_count == 0, "clearing handler releases listener slot");

    dispatch_event(*button, click);
    result = runtime.eval("log");
    check(result.ok && result.value == "click;", "cleared event handler no longer runs");
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

void javascript_dom_attribute_and_remove_ergonomics_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><section id='panel'><span id='item'>One</span></section></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var panel = document.getElementById('panel');"
        "var item = document.getElementById('item');"
        "var first = panel.toggleAttribute('hidden');"
        "var had = panel.hasAttribute('hidden');"
        "var second = panel.toggleAttribute('hidden', false);"
        "item.remove();"
        "String(first) + ':' + had + ':' + second + ':' + String(panel.hasAttribute('hidden')) + ':' + "
        "String(panel.children.length) + ':' + String(item.parentElement === null);");
    check(result.ok && result.value == "true:true:false:false:0:true",
          "hasAttribute, toggleAttribute and Node.remove work through JavaScript");
}

void javascript_tabindex_and_autofocus_reflection_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><div id='tile' tabindex='0' autofocus>Tile</div></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var tile = document.getElementById('tile');"
        "var before = tile.tabIndex + ':' + tile.autofocus;"
        "tile.tabIndex = -1; tile.autofocus = false;"
        "before + ':' + tile.getAttribute('tabindex') + ':' + tile.hasAttribute('autofocus');");
    check(result.ok && result.value == "0:true:-1:false",
          "tabIndex and autofocus reflect through JavaScript");
}

void javascript_form_idl_reflection_subset_works() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<input id='name' type='text' name='user' value='Ada' placeholder='Name'>"
        "<input id='contact' type='EMAIL' value='ada@example.test'>"
        "<button id='go' type='button' name='action' value='save'>Go</button>"
        "<select id='mode' name='mode' size='1'><option value='a'>Alpha</option>"
        "<optgroup id='group' label='More'><option id='beta'>Beta</option></optgroup></select>"
        "<textarea id='note' name='note' rows='3' cols='12' wrap='soft'>Hi</textarea>"
        "<progress id='load' value='25' max='100'></progress>"
        "<meter id='battery' min='0' max='10' value='7' low='2' high='8' optimum='9'></meter>"
        "</body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var name = document.getElementById('name');"
        "var contact = document.getElementById('contact');"
        "var go = document.getElementById('go');"
        "var mode = document.getElementById('mode');"
        "var beta = document.getElementById('beta');"
        "var group = document.getElementById('group');"
        "var note = document.getElementById('note');"
        "var load = document.getElementById('load');"
        "var battery = document.getElementById('battery');"
        "name.required = true;"
        "name.defaultValue = 'Grace';"
        "name.defaultChecked = true;"
        "name.checked = false;"
        "note.defaultValue = 'Memo';"
        "note.rows = 4;"
        "note.cols = 16;"
        "note.placeholder = 'Memo';"
        "mode.required = true;"
        "mode.size = 2;"
        "beta.defaultSelected = true;"
        "beta.value = 'b';"
        "beta.text = 'Beta Prime';"
        "group.label = 'Extra';"
        "load.value = 50;"
        "battery.value = 6;"
        "battery.low = '3';"
        "name.type + ':' + contact.type + ':' + contact.value + ':' + name.name + ':' + name.required + ':' + name.defaultValue + ':' + "
        "name.defaultChecked + ':' + name.checked + ':' + "
        "go.type + ':' + go.name + ':' + go.value + ':' + "
        "mode.type + ':' + mode.name + ':' + mode.required + ':' + mode.size + ':' + "
        "beta.index + ':' + beta.label + ':' + beta.defaultSelected + ':' + beta.value + ':' + "
        "beta.text + ':' + group.label + ':' + "
        "note.type + ':' + note.rows + ':' + note.cols + ':' + note.wrap + ':' + note.placeholder + ':' + "
        "note.defaultValue + ':' + note.textLength + ':' + "
        "load.value + ':' + load.max + ':' + load.position + ':' + "
        "battery.value + ':' + battery.min + ':' + battery.max + ':' + battery.low + ':' + "
        "battery.high + ':' + battery.optimum + ':' + typeof battery.low");

    check(result.ok, "form IDL reflection script succeeds");
    check(result.value == "text:email:ada@example.test:user:true:Grace:true:false:button:action:save:select-one:mode:true:2:"
                          "1:Beta Prime:true:b:Beta Prime:Extra:"
                          "textarea:4:16:soft:Memo:Memo:4:50:100:0.5:6:0:10:3:8:9:number",
          "form/progress/meter IDL subset reflects expected values");
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

void javascript_query_selector_subset_works() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><main id='app'><button class='key primary' data-op='+'><span>Plus</span></button>"
        "<button class='key' data-op='-'>Minus</button><p class='note'>Done</p></main></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var app = document.querySelector('#app');"
        "var firstKey = document.querySelector('.key');"
        "var scoped = app.querySelector('[data-op=\"-\"]');"
        "var keys = document.querySelectorAll('button.key');"
        "var notes = app.querySelectorAll('.note');"
        "var complex = document.querySelector('main > button');"
        "app.tagName + ':' + firstKey.dataset.op + ':' + scoped.dataset.op + ':' + "
        "String(keys.length) + ':' + notes[0].textContent + ':' + String(complex === null)");

    check(result.ok, "querySelector subset script succeeds");
    check(result.value == "main:+:-:2:Done:true",
          "querySelector/querySelectorAll simple selector subset works");
}

void javascript_class_name_reflects_class_attribute() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='save' class='idle'>Save</button></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var save = document.getElementById('save');"
        "var before = save.className;"
        "save.className = 'active primary';"
        "before + ':' + save.getAttribute('class') + ':' + save.className");

    check(result.ok, "className reflection script succeeds");
    check(result.value == "idle:active primary:active primary", "className reflects class attribute");
    check((subtree_dirty_flags(*document) & DomDirtyStyle) != 0U, "className marks style dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "className marks layout dirty");
}

void javascript_id_and_document_body_reflect_dom_attributes() {
    HtmlParser parser;
    auto document = parser.parse("<body><main id='app'>Ready</main></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var body = document.body;"
        "var app = body.children[0];"
        "var before = app.id;"
        "app.id = 'launcher';"
        "before + ':' + body.tagName + ':' + document.getElementById('launcher').textContent");

    check(result.ok, "id/body reflection script succeeds");
    check(result.value == "app:body:Ready", "element.id and document.body reflect DOM state");
    check((subtree_dirty_flags(*document) & DomDirtyStyle) != 0U, "id setter marks style dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "id setter marks layout dirty");
}

void javascript_class_list_subset_mutates_class_attribute() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='save' class='idle primary'>Save</button></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var save = document.getElementById('save');"
        "var before = save.classList.contains('idle');"
        "save.classList.add('active', 'primary');"
        "var afterAdd = save.className;"
        "var forced = save.classList.toggle('pressed', true);"
        "var removed = save.classList.toggle('idle', false);"
        "save.classList.remove('primary', 'missing');"
        "var firstReplace = save.classList.replace('active', 'ready');"
        "var mergedReplace = save.classList.replace('ready', 'pressed');"
        "var missingReplace = save.classList.replace('missing', 'later');"
        "var invalidReplace = save.classList.replace('bad token', 'later');"
        "before + ':' + afterAdd + ':' + forced + ':' + removed + ':' + firstReplace + ':' + "
        "mergedReplace + ':' + missingReplace + ':' + invalidReplace + ':' + save.className");

    check(result.ok, "classList subset script succeeds");
    check(result.value == "true:idle primary active:true:false:true:true:false:false:pressed",
          "classList contains/add/remove/toggle/replace update class attribute");
    check((subtree_dirty_flags(*document) & DomDirtyStyle) != 0U, "classList marks style dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "classList marks layout dirty");
}

void javascript_bounding_client_rect_uses_numeric_frame_snapshots() {
    HtmlParser parser;
    auto document = parser.parse("<body><section id='card'>Card</section></body>");
    Node* card = find_by_id(*document, "card");
    check(card != nullptr, "bounding client rect fixture exists");

    LayoutBox root;
    root.node = document.get();
    root.rect = Rect{0, 0, 172, 320};
    LayoutBoxPtr card_box(new LayoutBox());
    card_box->node = card;
    card_box->rect = Rect{8, 32, 120, 48};
    root.children.push_back(std::move(card_box));

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var card = document.getElementById('card');"
        "var before = card.getBoundingClientRect();"
        "before.x + ':' + before.width");
    check(result.ok && result.value == "0:0", "rect request starts without a completed frame snapshot");

    runtime.capture_layout_snapshot(root, 0, -4);
    result = runtime.eval(
        "var rect = card.getBoundingClientRect();"
        "rect.x + ':' + rect.y + ':' + rect.width + ':' + rect.height + ':' + rect.top + ':' + "
        "rect.right + ':' + rect.bottom + ':' + rect.left");
    check(result.ok && result.value == "8:28:120:48:28:128:76:8",
          "rect snapshot returns numeric client-relative DOMRect fields");

    result = runtime.eval(
        "document.body.textContent = 'replacement';"
        "var destroyed = 'live';"
        "try { card.getBoundingClientRect(); } catch (error) { destroyed = 'invalid'; }"
        "destroyed");
    check(result.ok && result.value == "invalid", "destroyed element cannot read a stale layout snapshot");
}

void javascript_dataset_writes_reflect_bounded_data_attributes() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='save' data-mode='idle'>Save</button></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var save = document.getElementById('save');"
        "var data = save.dataset;"
        "data.statusText = 'ready';"
        "var reflected = save.getAttribute('data-status-text');"
        "var live = save.dataset.statusText;"
        "data['bad-key'] = 'ignored';"
        "var rejected = save.getAttribute('data-bad-key') === null;"
        "delete data.statusText;"
        "data.mode + ':' + reflected + ':' + live + ':' + rejected + ':' + "
        "String(save.getAttribute('data-status-text') === null)");

    check(result.ok, "dataset write subset script succeeds");
    check(result.value == "idle:ready:ready:true:true",
          "dataset writes use camelCase data attributes and remain live for reads");
    check((subtree_dirty_flags(*document) & DomDirtyStyle) != 0U, "dataset writes mark style dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "dataset writes mark layout dirty");
}

void javascript_bounding_client_rect_snapshot_budget_is_bounded() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='one'>One</p><p id='two'>Two</p></body>");
    Node* one = find_by_id(*document, "one");
    Node* two = find_by_id(*document, "two");
    check(one != nullptr && two != nullptr, "rect budget fixture exists");

    LayoutBox root;
    root.node = document.get();
    LayoutBoxPtr one_box(new LayoutBox());
    one_box->node = one;
    one_box->rect = Rect{1, 2, 3, 4};
    LayoutBoxPtr two_box(new LayoutBox());
    two_box->node = two;
    two_box->rect = Rect{5, 6, 7, 8};
    root.children.push_back(std::move(one_box));
    root.children.push_back(std::move(two_box));

    JerryScriptRuntimeOptions options;
    options.max_layout_snapshot_nodes = 1;
    JerryScriptRuntime runtime(options);
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var one = document.getElementById('one');"
        "var two = document.getElementById('two');"
        "one.getBoundingClientRect(); two.getBoundingClientRect();"
        "'requested';");
    check(result.ok && result.value == "requested", "rect budget requests are accepted without allocation failure");

    runtime.capture_layout_snapshot(root);
    result = runtime.eval(
        "one.getBoundingClientRect().width + ':' + two.getBoundingClientRect().width");
    check(result.ok && result.value == "3:0", "rect snapshot cap refuses measurements beyond the configured budget");
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

void javascript_standard_reflected_attributes_work() {
    HtmlParser parser;
    auto document = parser.parse(
        "<html dir='ltr'><head><title>Old</title></head><body>"
        "<details id='panel'><summary>More</summary></details>"
        "<input id='name' maxlength='5' min='1' max='9' step='2'></body></html>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var panel = document.getElementById('panel');"
        "var name = document.getElementById('name');"
        "panel.title = 'Tip';"
        "panel.lang = 'zh-CN';"
        "panel.dir = 'rtl';"
        "panel.open = true;"
        "name.readOnly = true;"
        "name.maxLength = 3;"
        "name.min = '2';"
        "name.max = '8';"
        "name.step = '3';"
        "var beforeTitle = document.title;"
        "document.title = 'New';"
        "document.dir = 'rtl';"
        "beforeTitle + ':' + document.title + ':' + document.dir + ':' + "
        "panel.title + ':' + panel.lang + ':' + panel.dir + ':' + panel.open + ':' + "
        "name.readOnly + ':' + name.maxLength + ':' + name.min + ':' + name.max + ':' + name.step");

    check(result.ok, "reflected attribute script succeeds");
    check(result.value == "Old:New:rtl:Tip:zh-CN:rtl:true:true:3:2:8:3",
          "standard reflected attributes round-trip through JS");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U,
          "reflected attributes mark layout/style dirty");
}

void javascript_document_ready_state_and_element_click_work() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body>"
        "<button id='button'>0</button>"
        "<input id='check' type='checkbox'>"
        "<details id='panel'><summary id='summary'>More</summary><p>Hidden</p></details>"
        "</body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var button = document.getElementById('button');"
        "var check = document.getElementById('check');"
        "var panel = document.getElementById('panel');"
        "var summary = document.getElementById('summary');"
        "var log = document.readyState + ';';"
        "button.onclick = function (event) { log += event.type + ':' + event.clientX + ';'; };"
        "check.onchange = function () { log += 'change:' + check.checked + ';'; };"
        "panel.ontoggle = function () { log += 'toggle:' + panel.open + ';'; };"
        "button.click();"
        "check.click();"
        "summary.click();"
        "log + String(check.checked) + ':' + String(panel.open)");

    check(result.ok, "document readyState and element click script succeeds");
    check(result.value == "complete;click:0;change:true;toggle:true;true:true",
          "readyState is complete and click() dispatches bounded activation behavior");

    result = runtime.eval(
        "summary.onclick = function (event) { event.preventDefault(); };"
        "summary.click();"
        "String(panel.open)");
    check(result.ok && result.value == "true", "preventDefault blocks summary click default toggle");
}

void javascript_small_document_and_text_idl_tail_works() {
    HtmlParser parser;
    auto document = parser.parse("<html><head><title>Demo</title></head><body><p id='label'>Ready</p></body></html>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var label = document.getElementById('label');"
        "var before = label.innerText;"
        "label.innerText = 'Done';"
        "before + ':' + label.textContent + ':' + document.head.tagName + ':' + document.hasFocus() + ':' + "
        "(document.defaultView === window) + ':' + (self === window) + ':' + "
        "(window.window === window) + ':' + (window.document === document) + ':' + "
        "(window.navigator === navigator) + ':' + "
        "origin + ':' + isSecureContext + ':' + crossOriginIsolated");

    check(result.ok, "small document/text IDL tail script succeeds");
    check(result.value == "Ready:Done:head:true:true:true:true:true:true:null:false:false",
          "innerText, document focus/defaultView and global environment constants are exposed");
}

void document_static_collections_work() {
    HtmlParser parser;
    auto document = parser.parse(
        "<html><head><script></script></head><body>"
        "<img id='hero' name='asset'><a id='home' href='#home'>Home</a><a id='plain'>Plain</a>"
        "<form id='form' name='account'><input name='account'></form><embed id='plugin'>"
        "</body></html>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var images = document.images;"
        "var before = images.length + ':' + images[0].id + ':' + "
        "document.links.length + ':' + document.links[0].id + ':' + "
        "document.forms.length + ':' + document.forms[0].id + ':' + "
        "document.scripts.length + ':' + document.embeds.length + ':' + document.plugins.length + ':' + "
        "document.getElementsByName('account').length;"
        "var added = document.createElement('img');"
        "document.body.appendChild(added);"
        "before + ':' + images.length + ':' + document.images.length");

    check(result.ok, "document static collection script succeeds");
    check(result.value == "1:hero:1:home:1:form:1:1:1:2:1:2",
          "document collections expose static snapshots for common element sets");
}

void element_specific_reflected_idl_properties_work() {
    HtmlParser parser;
    auto document = parser.parse(
        "<html><head><meta id='meta' name='viewport' content='width=device-width'></head><body>"
        "<a id='link' download='demo.txt' ping='p' rel='nofollow' referrerpolicy='no-referrer'>Open</a>"
        "<data id='data' value='42'>Answer</data><time id='time' datetime='2026-07-05'>Today</time>"
        "<img id='image' alt='Cloud'>"
        "<label id='label' for='field'>Field</label>"
        "<input id='field' value='A'><label id='wrapped'><input id='wrappedField' value='B'></label>"
        "</body></html>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var meta = document.getElementById('meta');"
        "var link = document.getElementById('link');"
        "var data = document.getElementById('data');"
        "var time = document.getElementById('time');"
        "var image = document.getElementById('image');"
        "var label = document.getElementById('label');"
        "var wrapped = document.getElementById('wrapped');"
        "var before = meta.name + ':' + meta.content + ':' + link.text + ':' + data.value + ':' + "
        "time.dateTime + ':' + image.alt + ':' + label.htmlFor + ':' + label.control.id + ':' + "
        "wrapped.control.id;"
        "meta.httpEquiv = 'refresh'; meta.media = 'screen'; meta.content = 'ok';"
        "link.text = 'Launch'; link.download = 'new.txt'; link.ping = 'a b'; link.rel = 'noopener';"
        "link.referrerPolicy = 'origin'; data.value = '84'; time.dateTime = '2026-07-06';"
        "image.alt = 'Rain'; label.htmlFor = 'wrappedField';"
        "before + ':' + meta.getAttribute('http-equiv') + ':' + meta.media + ':' + meta.content + ':' + "
        "link.textContent + ':' + link.download + ':' + link.ping + ':' + link.rel + ':' + "
        "link.referrerPolicy + ':' + data.getAttribute('value') + ':' + time.getAttribute('datetime') + ':' + "
        "image.getAttribute('alt') + ':' + label.getAttribute('for') + ':' + label.control.id");

    check(result.ok, "element-specific reflected IDL script succeeds");
    check(result.value ==
              "viewport:width=device-width:Open:42:2026-07-05:Cloud:field:field:wrappedField:refresh:screen:ok:"
              "Launch:new.txt:a b:noopener:origin:84:2026-07-06:Rain:wrappedField:wrappedField",
          "element-specific IDL properties reflect bounded content attributes");
}

void javascript_element_style_extended_properties_work() {
    HtmlParser parser;
    auto document = parser.parse("<body><div id='dial'></div></body>");
    clear_dirty_flags(*document);

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var dial = document.getElementById('dial');"
        "dial.style.opacity = '0.72';"
        "dial.style.transform = 'translate(4px, 2px) rotate(15deg)';"
        "dial.style.borderRadius = '50%';"
        "dial.style.fontSize = '18px';"
        "dial.style.lineHeight = '22px';"
        "dial.style.textTransform = 'uppercase';"
        "dial.style.boxSizing = 'border-box';"
        "dial.style.padding = '4px 6px';"
        "dial.style.marginTop = '3px';"
        "dial.style.maxWidth = '100%';"
        "dial.style.backgroundImage = 'radial-gradient(#ffffff, rgba(36,126,160,.20))';"
        "dial.style.left = '6px';"
        "dial.style.top = '8px';"
        "dial.style.visibility = 'hidden';"
        "dial.style.setProperty('--progress', '76%');"
        "dial.style.setProperty('background-image', 'radial-gradient(circle, #ffffff, #000000)');"
        "dial.style.setProperty('background-image', \"url('/assets/cover.bmp')\");"
        "dial.style.setProperty('min-height', '44px');"
        "var oldProgress = dial.style.removeProperty('--progress');"
        "var missingFilter = dial.style.getPropertyValue('filter');"
        "dial.style.setProperty('filter', 'blur(4px)');"
        "oldProgress + ':' + missingFilter + ':' + dial.style.getPropertyValue('min-height') + ':' + "
        "dial.getAttribute('style')");

    check(result.ok, "extended style script succeeds");
    check(result.value.find("76%::44px:") == 0, "style get/removeProperty return inline values");
    check(result.value.find("opacity: 0.72") != std::string::npos, "opacity write serialized");
    check(result.value.find("transform: translate(4px, 2px) rotate(15deg)") != std::string::npos,
          "transform write serialized");
    check(result.value.find("border-radius: 50%") != std::string::npos, "borderRadius write serialized");
    check(result.value.find("font-size: 18px") != std::string::npos, "fontSize write serialized");
    check(result.value.find("line-height: 22px") != std::string::npos, "lineHeight write serialized");
    check(result.value.find("text-transform: uppercase") != std::string::npos,
          "textTransform write serialized");
    check(result.value.find("box-sizing: border-box") != std::string::npos, "boxSizing write serialized");
    check(result.value.find("padding: 4px 6px") != std::string::npos, "padding write serialized");
    check(result.value.find("margin-top: 3px") != std::string::npos, "marginTop write serialized");
    check(result.value.find("max-width: 100%") != std::string::npos, "maxWidth write serialized");
    check(result.value.find("min-height: 44px") != std::string::npos, "setProperty min-height serialized");
    check(result.value.find("background-image: url('/assets/cover.bmp')") != std::string::npos,
          "package backgroundImage write serialized");
    check(result.value.find("visibility: hidden") != std::string::npos, "visibility write serialized");
    check(result.value.find("--progress") == std::string::npos, "removeProperty removes custom property");
    check(result.value.find("filter") == std::string::npos, "unsupported style.setProperty is ignored");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "extended style writes mark layout dirty");
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

    const ScriptEvaluationResult reentrant = runtime.eval(
        "var reentrantXhrs = [];"
        "xhr.onload = function () { for (var i = 0; i < 8; ++i) reentrantXhrs.push(new XMLHttpRequest()); };"
        "xhr.open('GET', '/data/weather.json', true); xhr.send(); 'armed';");
    check(reentrant.ok && reentrant.value == "armed", "XHR reentrant completion script evaluates");
    check(complete_network_and_dispatch(host, network, runtime) == 1, "XHR reentrant completion dispatched");
    check(runtime.statistics().xml_http_request_count == 9,
          "XHR callback can allocate wrappers without invalidating completion dispatch");
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

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.xhr-budget", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{64, 512, 256, 1});
    runtime.bind_app_services(host, network);
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

    {
        JerryScriptRuntime runtime;
        runtime.bind_document(*document);
        ScriptEvaluationResult result = runtime.eval("typeof XMLHttpRequest");
        check(result.ok, "XHR absence script evaluates");
        check(result.value == "undefined", "XHR is absent when no network host is bound");
    }

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.xhr-constructor", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    JerryScriptRuntime runtime_with_network;
    runtime_with_network.bind_app_services(host, network);
    runtime_with_network.bind_document(*document);
    ScriptEvaluationResult result = runtime_with_network.eval("String(XMLHttpRequest === window.XMLHttpRequest)");
    check(result.ok, "XHR constructor identity script evaluates");
    check(result.value == "true", "global and window share the XHR constructor when bound");
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
        check(result.value == "undefined:false:false", "Audio is absent without a bound host");
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

        result = runtime_with_audio.eval(
            "var reentrantAudio = [];"
            "tone.onended = function () { for (var i = 0; i < 4; ++i) reentrantAudio.push(new Audio('/audio/tone.wav')); };"
            "'armed';");
        check(result.ok && result.value == "armed", "Audio reentrant dispatch script evaluates");
        check(runtime_with_audio.dispatch_audio_event(host.audio_id, ScriptAudioEventKind::Ended),
              "Audio reentrant dispatch reports handled");
        check(runtime_with_audio.statistics().audio_element_count == 5,
              "Audio callback can allocate wrappers without invalidating event dispatch");
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

void javascript_service_objects_are_invalidated_after_clear_and_rebind() {
    HtmlParser parser;
    auto first_document = parser.parse("<body></body>");
    auto second_document = parser.parse("<body><p>next</p></body>");

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 2});
    host.launch("org.example.service-rebind", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    AppLocalStorageShadow storage(AppPrivateKvPolicy{true, 16, 32, 4, 128});
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, 2});
    FakeAudioHost audio_host;

    JerryScriptRuntime runtime;
    runtime.bind_app_services(host, network);
    runtime.bind_location_service(host, location);
    runtime.bind_local_storage(storage);
    runtime.bind_audio_host(ScriptAudioHost{fake_audio_play, &audio_host});
    runtime.bind_document(*first_document);
    ScriptEvaluationResult result = runtime.eval(
        "var SavedXHR = XMLHttpRequest;"
        "var xhr = new XMLHttpRequest();"
        "var SavedAudio = Audio;"
        "var tone = new Audio('/tone.wav');"
        "var store = localStorage;"
        "store.setItem('before', 'ok');"
        "'armed'");
    check(result.ok && result.value == "armed", "service objects are created before clear");
    check(runtime.statistics().xml_http_request_count == 1, "one active XHR before clear");
    check(runtime.statistics().audio_element_count == 1, "one active Audio before clear");

    check(location.set_fixture(AppLocationSnapshotFixture{1234, 31.2304, 121.4737, 4.0f, 8.0f, 0.2f}),
          "geolocation fixture accepted before service clear");
    result = runtime.eval(
        "var geoState = 'pending';"
        "navigator.geolocation.getCurrentPosition(function () { geoState = 'success'; },"
        "  function (error) { geoState = 'error:' + error.code + ':' + error.message; });"
        "geoState");
    check(result.ok && result.value == "pending", "geolocation request starts before service clear");
    check(location.complete_next(host), "geolocation host worker completes before service clear");
    std::vector<HostServiceCompletion> completions;
    host.pump_frame_completions(completions);
    check(completions.size() == 1, "geolocation completion is queued before service clear");
    check(host.handles().active_count() == 1, "geolocation completion owns one host handle before clear");
    const std::uint32_t non_script_handle = host.handles().allocate(
        HostServiceHandleKind::ComputeResult, host.current_app_instance_id(), 16);
    check(non_script_handle != 0, "non-script app handle allocated before service clear");
    check(host.handles().active_count() == 2, "script and non-script handles coexist before service clear");

    runtime.clear_app_services();
    result = runtime.eval(
        "var types = [typeof XMLHttpRequest, typeof Audio, typeof localStorage].join(':');"
        "var xhrOpen = true;"
        "try { xhr.open('GET', '/late'); } catch (e) { xhrOpen = false; }"
        "var xhrCtor = true;"
        "try { new SavedXHR(); } catch (e) { xhrCtor = false; }"
        "var audioPlay = true;"
        "try { tone.play(); } catch (e) { audioPlay = false; }"
        "var audioCtor = true;"
        "try { new SavedAudio('/late.wav'); } catch (e) { audioCtor = false; }"
        "var storageWrite = true;"
        "try { store.setItem('after', 'bad'); } catch (e) { storageWrite = false; }"
        "types + '|' + [xhrOpen, xhrCtor, audioPlay, audioCtor, storageWrite].join(':')");

    check(result.ok, "cleared service objects script evaluates");
    check(result.value == "undefined:undefined:undefined|false:false:false:false:false",
          "cleared services immediately remove globals and invalidate cached objects");
    result = runtime.eval("typeof navigator.geolocation");
    check(result.ok && result.value == "undefined",
          "cleared services immediately remove geolocation namespace");
    check(runtime.statistics().xml_http_request_count == 0, "cleared XHR is not active");
    check(runtime.statistics().audio_element_count == 0, "cleared Audio is not active");
    check(runtime.statistics().geolocation_request_count == 0, "cleared geolocation request record is collected");
    check(host.handles().active_count() == 1 && host.handles().contains(non_script_handle),
          "cleared services release only runtime-owned handles");
    check(storage.get_item("after", nullptr) == AppLocalStorageStatus::NotFound,
          "cleared localStorage object cannot write after service clear");

    runtime.bind_document(*second_document);
    result = runtime.eval("typeof XMLHttpRequest + ':' + typeof Audio + ':' + typeof localStorage");
    check(result.ok && result.value == "undefined:undefined:undefined",
          "document rebind keeps revoked service globals absent");
}

void cleared_services_reclaim_late_runtime_completion_only() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.service-late", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    check(network.add_fixture(NetworkFetchFixture{"/late.json", 200, "application/json", "{}"}),
          "late service fixture accepted");

    JerryScriptRuntime runtime;
    runtime.bind_app_services(host, network);
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var late = new XMLHttpRequest(); late.open('GET', '/late.json'); late.send(); 'sent';");
    check(result.ok && result.value == "sent", "runtime-owned late XHR submitted");
    HostServiceRequest request;
    check(host.pop_worker_request(HostServiceJobKind::NetworkFetch, request), "late XHR worker owns request");
    check(request.client_token != 0, "runtime request carries a client token");
    const std::uint32_t non_script_handle = host.handles().allocate(
        HostServiceHandleKind::ComputeResult, host.current_app_instance_id(), 16);
    check(non_script_handle != 0, "late fixture non-script handle allocated");

    runtime.clear_app_services();
    const HostServiceCompletion completion = network.complete_request(host, request);
    check(completion.client_token == request.client_token, "worker completion preserves the client token");
    check(completion.handle != 0, "late worker completion allocates a response handle");
    check(runtime.handle_host_completion(completion), "cleared runtime consumes its late completion");
    check(host.handles().active_count() == 1 && host.handles().contains(non_script_handle),
          "late runtime completion does not release non-script app handles");
    check(network.response(completion.handle) == nullptr, "late runtime response record is reclaimed");
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

void javascript_runtime_honors_zero_host_budgets_and_deduplicates_listeners() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='button'>Go</button></body>");
    Node* button = find_first_by_tag(*document, "button");
    check(button != nullptr, "zero-budget button exists");

    HostBudgets zero_budgets;
    zero_budgets.max_timers = 0;
    zero_budgets.max_event_listeners = 0;
    zero_budgets.max_detached_dom_nodes = 0;
    zero_budgets.max_active_animations = 0;
    {
        JerryScriptRuntime zero_runtime(zero_budgets);
        zero_runtime.bind_document(*document);
        const ScriptEvaluationResult zero_result = zero_runtime.eval(
            "var button = document.getElementById('button');"
            "String(setTimeout(function () {}, 1)) + ':' +"
            "String(requestAnimationFrame(function () {})) + ':' +"
            "String(button.addEventListener('click', function () {}));");
        check(zero_result.ok && zero_result.value == "0:0:undefined",
              "zero host budgets reject timer and animation allocation without failing script evaluation");
        check(zero_runtime.statistics().timer_count == 0 &&
                  zero_runtime.statistics().animation_frame_callback_count == 0 &&
                  zero_runtime.statistics().event_listener_count == 0,
              "zero host budgets do not allocate script-owned records");
    }

    JerryScriptRuntime runtime(JerryScriptRuntimeOptions{4, 4});
    runtime.bind_document(*document);
    const ScriptEvaluationResult duplicate_result = runtime.eval(
        "var button = document.getElementById('button');"
        "var calls = 0;"
        "function listener() { calls += 1; }"
        "button.addEventListener('click', listener);"
        "button.addEventListener('click', listener);"
        "button.removeEventListener('click', listener, true);"
        "button.click();"
        "String(calls) + ':' + String(button.removeEventListener('click', listener));");
    check(duplicate_result.ok && duplicate_result.value == "1:undefined",
          "listener identity ignores duplicate add and remove respects capture");
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
        "window.ononline = function (event) {"
        "  networkEvents += 'prop-' + event.type + ':' + String(event.target === window) + ';';"
        "  window.ononline = null;"
        "};"
        "onoffline = function (event) { networkEvents += 'prop-' + event.type + ':' + String(event.target === window) + ';'; };"
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
    result = runtime.eval("String(navigator.onLine) + ':' + networkEvents + ':' + String(window.ononline)");
    check(result.ok && result.value == "true:prop-online:true;online:true;:null",
          "navigator.onLine and window online event update");

    network_snapshot.network_online = false;
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::NetworkStatusChanged, network_snapshot}),
          "offline system event handled");
    check(runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::NetworkStatusChanged, network_snapshot}),
          "unchanged offline event handled without redispatch");
    result = runtime.eval("String(navigator.onLine) + ':' + networkEvents");
    check(result.ok && result.value == "false:prop-online:true;online:true;prop-offline:true;offline;",
          "window offline event fires once and only on state change");

    result = runtime.eval(
        "var visibilityEvents = 0;"
        "document.onvisibilitychange = function () { visibilityEvents += 100; };"
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
    check(result.ok && result.value == "true:hidden:101", "document visibility state updates");
}

void javascript_host_data_snapshot_is_explicit_and_filtered() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    AppHostDataSnapshot snapshot;
    snapshot.has.battery = true;
    snapshot.battery = AppBatterySnapshot{1000, 87, true};
    snapshot.has.weather = true;
    snapshot.weather = AppWeatherSnapshot{1001, AppWeatherCondition::Rain, 213, 201, 74, 42, 13, 38};
    snapshot.has.activity = true;
    snapshot.activity = AppActivitySnapshot{1002, 6400, 32, 230, 4100};

    {
        JerryScriptRuntime unbound;
        unbound.bind_document(*document);
        const ScriptEvaluationResult result = unbound.eval("typeof navigator.jellyframe");
        check(result.ok && result.value == "undefined", "host-data namespace is absent until explicitly bound");
    }

    AppHostDataAccessPolicy policy;
    policy.battery = true;
    policy.weather = true;
    JerryScriptRuntime runtime;
    runtime.bind_host_data_snapshot(snapshot, policy);
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var data = navigator.jellyframe.getSnapshot();"
        "data.battery.percent + ':' + data.battery.charging + ':' + data.weather.condition + ':' + "
        "data.weather.temperatureC + ':' + String(data.activity)");
    check(result.ok, "host-data snapshot script evaluates");
    check(result.value == "87:true:rain:21.3:null", "host-data snapshot keeps only granted summaries");

    snapshot.battery.percent = 42;
    result = runtime.eval("navigator.jellyframe.getSnapshot().battery.percent");
    check(result.ok && result.value == "42", "host-data reads the latest host snapshot without polling");

    runtime.clear_app_services();
    result = runtime.eval("typeof navigator.jellyframe");
    check(result.ok && result.value == "undefined",
          "clearing services immediately removes host-data namespace");
    runtime.bind_document(*document);
    result = runtime.eval("typeof navigator.jellyframe");
    check(result.ok && result.value == "undefined", "clearing services removes host-data namespace");
}

void javascript_location_hash_routes_within_one_app() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);

    ScriptEvaluationResult result = runtime.eval(
        "var routeEvents = '';"
        "window.onhashchange = function (event) { routeEvents += event.type + ':' + location.hash + ';'; };"
        "window.addEventListener('hashchange', function (event) { routeEvents += 'listener:' + String(event.target === window) + ';'; });"
        "location.hash = 'settings';"
        "location.hash = '#settings';"
        "window.location.hash = '#about';"
        "String(location.hash) + ':' + routeEvents");
    check(result.ok, "location hash route script evaluates");
    check(result.value == "#about:hashchange:#settings;listener:true;hashchange:#about;listener:true;",
          "location hash updates and dispatches one window event per route change");

    result = runtime.eval(
        "var popEvents = '';"
        "window.onpopstate = function (event) { popEvents += event.type + ':' + location.hash + ';'; };"
        "history.back(); var afterBack = location.hash;"
        "history.forward();"
        "history.pushState(null, '', '#advanced');"
        "history.replaceState(null, '', '#details');"
        "history.back();"
        "String(typeof location.assign) + ':' + String(window.history === history) + ':' + history.length + ':' + "
        "afterBack + ':' + location.hash + ':' + popEvents");
    check(result.ok && result.value == "undefined:true:4:#settings:#about:popstate:#settings;popstate:#about;popstate:#about;",
          "fragment-only history traverses one app without browser navigation");

    auto rebound_document = parser.parse("<body></body>");
    runtime.bind_document(*rebound_document);
    result = runtime.eval("String(location.hash)");
    check(result.ok && result.value.empty(), "location route fragment resets for a new document");
}

void javascript_date_now_uses_host_time() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");

    JerryScriptRuntime runtime;
    runtime.set_host_time_ms(1700000000123ULL);
    runtime.bind_document(*document);

    ScriptEvaluationResult result = runtime.eval("String(Date.now())");
    check(result.ok && result.value == "1700000000123", "Date.now reads host time");

    AppSystemStateSnapshot snapshot;
    snapshot.unix_time_ms = 1700000000456ULL;
    check(!runtime.handle_system_event(AppSystemEvent{1, AppSystemEventKind::TimeChanged, snapshot}),
          "time system event updates clock without dispatching a web event");

    result = runtime.eval("String(Date.now())");
    check(result.ok && result.value == "1700000000456", "Date.now follows time system events");
}

void javascript_geolocation_uses_bound_location_service() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='status'>ready</p></body>");

    {
        JerryScriptRuntime unbound;
        unbound.bind_document(*document);
        ScriptEvaluationResult result = unbound.eval("String(typeof navigator.geolocation)");
        check(result.ok && result.value == "undefined", "geolocation is absent when no location service is bound");
    }

    AppRuntimeHost host(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
    host.launch("org.example.geo", AppRole::App);
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, 2});
    check(location.set_fixture(AppLocationSnapshotFixture{1234, 31.2304, 121.4737, 4.0f, 8.0f, 0.2f}),
          "geolocation fixture accepted");

    {
        JerryScriptRuntime runtime;
        runtime.bind_location_service(host, location);
        runtime.bind_document(*document);
        ScriptEvaluationResult result = runtime.eval(
            "var geoResult = 'pending';"
            "navigator.geolocation.getCurrentPosition(function (pos) {"
            "  geoResult = String(pos.coords.latitude) + ',' + String(pos.coords.longitude) + ',' +"
            "    String(pos.coords.accuracy) + ',' + String(pos.timestamp);"
            "}, function (error) { geoResult = 'error:' + error.code + ':' + error.message; });"
            "geoResult");
        check(result.ok && result.value == "pending", "geolocation request starts asynchronously");
        check(location.complete_next(host), "geolocation host worker completes");
        std::vector<HostServiceCompletion> completions;
        host.pump_frame_completions(completions);
        check(completions.size() == 1, "geolocation completion pumped");
        check(runtime.handle_host_completion(completions.front()), "geolocation completion handled");
        result = runtime.eval("geoResult");
        check(result.ok && result.value == "31.2304,121.4737,8,1234", "geolocation success object shape");
        check(host.handles().active_count() == 0, "geolocation completion releases host handle");
        check(runtime.statistics().geolocation_request_count == 0, "geolocation request record is collected");
    }

    AppLocationSnapshotMock missing_location(AppLocationSnapshotPolicy{true, 1});
    {
        JerryScriptRuntime missing_runtime;
        missing_runtime.bind_location_service(host, missing_location);
        missing_runtime.bind_document(*document);
        ScriptEvaluationResult result = missing_runtime.eval(
            "var geoError = 'pending';"
            "navigator.geolocation.getCurrentPosition(function () { geoError = 'success'; },"
            "  function (error) { geoError = String(error.code) + ':' + error.message; });"
            "geoError");
        check(result.ok && result.value == "pending", "geolocation missing request starts asynchronously");
        check(missing_location.complete_next(host), "geolocation missing host worker completes");
        std::vector<HostServiceCompletion> completions;
        host.pump_frame_completions(completions);
        check(completions.size() == 1, "geolocation missing completion pumped");
        check(missing_runtime.handle_host_completion(completions.front()), "geolocation missing completion handled");
        result = missing_runtime.eval("geoError");
        check(result.ok && result.value == "2:geolocation position unavailable", "geolocation error callback shape");
    }

    {
        AppRuntimeHost reentrant_host(AppRuntimeHostOptions{8, 8, 8, 4096, 2});
        reentrant_host.launch("org.example.geo-reentrant", AppRole::App);
        AppLocationSnapshotMock reentrant_location(AppLocationSnapshotPolicy{true, 2});
        check(reentrant_location.set_fixture(AppLocationSnapshotFixture{99, 30.0, 120.0}),
              "reentrant geolocation fixture accepted");
        JerryScriptRuntime reentrant_runtime;
        reentrant_runtime.bind_location_service(reentrant_host, reentrant_location);
        reentrant_runtime.bind_document(*document);
        ScriptEvaluationResult result = reentrant_runtime.eval(
            "var geoOrder = '';"
            "navigator.geolocation.getCurrentPosition(function () {"
            "  geoOrder += 'first;';"
            "  navigator.geolocation.getCurrentPosition(function () { geoOrder += 'second;'; });"
            "});"
            "geoOrder;");
        check(result.ok && result.value.empty(), "reentrant geolocation starts first request");

        check(reentrant_location.complete_next(reentrant_host), "first reentrant location completion queued");
        std::vector<HostServiceCompletion> completions;
        reentrant_host.pump_frame_completions(completions);
        check(completions.size() == 1 && reentrant_runtime.handle_host_completion(completions.front()),
              "first reentrant location completion handled");
        check(reentrant_location.complete_next(reentrant_host), "second reentrant location completion queued");
        completions.clear();
        reentrant_host.pump_frame_completions(completions);
        check(completions.size() == 1 && reentrant_runtime.handle_host_completion(completions.front()),
              "second reentrant location completion handled");
        result = reentrant_runtime.eval("geoOrder");
        check(result.ok && result.value == "first;second;", "reentrant geolocation callbacks stay ordered and safe");
        check(reentrant_host.handles().active_count() == 0, "reentrant geolocation releases both handles");
    }
}

void javascript_form_submission_and_form_data_work() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form id='account'><input id='name' name='name' required>"
        "<input id='agree' name='agree' type='checkbox' required value='yes'>"
        "<button id='send' name='action' value='save'>Save</button>"
        "<button id='reset' type='reset'>Reset</button></form></body>");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var form = document.getElementById('account');"
        "var name = document.getElementById('name');"
        "var agree = document.getElementById('agree');"
        "var send = document.getElementById('send');"
        "var reset = document.getElementById('reset');"
        "var invalidCount = 0; var submitterId = 'none'; var submitValue = 'none';"
        "name.addEventListener('invalid', function () { invalidCount++; });"
        "form.addEventListener('submit', function (event) {"
        "  submitterId = event.submitter.id;"
        "  submitValue = new FormData(form).get('name') + ':' + new FormData(form).get('agree');"
        "  event.preventDefault();"
        "});"
        "form.requestSubmit(send);"
        "name.value = 'Ada'; agree.checked = true;"
        "form.requestSubmit(send);"
        "var data = new FormData(form);"
        "data.append('tag', 'one'); data.append('tag', 'two'); data.set('tag', 'three');"
        "var iteration = ''; var receiver = { prefix: '@' };"
        "data.forEach(function (value, key, source) { iteration += this.prefix + key + '=' + value + ':' + (source === data) + ';'; }, receiver);"
        "reset.click();"
        "String(invalidCount) + ':' + submitterId + ':' + submitValue + ':' + data.get('tag') + ':' +"
        "String(data.getAll('tag').length) + ':' + String(data.has('name')) + ':' + String(form.checkValidity()) + ':' +"
        "name.value + ':' + String(agree.checked) + ':' + iteration;");
    check(result.ok && result.value == "1:send:Ada:yes:three:1:true:false::false:@name=Ada:true;@agree=yes:true;@tag=three:true;",
          "form validation, submit event, FormData and reset work through JavaScript");
}

void javascript_form_data_budget_is_bounded() {
    HtmlParser parser;
    auto document = parser.parse("<body></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var data = new FormData(); var rejected = false;"
        "try { for (var i = 0; i < 33; i++) data.append('k' + i, 'v'); }"
        "catch (error) { rejected = true; }"
        "String(data.getAll('k0').length) + ':' + rejected;");
    if (!result.ok) {
        throw std::runtime_error("FormData budget evaluation failed: " + result.error);
    }
    if (result.value != "1:true") {
        throw std::runtime_error("FormData budget result: " + result.value);
    }
}

void javascript_control_validity_subset_works() {
    HtmlParser parser;
    auto document = parser.parse(
        "<body><form><input id='name' required minlength='3'><input id='disabled' required disabled>"
        "<button id='send'>Send</button></form></body>");
    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var name = document.getElementById('name');"
        "var disabled = document.getElementById('disabled');"
        "var send = document.getElementById('send');"
        "var invalid = 0; name.addEventListener('invalid', function () { invalid++; });"
        "var before = name.validity.valueMissing + ':' + name.validity.valid + ':' + name.validationMessage + ':' + name.willValidate;"
        "var checked = name.checkValidity();"
        "name.value = 'Al';"
        "var shortState = name.validity.tooShort + ':' + name.validationMessage;"
        "name.setCustomValidity('Choose a full name.');"
        "var customState = name.validity.customError + ':' + name.validity.valid + ':' + name.validationMessage;"
        "name.setCustomValidity('');"
        "before + ':' + checked + ':' + invalid + ':' + shortState + ':' + customState + ':' + "
        "disabled.willValidate + ':' + disabled.validity.valid + ':' + send.willValidate + ':' + send.type");
    check(result.ok && result.value ==
                           "true:false:Please fill out this field.:true:false:1:true:Value is too short.:true:false:Choose a full name.:false:true:false:submit",
          "control-level ValidityState subset works through JavaScript");
}

void javascript_canvas_2d_is_optional_and_lazy() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='chart' width='8' height='8'></canvas></body>");
    Node* canvas = find_first_by_tag(*document, "canvas");
    check(canvas != nullptr, "canvas element exists");

    {
        JerryScriptRuntime runtime;
        runtime.bind_document(*document);
        const ScriptEvaluationResult result =
            runtime.eval("document.getElementById('chart').getContext('2d') === null");
        check(result.ok && result.value == "true", "canvas getContext returns null when unbound");
    }

    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 64, 64, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('chart').getContext('2d');"
        "ctx.fillStyle = '#336699';"
        "ctx.fillRect(5, 1, 2, 2);"
        "ctx.strokeStyle = '#ffffff';"
        "ctx.beginPath();"
        "ctx.moveTo(0, 0);"
        "ctx.lineTo(7, 7);"
        "ctx.stroke();"
        "ctx.save();"
        "ctx.globalAlpha = 0.5;"
        "ctx.fillStyle = '#ff0000';"
        "ctx.fillRect(0, 6, 1, 1);"
        "ctx.restore();"
        "ctx.font = 'bold 10px system-ui';"
        "var textWidth = ctx.measureText('Hi').width;"
        "ctx.fillStyle = '#00ff00';"
        "ctx.fillText('Hi', 1, 5);"
        "var gradient = ctx.createLinearGradient(0, 0, 8, 0);"
        "gradient.addColorStop(0, '#000000');"
        "gradient.addColorStop(1, '#ffffff');"
        "ctx.fillStyle = gradient;"
        "ctx.fillRect(6, 0, 2, 1);"
        "ctx.fillStyle = '#00ff00';"
        "ctx.beginPath();"
        "ctx.moveTo(6, 6);"
        "ctx.arc(6, 6, 2, 0, Math.PI);"
        "ctx.closePath();"
        "ctx.fill();"
        "ctx.font + ':' + String(textWidth > 0) + ':' + ctx.fillStyle");
    check(result.ok && result.value == "bold 10px system-ui:true:#00ff00",
          "canvas context draws and reflects text state");

    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr, "canvas surface allocated lazily after getContext");
    const Color filled = surface->pixels[1 * surface->width + 5];
    check(filled.r == 0x33 && filled.g == 0x66 && filled.b == 0x99, "canvas fillRect updated pixels");
    const Color stroked = surface->pixels[1 * surface->width + 1];
    check(stroked.r == 255 && stroked.g == 255 && stroked.b == 255, "canvas stroke updated pixels");
    const Color translucent = surface->pixels[6 * surface->width + 0];
    check(translucent.r == 255 && translucent.a >= 126 && translucent.a <= 129,
          "canvas globalAlpha affects fillRect");
    const Color gradient_pixel = surface->pixels[0 * surface->width + 7];
    check(gradient_pixel.r > 180 && gradient_pixel.g > 180 && gradient_pixel.b > 180,
          "canvas linear gradient fillStyle updates pixels");
    const Color arc_filled = surface->pixels[6 * surface->width + 6];
    check(arc_filled.r == 0x00 && arc_filled.g == 0xff && arc_filled.b == 0x00,
          "canvas arc and fill are bound to script");
    check((subtree_dirty_flags(*document) & DomDirtyPaint) != 0U, "canvas drawing marks paint dirty");
}

void javascript_canvas_bezier_curve_to_strokes_path() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='16' height='16'></canvas></body>");
    Node* canvas = find_by_id(*document, "canvas");
    check(canvas != nullptr, "canvas bezier path node exists");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 256, 256, 16, 16});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "ctx.strokeStyle = '#ffffff'; ctx.beginPath(); ctx.moveTo(1, 12);"
        "ctx.bezierCurveTo(4, 0, 12, 0, 15, 12); ctx.stroke(); 'ok';");
    check(result.ok && result.value == "ok", "canvas bezierCurveTo script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr && surface->pixels[4 * surface->width + 8].a > 0,
          "canvas bezierCurveTo script strokes curved middle");
}

void javascript_canvas_quadratic_curve_to_strokes_path() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='16' height='16'></canvas></body>");
    Node* canvas = find_by_id(*document, "canvas");
    check(canvas != nullptr, "canvas quadratic path node exists");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 256, 256, 16, 16});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "ctx.strokeStyle = '#ffffff'; ctx.beginPath(); ctx.moveTo(1, 12);"
        "ctx.quadraticCurveTo(8, 0, 15, 12); ctx.stroke(); 'ok';");
    check(result.ok && result.value == "ok", "canvas quadraticCurveTo script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr && surface->pixels[6 * surface->width + 8].a > 0,
          "canvas quadraticCurveTo script strokes curved middle");
}

void javascript_canvas_reset_transform_clears_translation() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='8' height='8'></canvas></body>");
    Node* canvas = find_by_id(*document, "canvas");
    check(canvas != nullptr, "canvas reset transform node exists");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 64, 64, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "ctx.fillStyle = '#ff0000'; ctx.translate(3, 2); ctx.resetTransform(); ctx.fillRect(0, 0, 1, 1); 'ok';");
    check(result.ok && result.value == "ok", "canvas resetTransform script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr && surface->pixels[0].r == 255, "canvas resetTransform clears translation");
}

void javascript_canvas_translate_is_pixel_aligned_and_saved() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='8' height='8'></canvas></body>");
    Node* canvas = find_by_id(*document, "canvas");
    check(canvas != nullptr, "canvas translate node exists");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 64, 64, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "ctx.fillStyle = '#ff0000'; ctx.translate(2.4, 1.6); ctx.fillRect(0, 0, 1, 1);"
        "ctx.save(); ctx.translate(2, 0); ctx.fillRect(0, 0, 1, 1); ctx.restore();"
        "ctx.fillRect(1, 0, 1, 1); 'ok';");
    check(result.ok && result.value == "ok", "canvas translate script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr && surface->pixels[2 * surface->width + 2].r == 255 &&
              surface->pixels[2 * surface->width + 3].r == 255 &&
              surface->pixels[2 * surface->width + 4].r == 255,
          "canvas translate uses rounded integer offsets and save restore");
}

void javascript_canvas_radial_gradient_uses_concentric_two_stop_subset() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='8' height='8'></canvas></body>");
    Node* canvas = find_by_id(*document, "canvas");
    check(canvas != nullptr, "canvas radial gradient node exists");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 64, 64, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "var gradient = ctx.createRadialGradient(4, 4, 0, 4, 4, 4);"
        "gradient.addColorStop(0, '#000000');"
        "gradient.addColorStop(1, '#ffffff');"
        "ctx.fillStyle = gradient; ctx.fillRect(0, 0, 8, 8); 'ok';");
    check(result.ok && result.value == "ok", "canvas radial gradient script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    check(surface != nullptr && surface->pixels[4 * surface->width + 4].r < 80 &&
              surface->pixels[0 * surface->width + 4].r > 220,
          "canvas radial gradient script shades center and edge");
    const ScriptEvaluationResult unsupported = runtime.eval(
        "String(ctx.createRadialGradient(4, 4, 0, 5, 4, 4));");
    check(unsupported.ok && unsupported.value == "null", "canvas radial gradient rejects non-concentric circles");
}

void javascript_retained_canvas_gradient_is_safe_after_canvas_clear() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='canvas' width='8' height='8'></canvas></body>");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 1, 64, 64, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult created = runtime.eval(
        "var ctx = document.getElementById('canvas').getContext('2d');"
        "var retainedGradient = ctx.createLinearGradient(0, 0, 8, 0); 'ok';");
    check(created.ok && created.value == "ok", "canvas gradient wrapper is created");

    runtime.clear_canvas_2d();
    const ScriptEvaluationResult reused = runtime.eval("retainedGradient.addColorStop(0, '#ffffff'); 'ok';");
    check(reused.ok && reused.value == "ok", "retained gradient is safe after canvas clear");
}

void javascript_canvas_draw_image_copies_and_scales_canvas_source() {
    HtmlParser parser;
    auto document = parser.parse("<body><canvas id='src' width='4' height='4'></canvas><canvas id='dst' width='8' height='8'></canvas></body>");
    Node* source = find_by_id(*document, "src");
    Node* destination = find_by_id(*document, "dst");
    check(source != nullptr && destination != nullptr, "canvas drawImage nodes exist");
    Canvas2DRegistry registry(Canvas2DPolicy{true, 2, 64, 128, 8, 8});
    JerryScriptRuntime runtime;
    runtime.bind_canvas_2d(registry);
    runtime.bind_document(*document);
    const ScriptEvaluationResult result = runtime.eval(
        "var src = document.getElementById('src');"
        "var dst = document.getElementById('dst');"
        "var a = src.getContext('2d'); var b = dst.getContext('2d');"
        "a.fillStyle = '#ff0000'; a.fillRect(0, 0, 2, 2);"
        "b.drawImage(src, 0, 0);"
        "b.drawImage(src, 4, 0, 2, 2);"
        "b.drawImage(src, 0, 0, 2, 2, 2, 2, 4, 4);"
        "'ok';");
    check(result.ok && result.value == "ok", "canvas drawImage script succeeds");
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*destination));
    check(surface != nullptr && surface->pixels[0].r == 255 &&
              surface->pixels[0 * surface->width + 4].r == 255 &&
              surface->pixels[2 * surface->width + 2].r == 255 &&
              surface->pixels[5 * surface->width + 5].r == 255,
          "canvas drawImage script covers standard overloads");
}

void javascript_dialog_modal_subset_is_bounded_and_cancellable() {
    HtmlParser parser;
    auto document = parser.parse("<body><button id='launch'>Launch</button><dialog id='confirm'><button>OK</button></dialog></body>");
    Node* dialog = find_by_id(*document, "confirm");
    check(dialog != nullptr, "dialog fixture exists");

    JerryScriptRuntime runtime;
    runtime.bind_document(*document);
    ScriptEvaluationResult result = runtime.eval(
        "var d = document.getElementById('confirm');"
        "var cancelCount = 0; var closeCount = 0;"
        "d.addEventListener('cancel', function () { cancelCount++; });"
        "d.onclose = function () { closeCount++; };"
        "d.showModal(); d.open + ':' + d.returnValue;");
    check(result.ok && result.value == "true:", "showModal opens the dialog with an empty return value");
    check(runtime.active_modal_dialog() == dialog, "showModal exposes one active dialog to the host");

    check(runtime.request_modal_cancel(), "host cancel is consumed by the active modal");
    result = runtime.eval("d.open + ':' + cancelCount + ':' + closeCount + ':' + d.returnValue;");
    check(result.ok && result.value == "false:1:1:", "unprevented cancel closes and emits close exactly once");
    check(runtime.active_modal_dialog() == nullptr, "cancelled dialog releases modal state");

    result = runtime.eval(
        "d.showModal();"
        "d.addEventListener('cancel', function (event) { event.preventDefault(); });"
        "'open';");
    check(result.ok && result.value == "open", "dialog can reopen after cancellation");
    check(runtime.request_modal_cancel(), "prevented cancel still consumes the host request");
    result = runtime.eval("d.open + ':' + cancelCount + ':' + closeCount;");
    check(result.ok && result.value == "true:2:1", "prevented cancel keeps the modal open");

    result = runtime.eval("d.close('confirmed'); d.open + ':' + d.returnValue + ':' + closeCount;");
    check(result.ok && result.value == "false:confirmed:2", "close stores returnValue and emits close");
    check(runtime.active_modal_dialog() == nullptr, "explicit close releases modal state");

    result = runtime.eval("d.showModal(); document.body.textContent = 'replaced'; 'done';");
    check(result.ok && result.value == "done", "destroying an active dialog remains safe");
    check(runtime.active_modal_dialog() == nullptr, "destroyed modal clears host-visible state");
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
        host_budgets_map_to_script_runtime_options_without_field_drift();
        runtime_can_restart();
        base64_helpers_follow_html_binary_string_subset();
        inline_document_script_mutates_dom();
        document_get_element_by_id_updates_text_content();
        document_create_and_append_element();
        javascript_append_and_prepend_mix_text_and_nodes();
        remove_child_keeps_wrapper_usable();
        javascript_listener_on_destroyed_subtree_is_invalidated_before_runtime_cleanup();
        javascript_cached_destroyed_node_wrappers_are_invalidated();
        javascript_detached_node_budget_is_bounded();
        javascript_click_listener_mutates_dom();
        javascript_generic_click_event_does_not_fake_mouse_coordinates();
        javascript_event_prevent_default_and_remove_listener_work();
        javascript_event_object_survives_after_dispatch_as_snapshot();
        javascript_event_handler_properties_work();
        javascript_form_properties_mutate_control_state();
        javascript_dom_attribute_and_remove_ergonomics_work();
        javascript_tabindex_and_autofocus_reflection_work();
        javascript_form_idl_reflection_subset_works();
        javascript_embedded_ui_helpers_support_event_delegation();
        javascript_query_selector_subset_works();
        javascript_class_name_reflects_class_attribute();
        javascript_id_and_document_body_reflect_dom_attributes();
        javascript_class_list_subset_mutates_class_attribute();
        javascript_bounding_client_rect_uses_numeric_frame_snapshots();
        javascript_dataset_writes_reflect_bounded_data_attributes();
        javascript_bounding_client_rect_snapshot_budget_is_bounded();
        javascript_element_style_hidden_and_disabled_properties_work();
        javascript_standard_reflected_attributes_work();
        javascript_document_ready_state_and_element_click_work();
        javascript_small_document_and_text_idl_tail_works();
        document_static_collections_work();
        element_specific_reflected_idl_properties_work();
        javascript_element_style_extended_properties_work();
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
        javascript_service_objects_are_invalidated_after_clear_and_rebind();
        cleared_services_reclaim_late_runtime_completion_only();
        javascript_runtime_respects_timer_and_listener_budgets();
        javascript_runtime_honors_zero_host_budgets_and_deduplicates_listeners();
        javascript_system_state_exposes_web_adjacent_subset();
        javascript_host_data_snapshot_is_explicit_and_filtered();
        javascript_location_hash_routes_within_one_app();
        javascript_date_now_uses_host_time();
        javascript_geolocation_uses_bound_location_service();
        javascript_form_submission_and_form_data_work();
        javascript_form_data_budget_is_bounded();
        javascript_control_validity_subset_works();
        javascript_canvas_2d_is_optional_and_lazy();
        javascript_canvas_quadratic_curve_to_strokes_path();
        javascript_canvas_bezier_curve_to_strokes_path();
        javascript_canvas_translate_is_pixel_aligned_and_saved();
        javascript_canvas_reset_transform_clears_translation();
        javascript_canvas_radial_gradient_uses_concentric_two_stop_subset();
        javascript_retained_canvas_gradient_is_safe_after_canvas_clear();
        javascript_canvas_draw_image_copies_and_scales_canvas_source();
        javascript_dialog_modal_subset_is_bounded_and_cancellable();
    } catch (const std::exception& error) {
        std::cerr << "script runtime test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "script runtime tests passed\n";
    return 0;
}
