# Scripting Scope

WearWeb scripting is intentionally staged. The engine should become useful for
embedded app UI without inheriting the full browser API surface.

## M2 Support

- Optional `wearweb_script` target behind `WEARWEB_BUILD_SCRIPTING=ON`.
- `JERRYSCRIPT_ROOT` can point at an official JerryScript checkout, for example
  `third_party/jerryscript`.
- JerryScript lifecycle owned by `JerryScriptRuntime`.
- `eval(source, source_name)` for classic JavaScript source text.
- Stringified success result and stringified exception result.
- Repeated initialize/shutdown in one process, one active runtime at a time.
- `wearweb_pseudo_browser --script file.js` for desktop acceptance.

## M3 Support

- Global `window` and `document` objects when a host binds a native DOM tree.
- `document.getElementById(id)`.
- `document.createElement(tag)`.
- `document.createTextNode(text)`.
- `node.appendChild(child)` with detached-node ownership transfer.
- `node.removeChild(child)` while keeping the returned wrapper usable.
- `element.setAttribute(name, value)`.
- `element.getAttribute(name)`.
- `node.textContent` getter/setter.
- `wearweb_pseudo_browser --script file.js` binds the parsed page DOM before
  script execution, so script mutations affect the rendered output.

## M4 Support

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

## M5 Support

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
- The scripting pseudo browser and Win32 shell can run small app-style examples
  from `examples/app_cases`.

## Not Supported Yet

- DOM selectors beyond `getElementById`.
- Timers, promises/job pumping beyond what JerryScript itself performs inside
  one evaluation.
- Inline `<script>` and script loading from HTML.
- Networking, modules, dynamic import, storage, canvas and Web Components.

## Embedded Policy

The scripting bridge must stay optional, explicit and bounded:

- Core HTML/CSS/rendering must build without JerryScript.
- Native wrappers must not own DOM nodes.
- Every retained `jerry_value_t` must have a clear release path.
- Script-driven redraws should consume dirty flags and coalesce work.
- APIs are added only when the C++ core can honor their behavior predictably.
