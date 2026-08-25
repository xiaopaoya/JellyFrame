# JellyFrame Documentation Index

> Last updated: 2026-08-25; Applies to: 0.6.0-dev

This directory contains technical documents: module behavior, supported subsets,
interfaces and host contracts. These docs should stay close to the code.

Each public Markdown document starts with a short freshness line:
`Last updated: YYYY-MM-DD; Applies to: VERSION`. Treat it as the quick signal
for whether a document needs another pass after code changes.

## Choose By Role

| You are | Read first | Then use |
| --- | --- | --- |
| App author | `../HOW_TO_START.md`, `app_author_guide.md` | capability table, recipes, target presets and `../tools/README.md` |
| Render Core contributor | `engine_architecture.md`, `../src/render_core/README.md` | scope docs beside the affected module and the feature matrix |
| Port maintainer | `porting_work_guide.md`, `embedded_hal_api.md` | `jfdp_v1_port_acceptance.md` for a developer transport, then `device_image_lifecycle_port_acceptance.md` for a Developer Image, plus `../ports/<port>/README.md`, framebuffer/text docs and port-owned reports |
| Runtime/host maintainer | `../src/app_runtime/README.md` | lifecycle, packaging, services and authorization contracts |
| Device-runtime maintainer | `device_runtime.md` | official board profiles, device protocol and port integration |
| Script/runtime maintainer | `../src/script/README.md` | scripting scope and cross-task ownership contract |
| Compatibility or tooling maintainer | capability matrix and searchable HTML/CSS tables | `../tools/README.md`, schemas and tool regression tests |

## First-Time Reading Order

1. [../HOW_TO_START.md](../HOW_TO_START.md)
2. [engine_architecture.md](engine_architecture.md)
3. [app_author_guide.md](app_author_guide.md) if you are writing apps.
4. [app_author_recipes.md](app_author_recipes.md) for copyable small-screen UI patterns.
5. [developer_capability_matrix.md](developer_capability_matrix.md)
6. [component_compatibility_matrix.md](component_compatibility_matrix.md) and [render_core_release_policy.md](render_core_release_policy.md) if you are splitting or consuming a build boundary.
7. [pre_1_0_evolution_policy.md](pre_1_0_evolution_policy.md) before changing a public or module boundary.
8. [html_living_standard_support_table.md](html_living_standard_support_table.md) or [csswg_support_table.md](csswg_support_table.md) if you need to search Web syntax support.
9. [app_packaging.md](../src/app_runtime/docs/app_packaging.md) if you are building local app packages.
10. The module document for the area you want to use, port or inspect.

## Technical Documents

### Core Pipeline

- [engine_architecture.md](engine_architecture.md): browser-like layers and
  current tradeoffs.
- [html_tokenizer_scope.md](../src/render_core/docs/html_tokenizer_scope.md): tokenizer subset,
  recovery rules and intentionally dropped states.
- [html_tree_builder_scope.md](../src/render_core/docs/html_tree_builder_scope.md): DOM tree-building
  subset and low-end device limits.
- [html_parser_architecture.md](../src/render_core/docs/html_parser_architecture.md): parser structure,
  naming and performance rules.
- [css_parser_scope.md](../src/render_core/docs/css_parser_scope.md): CSS syntax, at-rule and selector
  subset.
- [cssom_scope.md](../src/render_core/docs/cssom_scope.md): cascade and computed-style policy.
- [render_tree_scope.md](../src/render_core/docs/render_tree_scope.md): render tree construction and
  layout-facing rules.
- [layer_tree_scope.md](../src/render_core/docs/layer_tree_scope.md): layer reasons, clipping and
  degradation policy.
- [software_renderer_scope.md](../src/render_core/docs/software_renderer_scope.md): CPU renderer,
  display commands and deliberate cuts.

### Interaction And Runtime

- [events_scope.md](../src/render_core/docs/events_scope.md): hit testing, DOM-style event dispatch and
  input cuts.
- [scripting_scope.md](../src/script/docs/scripting_scope.md): current JerryScript support and
  unsupported browser APIs.
- [run_loop_contract.md](../src/render_core/docs/run_loop_contract.md): host run-loop order, dirty flags
  and repaint planning.

### Host, Embedded And Text

