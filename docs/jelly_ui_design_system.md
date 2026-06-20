# JellyFrame Gel Interface System

This document defines JellyFrame's first native visual and motion system. It
interprets the `Jelly` name through the shared qualities of jellyfish and gel:
soft membranes, translucent thickness, inner glow, buoyancy and gentle pressure
response. The goal is not to clone Material, Cupertino, Fluent or large vendor
design systems. The goal is a JellyFrame-specific embedded UI language that is
shippable, testable and degradable on the current engine.

## Goals

- Make JellyFrame apps visually recognizable as soft gel interfaces.
- Use only features the current runtime can reliably honor.
- Give every visual effect a low-cost fallback.
- Prefer paint/compositor motion over per-frame layout.
- Keep motion budgeted, optional and power-aware.

## Runtime Baseline

This system must match current project capabilities:

- Safe: `rgba()`, hex colors, named basics, `background-color`, simple
  `background` color extraction, single-value `border-radius`, borders,
  padding, margin, approximate `box-shadow`, `opacity`, `transform:
  translate()/scale()`, CSS `transition`, and dynamic states such as `:hover`,
  `:active`, `:focus`, `:focus-within`, `:checked` and `:disabled`.
- Safe: direct CSS custom property usage through `var(--token)` and fallback.
- Safe: bounded CSS `transition` and `transition-*` lists. Each style keeps at
  most four transition entries. Current animatable properties are `opacity`,
  `transform`, `background-color` and `color`.
- Safe with restraint: bounded `@keyframes` / `animation-*` from/to animations
  over `opacity`, `transform: translate()/scale()`, `background-color` and
  `color`. Use them for small persistent loading/pulse states, not layout
  motion.
- Safe: software compositor support for layer `translate()/scale()`.
  Translation is rounded to integer pixels; scale uses nearest-neighbor
  sampling around layer bounds center.
- Safe in scripting builds: host-pumped one-shot `requestAnimationFrame()` and
  `cancelAnimationFrame()`.
- Not safe for v1: `backdrop-filter`, CSS `filter`, `mix-blend-mode`, SVG
  texture, Canvas-rendered widgets, layout-property animation, rotate, skew,
  matrix, perspective and full `transform-origin`.

## Design Kernel

The gel language has four core ideas:

| Idea | Visual Meaning | Runtime Expression |
| --- | --- | --- |
| Membrane | Soft translucent shell | `rgba` background, thin border, rounded box |
| Core Glow | Inner light and thickness | child highlight, bright edge |
| Buoyancy | Light floating motion | `opacity + translateY` transition, rare rAF |
| Soft Press | Pressure and recovery | `:active` `scale()` and color shift |

Short principle: **not glassmorphism, not frosted blur, but hydrated gel.**

## Color Tokens

Default theme: `native-jelly`.

```json
{
  "theme": "native-jelly",
  "surface.light": "#F6FBFF",
  "surface.dark": "#0F1A29",
  "ink": "#101820",
  "mutedInk": "rgba(16, 24, 32, 0.68)",
  "gel": "rgba(156, 224, 247, 0.45)",
  "gel.thick": "rgba(82, 170, 204, 0.22)",
  "gel.highlight": "rgba(255, 255, 255, 0.28)",
  "gel.edge": "rgba(100, 200, 235, 0.60)",
  "glow.coral": "rgba(255, 126, 103, 0.55)",
  "glow.lime": "rgba(183, 243, 107, 0.55)",
  "state.success": "rgba(115, 222, 184, 0.50)",
  "state.warning": "rgba(255, 204, 136, 0.50)",
  "state.danger": "rgba(236, 134, 164, 0.50)"
}
```

Optional themes:

- `peach-gel`: warm lifestyle and rest-oriented apps.
- `lime-gel`: motion, timers and monitoring apps.
- `deep-jelly`: dark ocean bioluminescent mode.

Suggested CSS variables:

```css
:root {
  --jf-surface: #f6fbff;
  --jf-ink: #101820;
  --jf-muted-ink: rgba(16, 24, 32, 0.68);
  --jf-gel: rgba(156, 224, 247, 0.45);
  --jf-gel-thick: rgba(82, 170, 204, 0.22);
  --jf-gel-highlight: rgba(255, 255, 255, 0.28);
  --jf-gel-edge: rgba(100, 200, 235, 0.60);
  --jf-coral: rgba(255, 126, 103, 0.55);
  --jf-lime: rgba(183, 243, 107, 0.55);
}
```

