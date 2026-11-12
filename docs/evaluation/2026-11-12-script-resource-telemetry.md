# Installed script resource observations

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Installed script sessions now expose internal/PSRAM free bytes and largest free
blocks, plus worker/UI stack lifetime minimum unused bytes. The supervisor owns
task-handle sampling before deletion. Heap queries run outside the short value
copy critical section. Sampling occurs after child creation, at intervals of at
least one second and after both children finish. The startup baseline follows
resource preparation and precedes supervisor creation.

Heap observations describe global capability pools. Each sampled-minimum field
can come from a different instant, and transient pressure between samples can
be missed. These values do not isolate allocations made by one app. Stack HWM
uses bytes for the ESP-IDF Xtensa port. Zero requires interpretation with task
startup flags.

Logs retain two records per event. The compact status keeps initialization,
render, accepted frame and input counters. The resource tuple order is internal
free/largest, PSRAM free/largest. Stopped events retain the existing script-input
stopped summary consumed by lifecycle collectors. Other public telemetry fields
remain available even when omitted from the compact live log.

Formatter boundary checks and existing stop collectors pass. Full ESP-IDF5.3.1
build and combined R3/Node/CoreText/BMP/frame10 configuration compile. The actual
compiler flags and sdkconfig are verified in the resource1 physical run. Evidence
is E:/Experiments/script-resource-telemetry-20261112 and the adjacent migration
target acceptance evidence.

Resource1 at Runtime3f87b9d1/Core37919b82 starts and presents successfully.
Generation195 first live sample reports internal free203739/largest71680 and
PSRAM free7275280/largest6553600 bytes, worker/UI stack HWM15888/27132 bytes.
Thirty measured restart cycles are running. Product reserve, transient pressure,
sampling overhead and latency admission require further evidence.
