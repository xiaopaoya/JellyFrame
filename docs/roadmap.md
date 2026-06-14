# Roadmap

## Milestone 1: Static document core

- HTML subset: document nodes, text, common block and inline tags
- CSS subset: simple selectors and a small property set
- Layout: block flow and text measurement abstraction
- Rendering: sparse layer tree, clipping, and display list with rectangles, text
  and image placeholders

## Milestone 2: Embedded rendering backend

- Software framebuffer backend
- Dirty rectangle repaint from layer invalidation
- Font atlas or platform text backend
- Pointer/touch input routing

## Milestone 3: App runtime

- JerryScript integration
- DOM mutation APIs
- Timer/event loop
- Fetch/resource abstraction
- Device capability APIs

## Milestone 4: Wearable UI features

- Small-screen viewport model
- Focus/navigation model for crown/buttons/touch
- Power-aware animation scheduling
- App packaging format
