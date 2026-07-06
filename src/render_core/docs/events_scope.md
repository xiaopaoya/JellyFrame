# Events and Hit Testing Scope

> Last updated: 2026-07-07; Applies to: 0.5.0-dev


JellyFrame separates input plumbing from the core engine:

- The core engine owns hit testing, event objects, listener storage and event
  dispatch.
- Platform shells translate native input, such as Win32 messages or LVGL input
  callbacks, into core events.

This keeps the embeddable engine independent from Windows, MCU GPIO, touch
controllers and UI driver libraries.

## References

- WHATWG DOM Standard: `Event`, `EventTarget`, event path construction,
  capturing, target and bubbling phases.
  https://dom.spec.whatwg.org/
- W3C UI Events: `MouseEvent`, `WheelEvent` and legacy mouse event concepts.
  https://www.w3.org/TR/uievents/
- CSSOM View: `elementFromPoint()` is the closest web-facing model for viewport
  coordinate hit testing.
  https://www.w3.org/TR/cssom-view-1/
- Pointer Events and Touch Events remain future integration points.

## Implemented

- `EventTarget` storage on DOM `Node`.
- Lazy listener storage so nodes without listeners do not allocate listener
  tables.
- `add_event_listener()` and `remove_event_listener()` with listener ids.
- `add_event_listener_bounded()` for embedded hosts that register C++ listeners
  directly and want to enforce `HostBudgets::max_event_listeners` outside the
  script runtime.
- `Event`, `MouseEvent` and `WheelEvent` data objects.
- Platform-neutral `InputController` for pointer move/down/up and wheel input.
- `prevent_default()`, `stop_propagation()` and
  `stop_immediate_propagation()`.
- DOM event dispatch through:
  - capture phase
  - target phase
  - bubble phase
- Layer-aware hit testing with:
  - reverse paint/layer order
  - z-index layer ordering from the existing layer tree
  - overflow clipping
- text-node hits normalized to the nearest element target
- Input synthesis for `mouseover`, `mouseout`, `mousemove`, `mousedown`,
  `mouseup`, `click` and `wheel`.
- Default disclosure action for `summary` inside `details`: an uncanceled
  pointer/focused activation click toggles the parent `details` `open`
  attribute and dispatches a plain `toggle` event. `ToggleEvent` and
  `details[name]` grouping are not implemented.
- Hover, active and focus state tracking inside the core input controller.
- `StyleResolver` extracts dynamic pseudo-class hints from the stylesheet.
  Input state changes mark style/layout dirty only when selectors use the
  matching `:hover`, `:active`, `:focus` or `:focus-within` subset. Pages
  without those selectors still receive events, but pointer hover alone does
  not repaint them.
- A Windows validation shell that translates Win32 mouse/wheel messages into
  `InputController` calls.
- The Windows shell performs a simple viewport scroll default action after
  wheel dispatch. The core remains platform-neutral and does not own OS scroll
  state.

## Deliberate Cuts

- Direct C++ `add_event_listener()` is a host-trusted path. Embedded ports that
  expose third-party or dynamic native listener registration should use
  `add_event_listener_bounded()` or a stricter runtime wrapper.
- No shadow DOM, slots or composed paths.
- No default form, anchor, editing or selection behavior beyond the bounded
  `details`/`summary` disclosure action above.
- No `pointer-events` CSS property.
- No touch or pointer capture.
- No transformed coordinate hit testing.
- No keyboard dispatch yet.
- Input state changes mark style/layout dirty only for supported dynamic
  pseudo-classes that actually appear in the stylesheet. Script/event DOM
  mutations use the normal DOM dirty flags.

## Not Yet Covered

The event scope above is the supported contract. Do not rely on the following
behaviors yet:

- Keyboard event objects and dispatch.
- Paint-only dirty rectangles for every dynamic pseudo-class change.
- Browser-like default actions for anchors, forms and editable controls.
- Pointer capture, transformed-coordinate hit testing or full Pointer Events.
