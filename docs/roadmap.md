# Roadmap

## Milestone 1: Static document core

- HTML subset: document nodes, text, common block and inline tags
- CSS subset: simple selectors and a small property set
- Layout: block flow and text measurement abstraction
- Rendering: sparse layer tree, clipping, and display list with rectangles, text
  and image placeholders

Status: mostly complete, with a broader app-oriented subset than originally
planned.

## Milestone 2: Embedded rendering backend

- Software framebuffer backend: available for validation
- Dirty rectangle repaint from layer invalidation
- Font atlas or platform text backend: Win32/GDI exists for validation; embedded
  backend is still needed
- Pointer/touch input routing: pointer/wheel core exists; wearable focus/touch
  adapters are still needed

## Milestone 3: App runtime

- JerryScript integration: optional scripting build available
- DOM mutation APIs: available
- Timer/event loop: host-pumped timers available
- Classic document script loading: available in scripting example shells
- Fetch/resource abstraction
- Device capability APIs

## Milestone 4: Wearable UI features

- Small-screen viewport model
- Focus/navigation model for crown/buttons/touch
- Power-aware animation scheduling
- App packaging format

## Recommended Next Order

1. Dirty rectangle invalidation and `HostFrameSink` presentation.
2. Deployable embedded framebuffer backend.
3. Platform text backend beyond Win32/GDI.
4. Wearable focus/navigation model for touch, buttons and crown.
5. Resource and device capability APIs.