## Material Model

Runtime v1 uses three layers and avoids real blur or blend modes.

### Gel Base

```css
.jf-gel {
  background-color: var(--jf-gel);
  color: var(--jf-ink);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 14px;
  box-shadow: 0 0 12px rgba(100, 200, 235, 0.20);
}
```

### Core Highlight

Use real child elements instead of `backdrop-filter`.

```html
<button class="jf-button">
  <span class="jf-core"></span>
  <span class="jf-label">Start</span>
</button>
```

```css
.jf-button {
  position: relative;
  overflow: hidden;
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
}

.jf-core {
  position: absolute;
  left: 10px;
  right: 10px;
  top: 3px;
  height: 8px;
  background-color: var(--jf-gel-highlight);
  border-radius: 999px;
}

.jf-label {
  position: relative;
}
```

### Edge Thickness

Use border and a soft outer glow. Current runtime should treat irregular corner
geometry as a design-source idea, not as a required v1 effect.

```css
.jf-panel {
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 18px;
  box-shadow: 0 0 16px rgba(82, 170, 204, 0.18);
}
```

## Components

### Jelly Button

```css
.jf-button {
  min-height: 36px;
  padding: 8px 14px;
  color: var(--jf-ink);
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: transform 160ms ease-out, background-color 160ms ease-out,
              opacity 160ms ease-out, color 160ms ease-out;
}

.jf-button:focus,
.jf-button:hover {
  background-color: rgba(156, 224, 247, 0.58);
}

.jf-button:active {
  transform: translate(0px, 1px) scale(0.96);
  background-color: var(--jf-gel-thick);
}

.jf-button[disabled],
.jf-button:disabled {
  opacity: 0.45;
}
```

### Jelly Panel

```css
.jf-panel {
  padding: 12px;
  background-color: var(--jf-gel);
  color: var(--jf-ink);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 18px;
  transition: opacity 220ms ease-out, transform 220ms ease-out,
              background-color 220ms ease-out;
}

.jf-panel.is-entering {
  opacity: 0;
  transform: translate(0px, 8px) scale(0.98);
}

.jf-panel.is-visible {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

### Jelly Input

```css
.jf-input {
  min-height: 34px;
  padding: 7px 10px;
  color: var(--jf-ink);
  background-color: rgba(255, 255, 255, 0.54);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: background-color 160ms ease-out, color 160ms ease-out;
}

.jf-input:focus {
  background-color: rgba(255, 255, 255, 0.78);
}

.jf-input.is-danger {
  border-color: var(--jf-coral);
  background-color: rgba(236, 134, 164, 0.18);
}
```

### Jelly Switch

```html
<button class="jf-switch is-on" role="switch" aria-checked="true">
  <span class="jf-switch-thumb"></span>
</button>
```

```css
.jf-switch {
  width: 48px;
  height: 28px;
  padding: 3px;
  background-color: rgba(82, 170, 204, 0.22);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 999px;
  transition: background-color 180ms ease-out;
}

.jf-switch-thumb {
  display: block;
  width: 20px;
  height: 20px;
  background-color: rgba(255, 255, 255, 0.82);
  border-radius: 999px;
  transition: transform 180ms ease-out, background-color 180ms ease-out;
}

.jf-switch.is-on {
  background-color: var(--jf-lime);
}

.jf-switch.is-on .jf-switch-thumb {
  transform: translate(20px, 0px) scale(1.03);
}
```

### Tide Progress

```css
.jf-progress {
  height: 10px;
  background-color: rgba(82, 170, 204, 0.20);
  border-radius: 999px;
  overflow: hidden;
}

.jf-progress-value {
  height: 10px;
  background-color: var(--jf-gel-edge);
  border-radius: 999px;
  transition: transform 180ms linear, background-color 180ms ease-out;
}
```

### Gel Dialog

```css
.jf-dialog {
  padding: 14px;
  background-color: var(--jf-gel);
  border: 1px solid var(--jf-gel-edge);
  border-radius: 20px;
  opacity: 0;
  transform: translate(0px, 10px) scale(0.96);
  transition: opacity 240ms ease-out, transform 240ms ease-out;
}

