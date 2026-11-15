#!/usr/bin/env python3
"""Guard the intended CoreText and legacy ESP32 acceptance profiles."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "ports" / "esp32s3-idf"
CORE_TEXT = "CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM"


def read_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key in values:
            raise AssertionError(f"{path.name} repeats {key}")
        values[key] = value
    return values


def main() -> None:
    band_shell = read_config(PORT / "sdkconfig.ws147_band_shell.defaults")
    core_text = read_config(PORT / "sdkconfig.ws147_core_text.defaults")
    legacy_typography = read_config(PORT / "sdkconfig.ws147_typography_acceptance.defaults")

    assert band_shell.get(CORE_TEXT) == "y", "WS147 band-shell product profile must use CoreText"
    assert core_text.get(CORE_TEXT) == "y", "named CoreText acceptance profile must stay enabled"
    assert legacy_typography.get(CORE_TEXT) == "n", "legacy typography acceptance must stay explicit"


if __name__ == "__main__":
    main()
