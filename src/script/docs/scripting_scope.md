# Scripting Scope

JellyFrame scripting is intentionally small and optional. The engine should
become useful for embedded app UI without inheriting the full browser API
surface.

## Runtime Shell

- Optional `jellyframe_script` target behind `JELLYFRAME_BUILD_SCRIPTING=ON`.
- `JERRYSCRIPT_ROOT` can point at an official JerryScript checkout, for example
  `third_party/jerryscript`.
- JerryScript lifecycle owned by `JerryScriptRuntime`.
- `eval(source, source_name)` for classic JavaScript source text.
- Stringified success result and stringified exception result.
- Repeated initialize/shutdown in one process, one active runtime at a time.
- `jellyframe_win32_browser --script file.js` for desktop acceptance.

## DOM Binding

- Global `window` and `document` objects when a host binds a native DOM tree.
- `document.getElementById(id)`.
- `document.createElement(tag)`.
- `document.createTextNode(text)`.
- `node.appendChild(child)` with detached-node ownership transfer.
- `node.removeChild(child)` while keeping the returned wrapper usable.
- Detached nodes are runtime-owned through `DomOwner` while not attached to the
  document. `HostBudgets::max_detached_dom_nodes` bounds script-created and
  removed nodes so embedded apps cannot grow this pool without limit.
- `element.setAttribute(name, value)`.
- `element.getAttribute(name)`.
- `node.textContent` getter/setter.
- `jellyframe_win32_browser --script file.js` binds the parsed page DOM before
  script execution, so script mutations affect the rendered output.

## Events

- `node.addEventListener(type, callback, options)`.
- `node.removeEventListener(type, callback)`.
- Listener options: boolean capture, plus object `{ capture, once }`.
- Event object fields: `type`, `target`, `currentTarget`, `eventPhase`,
  `bubbles`, `cancelable`, `defaultPrevented`, mouse coordinates/buttons,
  modifier keys and wheel deltas when applicable.
- Event object methods: `preventDefault`, `stopPropagation` and
  `stopImmediatePropagation`.
- JavaScript listeners run through the existing C++ capture/target/bubble event
  flow and can mutate the DOM during native input dispatch.
- The Win32 browser shell accepts `--script file.js` in scripting builds and
  rerenders when script event callbacks dirty the DOM.

## Form Controls

- Form-control properties on relevant nodes:
  - `input.value`
  - `textarea.value`
  - `checkbox.checked`
  - `radio.checked`
  - `select.value`
  - `select.selectedIndex`
- Native text input, Backspace, checkbox/radio/select activation and range
  movement update JavaScript-visible control state.
- Native input dispatch fires JS-observable `input` and `change` events through
  the existing C++ event flow.
- JavaScript changes to form state mark the DOM dirty so the host can rerender
  lightweight native-style controls.
- The Win32 shell can run small app-style examples from `samples/apps/loose`.

## Timers

- `setTimeout(callback, ms)` and `clearTimeout(id)`.
- `setInterval(callback, ms)` and `clearInterval(id)`.
- Timer callbacks must be functions. String-eval timers and extra callback
  arguments are intentionally not supported.
- Timers are host-pumped through `JerryScriptRuntime::pump_timers(now_ms,
  max_callbacks)`, so embedded ports provide their own clock source and frame
  budget.
- The Win32 browser shell pumps timers through a desktop `WM_TIMER` and rerenders
  if callbacks dirty the DOM.

## Document Script Loading

- Classic inline `<script>` elements are collected from the parsed document and
  evaluated in DOM order in scripting builds.
- Local external classic scripts (`<script src="...">`) are loaded through a
  shell-provided callback. Core code still performs no file or network I/O.
- `type="module"` and other non-classic script types are skipped.
- `jellyframe_win32_browser` executes document scripts automatically;
  command-line `--script file.js` remains available as an extra desktop
  validation script.
- Network loading, ES modules, dynamic import and the full HTML loading
  algorithm remain intentionally out of scope.

## Planned Runtime Data APIs

Optional network, app-private KV storage and system status events are being
designed under `src/app_runtime/docs/runtime_data_api.md`. The intended V0 shape
is a small `JellyFrame` namespace with callback-based APIs such as
`JellyFrame.fetchText(...)`, `JellyFrame.storage.get(...)` and
`JellyFrame.system.on(...)`. These APIs are not exposed yet; the current
implementation only provides the platform-neutral C++ request/completion queues,
policy gates, mocks and system-event queue.

## Embedded-App DOM Helpers

- Embedded-app DOM helpers:
  - `element.children`
  - `element.parentElement`
  - `element.matches(simpleSelector)`
  - `element.closest(simpleSelector)`
  - `element.dataset` snapshot properties for existing `data-*` attributes
  - `element.style` for a small inline-style property set
  - `element.hidden` and `element.disabled`
- Supported `matches`/`closest` selectors are intentionally small: tag,
  `.class`, `#id`, `[attr]` and `[attr=value]`. Descendant/child combinators
  remain CSS-only for now.
- Native input dispatch exposes `pointerdown`, `pointerup`, `touchstart` and
  `touchend` as mouse-like events for press feedback on wearable shells.
- Disabled form controls do not accept text input, range movement or activation.
- Text-search compatibility scanning is retired. Script-related diagnostics
  should come from the package loader, JerryScript runtime and DOM/event binding
  code paths that actually handled the app.

## Not Supported Yet

- Full selector APIs such as `querySelector` / `querySelectorAll`.
- Dynamic `dataset` property creation or native mutation through new arbitrary keys.
- Promises/job pumping beyond what JerryScript itself performs inside one
  evaluation.
- Networking, modules, dynamic import, storage, system-status JS callbacks,
  canvas and Web Components.

## Embedded Policy

The scripting bridge must stay optional, explicit and bounded:

- Core HTML/CSS/rendering must build without JerryScript.
- Native wrappers must not own DOM nodes.
- Every retained `jerry_value_t` must have a clear release path.
- Detached DOM nodes must remain observable through runtime statistics so ports
  can audit script-heavy UI memory behavior.
- Script-driven redraws should consume dirty flags and coalesce work.
- APIs are added only when the C++ core can honor their behavior predictably.
