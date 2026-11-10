# FreeRTOS IRAM Reserve Experiment

This opt-in build profile tests `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y`
with ESP-IDF 5.3.1 on the WS147 ESP32-S3 port. It is not included in normal
defaults and has not been hardware-accepted.

The baseline artifact `canonical-background-ws147-metadata3-4888-20261110`
uses Render Core source hash
`488874f1f2ce0880a47da5695cbe88a13a14b927461360cff3b58f8e9d2e6172`
(138 files) and reports 16,383 of 16,384 static IRAM bytes used: 15,356 bytes
of text and 1,027 bytes of vectors. FreeRTOS is the largest project-independent
contributor. IDF's `components/freertos/Kconfig` says this option places
selected non-ISR FreeRTOS functions in flash; the `linker_smp.lf` mapping keeps
ISR functions and their dependencies in IRAM and retains some hot paths there.

Build in an isolated directory, layering this fragment after the normal WS147
bring-up defaults. Keep the CMake source override pointed at the exact Core
source tree used for the baseline. Compare `idf.py size` output and inspect the
generated `jellyframe_render_core_port_provenance.txt` before interpreting the
result. No device flashing or hardware stress is part of this experiment. Any
candidate profile still requires hardware tests that exercise timer, task,
queue, interrupt, and flash erase/write activity together before acceptance.

## 2026-11-10 Build Result

The isolated build at `E:/Builds/iram-reserve-ws147-core488874-20261110`
completed with the option enabled. Generated provenance matched the requested
Core hash and 138-file count. Static IRAM remained exactly 16,383/16,384 bytes
(.text 15,356; vectors 1,027), so this option creates no static IRAM reserve
for the current configuration. Stat D/IRAM use fell by 8,328 bytes (71,163 to
62,835), all in `.text`; flash `.text` grew by 8,992 bytes (839,072 to 848,064),
and total image size grew by 664 bytes. The experiment therefore confirms a
DIRAM reserve trade, not relief from the exhausted static IRAM budget. Leave
the profile opt-in; hardware flash-erase/write stress remains outstanding.