.jf-dialog.is-open {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

## Motion System

Use CSS transition first. Use rAF only when an interaction needs explicit
per-frame control.

| Token | Duration | Properties | Use |
| --- | --- | --- | --- |
| `jelly.press` | 140-180ms | `transform`, `background-color`, `opacity` | buttons, keys, switch thumb |
| `jelly.float-in` | 200-260ms | `opacity`, `transform` | dialog, toast, popover |
| `jelly.settle` | 180-240ms | `transform` | switch, slider, small card settle |
| `jelly.pulse-core` | 900-1400ms | `opacity`, `background-color` | rare active/loading state |

Supported timing functions are `linear`, `ease`, `ease-in`, `ease-out` and
`ease-in-out`. Do not require `cubic-bezier()` in v1.

```css
.jf-toast {
  opacity: 0;
  transform: translate(0px, 8px) scale(0.98);
  transition: opacity 220ms ease-out, transform 220ms ease-out;
}

.jf-toast.is-visible {
  opacity: 1;
  transform: translate(0px, 0px) scale(1);
}
```

### rAF Rules

- rAF callbacks are one-shot and must be re-registered if another frame is
  needed.
- Hosts can set animation callback/FPS budgets to zero.
- Callbacks should mutate a small number of DOM/style states only.
- Screen-off, background and low-power profiles must stop nonessential motion.

## Forbidden Or Degraded Features

Forbidden as required v1 effects:

- `backdrop-filter`
- `filter`
- `mix-blend-mode`
- SVG textures or SVG-only widget structure
- Canvas-rendered standard controls
- Layout-property animation and full CSS animation semantics
- rotate, skew, matrix and perspective transforms
- irregular per-corner radius as a required effect

Allowed in source design only when a runtime fallback exists:

- Organic silhouettes degrade to a single-radius panel.
- Real blur degrades to translucent color and approximate shadow.
- Noise or bubble textures degrade to static highlight children.
- Radial expansion degrades to `opacity + translate + scale`.

## Layout And Density

- Primary targets are compact viewports such as 300x300, 320x240 and 390x640.
- Spacing should stay stable even if it is not a strict 8px grid. Recommended
  values: 4, 6, 8, 12, 16.
- Minimum touch target height should be 32px, preferably 36px on touch devices.
- Do not make every region a floating card. JellyFrame apps still need clear
  hierarchy and low power behavior.

## Theme Package Shape

Suggested future files:

```text
tools/templates/themes/jelly/
  jelly.tokens.json
  jelly.css
  components/
    button.html
    input.html
    switch.html
    slider.html
    progress.html
    dialog.html
  diagnostics/
    jelly_theme_rules.json
```

Future diagnostics should report forbidden properties, unsupported animation
properties, excessive persistent motion, and missing disabled/focus/active
states.

## Implementation Order

1. Add `jelly.tokens.json` and `jelly.css` with safe v1 features only.
2. Build Button, Panel, Input, Switch and Progress first.
3. Provide 300x300 round target sample pages for each component.
4. Render through `jellyframe_pseudo_browser` and inspect BMP/PPM output.
5. Use `jellyframe_win32_browser` to validate hover, active, focus and
   transition behavior.
6. Teach diagnostics to catch forbidden Jelly UI properties and animation
   budget risks.
7. Add rAF loading/pulse templates only after static and transition paths are
   stable.

## Acceptance Checklist

- Default, hover, active, focus and disabled states exist.
- No forbidden v1 features are required for the visual design.
- Main motion changes only `opacity`, `transform`, `background-color` or
  `color`.
- Standard controls are not Canvas-rendered.
- Text fits in compact viewports.
- Final animated state matches the static target state.
- UI remains usable with persistent motion disabled.
- Package report has no missing resources, font coverage failures or budget
  overages.

## Relationship To The Capability Matrix

The capability matrix defines what JellyFrame can do. This document defines how
JellyFrame should look and move. When they conflict, the capability matrix and
source code win.
