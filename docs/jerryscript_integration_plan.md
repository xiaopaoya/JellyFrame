# JerryScript Integration Plan

WearWeb should integrate JerryScript as a small host runtime, not as an attempt
to clone a full browser Web API. The binding layer must expose only capabilities
that the core actually supports and must preserve the same embedded constraints
as the C++ engine: bounded memory, explicit ownership, no implicit networking and
predictable redraw scheduling.

## Source Notes

The JerryScript official API examples show the basic embedding lifecycle:
initialize with `jerry_init`, evaluate or parse/run scripts with `jerry_eval` or
`jerry_parse` plus `jerry_run`, check exception values, release every returned
`jerry_value_t` with `jerry_value_free`, and call `jerry_cleanup` at shutdown.
The official extension handler documentation shows how native C functions can be
registered as JavaScript-visible properties. The reference-counting guide is
important for the binding layer because returned `jerry_value_t` values carry
new live references that the host must release.

References:

- <https://jerryscript.net/api-example/>
- <https://jerryscript.net/api-reference/>
- <https://jerryscript.net/ext-reference-handler/>
- <https://jerryscript.net/reference-counting/>
- <https://jerryscript.net/debugger/>

## Current Readiness

Ready:

- M2 runtime shell behind `WEARWEB_BUILD_SCRIPTING=ON`: initialization,
  shutdown, `eval`, source names, result stringification and exception reporting.
- M3 minimal DOM bindings: `window`, `document`, `getElementById`,
  `createElement`, `createTextNode`, `appendChild`, `removeChild`,
  `setAttribute`, `getAttribute` and `textContent`.
- DOM tree construction and tolerant HTML parsing.
- DOM mutation primitives: append/remove child, set/remove attribute, text
  content updates.
- Dirty flags for tree, attribute, text, style and layout invalidation.
- Event dispatch with capture, target and bubble phases.
- Core form-control state for common controls.
- Pointer, wheel, text input and simple key input paths.
- Win32 validation shell that can rerender the same DOM after state changes.

Not ready:

- JavaScript runtime object lifetime model.
- Native-to-JS event listener storage.
- JavaScript wrappers for DOM nodes.
- Minimal task queue and redraw scheduler.
- JavaScript property accessors for form state and DOM attributes.

## Architecture

```text
WearWebScriptRuntime
  owns JerryScript engine lifecycle
  owns wrapper maps and JS callback references
  |
  +-- Window binding
  +-- Document binding
  +-- Element/Text binding
  +-- Event binding
  +-- Timer/task queue
  |
DOM + style + layout + layer + renderer
```

The runtime should be optional at build time:

- `WEARWEB_BUILD_SCRIPTING=OFF` by default until the bridge is stable.
- Core HTML/CSS/rendering remains independent from JerryScript headers.
- The scripting target links JerryScript and depends on `wearweb_core`.

## Binding Principles

- One native `Node*` maps to one JS wrapper while the runtime is alive.
- JS wrappers never own DOM nodes; the DOM tree owns nodes.
- Wrapper finalizers release JerryScript references but do not delete DOM nodes.
- Native event listeners keep explicit `jerry_value_t` references and free them
  when removed or when the runtime is destroyed.
- Every public binding function must return a defined success/error value; no
  uncaught C++ exceptions cross into JerryScript.
- Dirty flags are consumed by the host loop after script execution and event
  callbacks.

## Milestones

### M2: Runtime Shell

Status: implemented as an optional build target. `wearweb_script` links
JerryScript only when `WEARWEB_BUILD_SCRIPTING=ON`; `wearweb_core` stays free of
JerryScript headers and libraries. `wearweb_pseudo_browser --script` can execute
one external script file and print the result or exception for acceptance
testing.

Create `WearWebScriptRuntime` with:

- `initialize()` / `shutdown()`.
- `eval(std::string_view source, std::string_view name)`.
- strict `jerry_value_t` RAII helper for reference release.
- exception-to-log path for script errors.

Validation:

- A script can run and set a global value.
- All returned JerryScript values are released.
- Runtime can initialize and shut down repeatedly in one process.

### M3: Minimal DOM Objects

Status: implemented for synchronous host-driven scripts. Detached nodes created
by JavaScript are owned by the runtime until `appendChild` transfers them into
the native DOM tree; `removeChild` moves ownership back to the runtime so the
returned wrapper remains usable.

Expose:

- `window`
- `document`
- `document.getElementById(id)`
- `document.createElement(tag)`
- `document.createTextNode(text)`
- `node.appendChild(child)`
- `node.removeChild(child)`
- `element.setAttribute(name, value)`
- `element.getAttribute(name)`
- `node.textContent`

Validation:

- JS changes text content and the host observes `DomDirtyText | DomDirtyLayout`.
- JS creates an element, appends it and the pipeline rerenders it.

### M4: Event Bridge

Expose:

- `addEventListener(type, callback)`
- `removeEventListener(id)` or a constrained equivalent
- event object with `type`, `target`, `currentTarget`, `preventDefault`,
  `stopPropagation`

Validation:

- A JS click listener mutates the DOM.
- Event callback references are released when listeners are removed.
- Reentrant event dispatch is either supported or explicitly blocked.

### M5: Form-Control Properties

Expose:

- `input.value`
- `textarea.value`
- `checkbox.checked`
- `radio.checked`
- `select.selectedIndex`

Validation:

- User input updates JS-visible state.
- JS state updates rerender native-lite controls.
- `input` and `change` events can be observed from JS.

### M6: Task Queue and Timers

Expose:

- `setTimeout(callback, ms)`
- `clearTimeout(id)`
- a small task queue pumped by the host shell

Validation:

- Timer callbacks run after host ticks.
- Dirty flags coalesce so repeated JS mutations trigger one rerender.

### M7: Script Loading

Support:

- inline classic `<script>`
- optional shell-provided local script loader callback

Avoid for now:

- network fetch
- ES modules
- dynamic import
- full browser loading algorithm

## First Demo Target

HTML:

```html
<button id="count">0</button>
<script>
  var n = 0;
  document.getElementById("count").addEventListener("click", function () {
    n += 1;
    document.getElementById("count").textContent = String(n);
  });
</script>
```

Expected behavior:

- The page renders the button.
- Clicking dispatches through the C++ event system into JS.
- JS mutates `textContent`.
- Dirty flags cause the host to rerender.
- The button label increments.

## Main Risks

- Leaking `jerry_value_t` references in event listeners or wrapper maps.
- Wrapper lifetime mismatch when DOM nodes are removed.
- Accidentally exposing APIs the core cannot honor.
- Rebuilding too much after every small mutation.
- Expanding into full Web compatibility before the embedded runtime is stable.

## Recommended Next Step

Continue with M4: bridge native event dispatch to JavaScript listeners. Keep the
listener storage explicit, release all retained `jerry_value_t` callbacks and
coalesce dirty flags after event callbacks before rerendering.
