# Native contextual Desktop and package preflight follow-up

Last updated: 2026-11-18; Applies to: 0.6.0-dev.

## Desktop contextual route

Desktop explicitly accepts the jfsp2-block-v12 declaration through its contextual
loader. It owns the authenticated RuntimeShapedTextBinding, Core contextual
LayoutResult and typed LayerCapture through painting and input. Resource bounds,
local package SHA256 identities, packet identity checks and staged framebuffer
publication remain on that route. The scalar package loader retains explicit
pipeline refusal. Local resource identities assume operator-selected local
package bytes and do not establish physical signed-install trust.

The regression exercises directory and complete bundle entry, nested contextual
layout and exact pixels, native click targeting, invalid declaration/resource/
packet/budget refusal, retained replay after source retirement and final Runtime
wrapper release through a weak handle. It also uses the actual shell and a
classic script which requests geometry before the first completed frame.
Two callbacks validate the previous completed rectangle and mutate text/width.
All three accepted captures match independently built contextual frames.

The Desktop route now publishes its completed contextual layout through the
existing script snapshot API after clamping scroll. The snapshot helper chooses
the contextual LayoutResult tree and applies the same instance and client-offset
checks used by the scalar route. This repairs the previously absent
getBoundingClientRect snapshots. Snapshot reads retain the existing asynchronous
per-wrapper registration contract.

## Package preflight and scalar resources

Native image loading shares bounded BMP validation and Core fractional sampling
between Desktop and pseudo-browser. Preflight resource roots retain the package
entry URL for image and stylesheet lookup. Linked package stylesheets now use
the common package URL loader when --resource-root is supplied. A nested HTML
entry regression checks both /theme.css and a relative stylesheet by exact
painted pixels, then checks parent traversal rejection. Standalone inputs retain
their existing filesystem stylesheet context.

CLI SVG staging keeps the transformed package alive through pipeline checks,
uses its entry/resources and selected preview viewport, reuses nested staging,
and releases the temporary package on normal and return-code failure paths.
Contextual CLI preflight still invokes the scalar pseudo-browser and requires
an explicit contextual implementation in a subsequent slice.

The separate named-family correction is commit5c545f67. ABI9 DOM font requests
use CSS family hashes, while ABI4 uses the existing resolver. The app-host test
now verifies a named resource differs from the default resource. Unchanged
native glyph tests produce system bold228 and named regular180 ink pixels in
both configurations.

## Tests and remaining work

With installed Core source641/hash760a1b68, six targeted Runtime CTests pass,
covering contextual Desktop, app runtime, native Core route, pipeline visual
diagnostics, named-family glyph ink and script-task runtime. ABI4 app-runtime,
pipeline visual and named-family glyph tests pass3/3. The preceding named-family
matrix additionally passed ABI9 three and ABI4 two targeted suites.

The three source-tool suites pass137 unittest cases for production0.9.0 and
137 for the explicit0.6.2 candidate override, with two Windows symlink-privilege
skips per configuration. Valid synthetic manifests derive the active line.
Intentional wrong-version checks remain. Production manifest/docs assertions
derive the actual repository lock with the test override temporarily cleared.
The three source-test changes are synchronized into the isolated candidate
snapshot. Production metadata consistency checks expose that snapshot's mixed
source metadata. Broader CLI/sample staging still requires a coherent entry.

The final broad candidate Runtime run passes62/87, with25 failed CTests.
Version/snapshot harness failures and actual product UnsupportedText failures
remain open. Weather diagnostic copies isolate an additional failure to the
image-containing scope. The image-free static page, eyebrow, facts and tabs
render. Removing image radius or margin alone does not repair the image scope.
The original product tests remain failed. Full/cropped/OFF contextual/signed
qualification must carry forward to the new Core before replacing the frozen
f35f1f77 baseline. Physical target resources, latency, signed install/recovery,
VM cleanup/restart and reviewed delivery remain open.

Evidence is under E:/Experiments/migration-anonymous-text-items-20261118,
E:/Experiments/migration-native-crash-font-20261118 and
E:/Experiments/migration-native-version-fixtures-20261118.
Earlier failed tests, source identities and diagnostic traces are retained.
