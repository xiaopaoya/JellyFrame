# Source

Platform-neutral JellyFrame source code.

- `core/`: HTML/CSS parsing, DOM, style, layout, layer, paint, framebuffer,
  events, input, pipeline statistics and host-facing contracts.
- `script/`: optional JerryScript binding layer.

The core should remain free of OS, filesystem, network and hardware dependencies.
Those concerns belong in samples, tools, ports or host adapters.
