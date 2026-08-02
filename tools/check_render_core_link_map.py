#!/usr/bin/env python3
"""Verify link-map-visible Render Core families against a generated profile.

This is intentionally a conservative smoke check. A map proves that a
separately linked family reached the selected desktop validation executable;
families compiled into shared translation units are reported as not applicable.
It does not replace an embedded link map or claim a firmware flash/RAM result.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from render_core_feature_registry import (
    KNOWN_RENDER_CORE_FEATURES,
    missing_feature_dependencies,
)


FEATURE_MARKERS = {
    "css.modern-paint": ("modern_paint_",),
    # The disabled Canvas API keeps the public registry symbols. The object
    # file marker distinguishes the real implementation from that stub.
    "graphics.canvas2d": ("canvas2d.cpp.obj", "canvas2d.cpp.o"),
}

# Flex/grid is conditionally compiled inside parser, style, layout and layer
# translation units that are also required by the minimal profile. A map cannot
# distinguish its preprocessor-gated paths from the common implementation.
# Keep that limitation explicit instead of treating profile metadata as a link
# proof. The configure-only profile regression and an ON/OFF behavior workload
# provide the evidence for this family.
PROFILE_GATED_WITHOUT_LINK_MARKER = {
    "css.flex-grid": (
        "compiled into shared parser/style/layout/layer translation units; "
        "validate through the generated profile and an ON/OFF behavior workload"
    ),
}

CHECKED_FEATURES = tuple(sorted(
    set(FEATURE_MARKERS) | set(PROFILE_GATED_WITHOUT_LINK_MARKER)
))


def fail(message: str) -> None:
    raise SystemExit(f"render_core_link_map_check: {message}")


def load_profile(path: Path) -> dict:
    try:
        profile = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        fail(f"cannot read profile {path}: {error}")
    except json.JSONDecodeError as error:
        fail(f"invalid profile JSON {path}: {error}")
    if not isinstance(profile, dict) or profile.get("schemaVersion") != 1:
        fail("profile must be a schemaVersion 1 object")
    features = profile.get("features")
    if not isinstance(features, list) or any(not isinstance(item, str) for item in features):
        fail("profile features must be an array of strings")
    if len(features) != len(set(features)):
        fail("profile features must be unique")
    unknown = sorted(set(features) - KNOWN_RENDER_CORE_FEATURES)
    if unknown:
        fail("profile contains unknown features: " + ", ".join(unknown))
    missing_dependencies = missing_feature_dependencies(features)
    if missing_dependencies:
        fail(
            "profile has missing feature dependencies: "
            + ", ".join(missing_dependencies)
        )
    return profile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--map", required=True, dest="map_path", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--used-feature", action="append", choices=CHECKED_FEATURES,
        help=(
            "Feature exercised by the linked workload. Repeat for multiple features. "
            "When supplied, profile-enabled but unused families are reported as not-tested "
            "because linker garbage collection may remove them."
        ),
    )
    parser.add_argument(
        "--scope-used-features", action="store_true",
        help=(
            "Scope the check to --used-feature values, including an empty set "
            "for a workload that calls no optional family."
        ),
    )
    args = parser.parse_args()

    profile = load_profile(args.profile)
    try:
        map_text = args.map_path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        fail(f"cannot read linker map {args.map_path}: {error}")

    checks = []
    failures = []
    if args.scope_used_features:
        used_features = set(args.used_feature or [])
    else:
        used_features = None if args.used_feature is None else set(args.used_feature)
    if used_features is not None:
        missing_used_features = sorted(used_features - set(profile["features"]))
        if missing_used_features:
            fail(
                "workload uses features absent from profile: "
                + ", ".join(missing_used_features)
            )
    for feature in CHECKED_FEATURES:
        expected = feature in profile["features"]
        tested = used_features is None or feature in used_features
        if feature in PROFILE_GATED_WITHOUT_LINK_MARKER:
            checks.append({
                "feature": feature,
                "expectedInProfile": expected,
                "markerFoundInMap": None,
                "testedByWorkload": tested,
                "markers": [],
                "mapValidation": "not-applicable",
                "reason": PROFILE_GATED_WITHOUT_LINK_MARKER[feature],
                "status": "not-applicable",
            })
            continue

        markers = FEATURE_MARKERS[feature]
        present = any(marker in map_text for marker in markers)
        if expected and used_features is not None and not tested:
            status = "not-tested"
        else:
            status = "pass" if present == expected else "fail"
        check = {
            "feature": feature,
            "expectedInProfile": expected,
            "markerFoundInMap": present,
            "testedByWorkload": tested,
            "markers": list(markers),
            "status": status,
        }
        checks.append(check)
        if status == "fail":
            failures.append(
                f"{feature}: profile expects {'linked' if expected else 'absent'}, "
                f"map marker is {'present' if present else 'absent'}"
            )

    report = {
        "format": "jellyframe.render_core.link_map_check",
        "profile": str(args.profile),
        "map": str(args.map_path),
        "usedFeatures": sorted(used_features) if used_features is not None else None,
        "checks": checks,
        "failures": failures,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if failures:
        return 1
    print("render core link map check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
