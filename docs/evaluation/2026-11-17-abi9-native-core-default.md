# ABI9 native scalar Core text default and product parity boundaries

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Result

The real Win32 desktop and pseudo-browser now select the scalar Core font
backend by default with the frozen ABI9 public package. Previously ABI9 lacked
the ABI4 canonical-value macro, causing desktop to select GDI and pseudo-browser
to omit Core fonts. An ABI-era capability check now selects the font backend
independently from constructor signature checks. This is scalar JFTP text,
not contextual JFSP2 Desktop integration.

All twelve native tool targets build with installed ABI9 full/cropped and an
ABI4 source compatibility control. The new actual-binary native regression
passes in all three configurations, both directly with retained image evidence
and through CTest. It checks:

- Default/explicit Core text and pseudo-browser exact pixel agreement and
  grayscale coverage, with truthful `core-text` diagnostics.
- Real pointer-triggered JavaScript text mutation and a hover font-size change,
  each matching a fresh Core render pixel for pixel and remaining stable.
- Exact pre-1.0 version refusal, package `textPipeline` declaration refusal
  including explicit null, and no success artifact for refused input.
- ABI9 unsupported scalar text returns a controlled error instead of a
  null dereference or implicit platform-font fallback.

ABI4's broader CLI, visual diagnostics and app-shell workflows also pass.
The CLI's existing source-package symlink test lacks Windows privilege and
prints its existing skip; it is not counted as verified here. ABI9's broader
CLI, dense diagnostics and app-shell workflows do **not** pass yet.

## Runtime changes

Shared diagnostic command-name helpers tolerate the selected public enum:
ABI9 removes PreparedText and exposes additional flat command kinds. Native
tools no longer require enum members from another ABI.

ABI9 Core text snapshots bind the document mutation generation, catalog and
layout proof. Even a paint-only `mark_dirty` invalidates the document proof.
The old native layer-only repaint and style-stable shortcut could reuse that
stale snapshot and receive a null layer tree. Native rendering now obtains a
fresh layout when the generation changes and bypasses that shortcut. It does
not modify the opaque Core snapshot or relax Core validation. The trace keeps
the originating dirty reason but records the actual rebuild action. The trace
regression retains the exact diagnostic repaint count and pixel equality;
only its ABI9 expected action changes to `rebuild-pipeline`.

Layout/layer refusals are checked at native build boundaries, frame-capture
exceptions report failure, and standard exceptions from Win32 message handling
are routed to existing application failure handling. This does not qualify VM
fatal recovery, all OOM paths or arbitrary callback recovery.

The pseudo-browser reports its actual font route instead of advertising
geometry-only diagnostics while painting Core glyphs. Pipeline dump remains
explicitly geometry-only. Test configuration chooses one expected policy;
it does not accept either policy to mask a fallback.

A separate regression-report bug was found during the real launcher workflow:
an aborted workflow wrote `status: passed` because it counted only completed
steps. An explicit failed abort step now makes the JSON and process result
agree. The original incomplete report is retained as invalid evidence, and
the rerun correctly reports five completed steps and one failure.

## Broader product blockers retained as failures

| Workload | Current observation | Follow-up |
|---|---|---|
| CLI SVG preview | Raster packaging succeeds; pseudo preflight refuses layout with Core status6/UnsupportedText | Native image/natural-size and shared inline placement proof must be connected; do not substitute platform text |
| Dense visual diagnostics | 600 empty element nodes reach a nonnull layout but layer build refuses | ABI9 reserves up to four layers per layout box; default max_layers1024 cannot admit this tree. Reconcile native desktop budgets/diagnostics against Core's bounded reservation contract |
| Actual sample launcher workflow | Install/update and capture succeed, but capture emits `grid-track-sizing-fallback` with `unsupported-collapsing-block-margins` | Resolve the source-grid contract/fallback; the zero-diagnostics assertion remains unchanged |

The SVG preflight's inline image lacks the shared paragraph/atomic proof
required by Core; ABI9's installed natural-image API probe is absent. This is
an integration/admission boundary, not evidence of bad font rasterization.
The dense refusal is confirmed at the original dense fixture, not inferred
from a passing reduced fixture. No budgets or fixtures were reduced to pass.

Native contextual package selection, authenticated Binding ownership through
Desktop rendering, selected-family/fallback policy, wider contextual images,
effects and general inline/native controls remain open. Current contextual
layout remains the separately qualified bounded nested static-block route.
Do not combine contextual geometry with scalar JFTP text painting/caches.
Animations, scroll-only reuse and the rest of the ABI9 sample corpus are not
qualified by the limited mutation/hover tests above.

## Identity, evidence and limits

Runtime parent: 0d00025d. Core remains
f35f1f778119f360d508b69b0c7a0eaeafae6cb4, source640 files/hash
997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61.
Full/cropped use installed public headers/archives, exact0.6.2/ABI9 lock,
and no Core source compilation. The compatibility control is source override
0.9.0/ABI4, hash7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a;
it is not an exact released ABI4 package qualification.

Evidence: `E:/Experiments/migration-contextual-desktop-20261117`.
`complete-result.json` and `audit_native.py` verify selected profiles,
Runtime source agreement, imported provenance, binary hashes and retained
positive/negative results. `frozen` holds the three real native executables,
build descriptions/profile identities and failed launcher workflow.
`final-full`, `final-cropped`, `final-abi4` retain input pages, captures,
traces, refusals and result JSON. Earlier failed attempts are preserved.
Candidate app manifests are staged separately and audited to change only
runtime.minRenderCore to0.6.2; production packages remain unchanged.

The production dependency lock remains0.9.0/ABI4; only the isolated qualification
snapshot uses0.6.2/ABI9. No Core holder/API, Runtime library implementation,
port, default storage profile, production keys or device contents changed.
The new conservative rebuild can cost more than layer reuse; no Desktop
performance, target memory/latency or physical acceptance is claimed here.
Historical released ABI4 device acceptances remain closed.

Next: reconcile native image/shared-inline entry and the budget/grid failures,
then implement one explicit contextual Desktop entry with matching resource,
layout and painter ownership. Preserve strict route and version refusals.
