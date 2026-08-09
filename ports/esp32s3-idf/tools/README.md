# ESP32-S3 Tools

> Last updated: 2026-08-09; Applies to: 0.6.0-dev; compatibility baseline: 0.5.0

Helper scripts for generating or validating ESP32-S3 port resources.

Tools in this directory run on the development machine, not on the embedded
target.

- `generate_resource_bundle.py`: turns the selected app/resource tree into the
  bounded C++ resource table consumed by the port build.
- `generate_noto_sans_sc_font_pack.py`: offline font-pack generation; source
  fonts and licensing records live under `../font/`.
- `collect_p3_1_acceptance.py` and `collect_p3_4_mixed_soak.py`: evidence
  collectors for port-owned reports, not runtime firmware tools.

Keep generated sources, SDK build directories, serial logs and test artifacts
outside commits. For the build/flash/report sequence, read the parent port
README and `docs/porting_work_guide.md` first.