- [host_abstraction.md](host_abstraction.md): thin host boundary for resources,
  time, framebuffer, text and budgets.
- [app_packaging.md](../src/app_runtime/docs/app_packaging.md): app package format, manifest and
  resource-bundle workflow.
- [app_lifecycle.md](../src/app_runtime/docs/app_lifecycle.md): app-author lifecycle behavior
  for install, launch, suspend, resume, service completion and recovery.
- [installed_bundle_binding.md](../src/app_runtime/docs/installed_bundle_binding.md):
  committed `.jfapp` lease ownership, Runtime launch ordering and protected
  launcher fallback for a Device OS.
- [device_runtime.md](device_runtime.md): product boundary and delivery plan for
  official developer images, device deployment and tooling.
- [device_tool_provider_contract.md](device_tool_provider_contract.md): future
  Device OS client boundary for Runtime CLI and VS Code; not yet implemented.
- [device_image_manifest.md](device_image_manifest.md): immutable Developer
  Image identity, provenance and provider-compatibility contract.
- [jfdp_v1_port_acceptance.md](jfdp_v1_port_acceptance.md): byte-stream,
  malformed-frame and evidence gate for the first physical developer transport.
- [device_image_lifecycle_port_acceptance.md](device_image_lifecycle_port_acceptance.md):
  persistent staging, registry publication and launcher-recovery gate for a
  first Developer Image.
- [authorized_file_broker.md](../src/app_runtime/docs/authorized_file_broker.md): host-owned
  authorized file access boundary for file-manager and system-component flows.
- [host_optional_services.md](../src/app_runtime/docs/host_optional_services.md): optional host-service
  contract for images, audio, lightweight video, network data and installable
  bundles.
- [../tools/vscode-jellyframe/README.md](../tools/vscode-jellyframe/README.md):
  JellyFrame's VS Code extension for app authors.
- [../tools/schemas/jellyframe.app.schema.json](../tools/schemas/jellyframe.app.schema.json):
  JSON Schema for `jellyframe.app.json`.
- [embedded_hal_api.md](embedded_hal_api.md): board-side API checklist.
- [embedded_framebuffer_backend.md](../src/render_core/docs/embedded_framebuffer_backend.md): caller-owned
  framebuffer conversion and flush contract.
- [text_backend.md](../src/render_core/docs/text_backend.md): text measurement/painting API and font
  workflow.
- [embedded_optimization_notes.md](embedded_optimization_notes.md): current
  optimization choices and benchmark baseline.
- [porting_work_guide.md](porting_work_guide.md): staged board-port work guide.

### Developer-Facing Feature Contract

- [app_author_guide.md](app_author_guide.md): short app-author contract,
  recipes and warning fixes.
- [app_author_capability_table.md](app_author_capability_table.md): quick
  app-author can-I-use-it table for HTML/CSS/JS/resources.
- [app_author_recipes.md](app_author_recipes.md): copyable small-screen
  component recipes for buttons, cards, scroll lists and bottom navigation.
- [developer_capability_matrix.md](developer_capability_matrix.md): the primary
  can-do/cannot-do contract.
- [component_compatibility_matrix.md](component_compatibility_matrix.md): Core,
  Runtime, Device OS and package-consumer compatibility evidence.
- [render_core_release_policy.md](render_core_release_policy.md): Core extraction,
  release, lock, profile and history-preservation policy.
- [html_living_standard_support_table.md](html_living_standard_support_table.md):
  full searchable HTML Living Standard support table.
- [html_living_standard_support_table.csv](html_living_standard_support_table.csv):
  machine-readable version of the same table for editor/tooling integrations.
- [csswg_support_table.md](csswg_support_table.md): full searchable CSSWG
  support table.
- [csswg_support_table.csv](csswg_support_table.csv): machine-readable version
  of the CSS table for editor/tooling integrations.
- [jelly_ui_design_system.md](jelly_ui_design_system.md): the gel/jellyfish
  control and motion design system.
- [versioning.md](versioning.md): versioning and release discipline.
- [pre_1_0_evolution_policy.md](pre_1_0_evolution_policy.md): breaking-change
  policy before the first stable release.
## File Naming

English docs use the base filename. Chinese docs use `_zh`, for example
`engine_architecture.md` and `engine_architecture_zh.md`.
