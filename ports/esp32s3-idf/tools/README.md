# ESP32-S3 Tools

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Helper scripts for generating or validating ESP32-S3 port resources.

Tools in this directory run on the development machine, not on the embedded
target.

- `generate_resource_bundle.py`: turns the selected app/resource tree into the
  bounded C++ resource table consumed by the port build.
- `generate_noto_sans_sc_font_pack.py`: offline font-pack generation; source
  fonts and licensing records live under `../font/`.
- `collect_p3_1_acceptance.py`, `collect_p3_4_mixed_soak.py` and
  `collect_value_frame_v2_soak.py`: evidence collectors for port-owned
  reports, not runtime firmware tools. The value-frame v2 collector writes a
  raw log, ANSI-clean log and machine-readable `summary.json`; use
  `--from-log <serial.raw.log>` to rebuild the latter two from an existing
  capture without touching hardware.

Keep generated sources, SDK build directories, serial logs and test artifacts
outside commits. For the build/flash/report sequence, read the parent port
README and `docs/porting_work_guide.md` first.
