# JellyFrame Documentation Index

This directory contains two kinds of documents:

- **Technical documents**: module behavior, supported subsets, interfaces and
  host contracts. These should stay close to the code.
- **Active project/process documents**: current status, roadmap, memory review,
  run-loop policy and changelog. These explain where the project is going.

Historical compatibility reports and completed milestone plans are archived
outside the workspace when they are fully superseded by maintained docs.

## First-Time Reading Order

1. [../HOW_TO_START.md](../HOW_TO_START.md)
2. [engine_architecture.md](engine_architecture.md)
3. [developer_capability_matrix.md](developer_capability_matrix.md)
4. [project_status.md](project_status.md)
5. The module document for the area you want to change.

## Technical Documents

### Core Pipeline

- [engine_architecture.md](engine_architecture.md): browser-like layers and
  current tradeoffs.
- [html_tokenizer_scope.md](html_tokenizer_scope.md): tokenizer subset,
  recovery rules and intentionally dropped states.
- [html_tree_builder_scope.md](html_tree_builder_scope.md): DOM tree-building
  subset and low-end device limits.
- [html_parser_architecture.md](html_parser_architecture.md): parser structure,
  naming and performance rules.
- [css_parser_scope.md](css_parser_scope.md): CSS syntax, at-rule and selector
  subset.
- [cssom_scope.md](cssom_scope.md): cascade and computed-style policy.
- [render_tree_scope.md](render_tree_scope.md): render tree construction and
  layout-facing rules.
- [layer_tree_scope.md](layer_tree_scope.md): layer reasons, clipping and
  degradation policy.
- [software_renderer_scope.md](software_renderer_scope.md): CPU renderer,
  display commands and deliberate cuts.

### Interaction And Runtime

- [events_scope.md](events_scope.md): hit testing, DOM-style event dispatch and
  input cuts.
- [scripting_scope.md](scripting_scope.md): current JerryScript support and
  unsupported browser APIs.
- [run_loop_contract.md](run_loop_contract.md): host run-loop order, dirty flags
  and repaint planning.

### Host, Embedded And Text

- [host_abstraction.md](host_abstraction.md): thin host boundary for resources,
  time, framebuffer, text and budgets.
- [embedded_hal_api.md](embedded_hal_api.md): board-side API checklist.
- [embedded_framebuffer_backend.md](embedded_framebuffer_backend.md): caller-owned
  framebuffer conversion and flush contract.
- [text_backend.md](text_backend.md): text measurement/painting API and font
  workflow.
- [embedded_optimization_notes.md](embedded_optimization_notes.md): current
  optimization choices and benchmark baseline.
- [porting_work_guide.md](porting_work_guide.md): staged board-port work guide.

### Developer-Facing Feature Contract

- [developer_capability_matrix.md](developer_capability_matrix.md): the primary
  can-do/cannot-do contract.
- [versioning.md](versioning.md): versioning and release discipline.

## Active Project / Process Documents

- [project_status.md](project_status.md): current scope boundary, completed work
  and next milestones.
- [roadmap.md](roadmap.md): high-level route.
- [memory_management.md](memory_management.md): current memory behavior and next
  allocator/container work.
- [dom_arena_feasibility.md](dom_arena_feasibility.md): DOM ownership and arena
  feasibility notes.
- [esp32s3_qemu_benchmark.md](esp32s3_qemu_benchmark.md): retained historical
  benchmark data for the ESP32-S3 reference port.
- [../CHANGELOG.md](../CHANGELOG.md): change history.

## Bilingual Maintenance

English docs use the base filename. Chinese docs use `_zh`, for example
`engine_architecture.md` and `engine_architecture_zh.md`. User-facing,
architecture, status, release and onboarding docs should be updated in both
languages in the same change.
