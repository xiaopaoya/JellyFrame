# Native positioned migration and route coverage

Last updated: 2026-11-18; Applies to: 0.6.0-dev

## Product qualification

Core source641/hash9645ce6b adds selected positioned percentage widths, definite
Grid/Flex and canonical positioned Canvas. The exact candidate lock is
E:/Experiments/migration-positioned-native-20261118/candidate-positioned-canvas-lock.cmake.
Production lock/defaults remain on their existing release line.

The positioned candidate reached 82/89 after the documentation gate was repaired. Canvas Gauges static
preflight and frame script, external author trial, Motion startup/equivalence,
low-power/soak and dirty-flag gates now pass. The source implementations and
existing product fixtures are used through their ordinary entries.

## Route coverage correction

The original route script clicked y278. Authored buttons occupy y238 through276
at300x300, and captured hit targets were main#app. Corrected y257 hits the actual
Focus, Settings and Today buttons. A separate functional gate checks each exact
hash, title/detail/value/summary and unique active tab, plus initial Today state.
A final green marker proves every transition executed. Its expected final frame
is removed before each run to prevent an earlier output satisfying a failed
capture. An experiment-only negative control with the previous coordinates
fails the transition checks.

The old rebuild1 expression described the missed-click workload. With actual
route changes, ABI4 control produces three text mutations, three style repaints
and four rebuilds including initial paint. The corrected deterministic gate is
repaint3/rebuild4. ABI4 functional and calibrated performance tests pass3/3
including staging. ABI9 functional state passes while performance remains
repaint0/rebuild7, so its gate remains failed.

Core requires exact document generation for its scalar snapshots. Core
mark_dirty advances that generation for paint-only marks too; each scalar text
snapshot and shared paragraph proof binds to the same exact generation. Runtime
retains the old geometry/layers for damage, but LayerTree correctly refuses to
consume stale snapshots. Therefore the unchanged-layout predicate alone cannot
admit ABI9 reuse.

The subsequent Core experiment is an explicit snapshot rebind operation that
refreshes proof metadata only after Runtime proves equal tree shape, layout
fields and stable text sources. It must refresh shared IFC/control proofs
transactionally, reject partial/unsupported trees, and keep direct stale
LayerTree builds failing. Route Tabs changes paragraph source text and must
continue shaping current text; any reuse there needs an explicit source update
contract. Motion Lab's transform/opacity animation is the paint-only positive
candidate.

## Remaining gates

Core `47229e42` (source hash `af853a23…`) resolves Band Shell's Grid source
width query. The original launcher at 172×320 emits zero diagnostics and its
BMP is byte-identical to the predecessor capture. Weather route pixel parity
passes. Core full/cropped/OFF passed 26/26, 26/26 and 14/14. The matching Runtime
installed matrix passed 39/39 per profile under the exact package identity lock.
The Runtime app-font microbenchmark builds and executes with the ABI9 CSS family
hash and retains the resolved-family path for the canonical value API.

Core `56f1d07f` (`cc006bfc`) keeps native graphic controls in Flex out of the
image metadata path. Core passed 26/26 full, 26/26 cropped and 14/14 with
contextual text disabled. Runtime `df0bde80` shares package font loading
between preflight and the shell; both produce identical named-font probe
pixels. The positive Font Policy sample and the negative Kana probe now run
separately. The matching installed Runtime suites pass 39/39 per profile.

Desktop qualification reaches 88/90 after retaining previous geometry for
damage calculation when stale scalar text requires a fresh layout. Motion
Lab rebuilds 68 times, with four local paints and 64 full paints. All 90
captured frames match forced full repaint. Contextual CLI package rendering is
implemented through the existing five-resource loader and contextual capture
path. The positive fixture is byte-identical to desktop-shell pixels; invalid
font bytes refuse before output. Targeted contextual/paint/font-policy gates
pass 6/6. The current Core rebind requires the generation of the candidate tree
and refuses a transform change above absolute/fixed descendants. Route Tabs
uses local repaint on the three scripted navigations; Motion Lab reuses the
layout for 63 of 64 transform/opacity updates and all 90 frames match full
repaint. The device-provider simulation now isolates its temporary session
directory. Runtime Release passes 108/108 with a source override (contextual
OFF), and 90/90 against the installed full Core at exact source f92cbd9e
(contextual available; the desktop/CLI fixture exercises this route).
Feature inventory, signed/resource/target carry-forward and physical
acceptance remain open.
Current evidence: E:/Experiments/migration-flex-controls-20261118.

Grid/desktop evidence: E:/Experiments/migration-inline-atom-grid-20261118.
Installed identity and results: E:/Experiments/migration-inline-atom-installed-20261118/result.json.

Evidence is E:/Experiments/migration-positioned-native-20261118 and
E:/Experiments/migration-route-tabs-coverage-20261118. The comprehensive log is
runtime-with-canvas-ctest.log. ABI4 calibrated evidence is
abi4-final-route-ctest.log, and ABI9 calibrated failure is
abi9-calibrated-route-ctest.log. Earlier e3cd8250 and sourcef431075e results remain
historical.
