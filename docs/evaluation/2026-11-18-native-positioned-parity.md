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

Core requires exact document generation for its scalar snapshots. Runtime's
stale-snapshot guard correctly rebuilds at present. Reuse needs a proved Core
refresh/rebind path for paint changes. Text mutations need their own coherent
source/paragraph update contract. Counter reporting must continue reflecting
actual layout operations.

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
captured frames match forced full repaint. Route Tabs repaint reuse and
Motion Lab style-stable reuse remain open, as do contextual CLI, feature
coverage, signed/resource/target carry-forward and physical acceptance.
Current evidence: E:/Experiments/migration-flex-controls-20261118.

Grid/desktop evidence: E:/Experiments/migration-inline-atom-grid-20261118.
Installed identity and results: E:/Experiments/migration-inline-atom-installed-20261118/result.json.

Evidence is E:/Experiments/migration-positioned-native-20261118 and
E:/Experiments/migration-route-tabs-coverage-20261118. The comprehensive log is
runtime-with-canvas-ctest.log. ABI4 calibrated evidence is
abi4-final-route-ctest.log, and ABI9 calibrated failure is
abi9-calibrated-route-ctest.log. Earlier e3cd8250 and sourcef431075e results remain
historical.
