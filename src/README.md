# Source

Platform-neutral JellyFrame source code.

- `render_core/`: HTML/CSS parsing, DOM, style, layout, layer, paint,
  framebuffer, events, input, diagnostics and neutral host-facing render
  contracts.
- `app_runtime/`: platform-neutral app lifecycle helpers and optional
  host-service queues for installable apps, async network/media/storage work
  and host handles.
- `script/`: optional JerryScript binding layer.

`render_core` must remain free of OS, filesystem, network, JavaScript engine and
hardware dependencies. `app_runtime` may define neutral contracts for those
services, but real I/O still belongs in tools, shells, ports or host adapters.
