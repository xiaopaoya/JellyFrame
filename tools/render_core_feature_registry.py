"""Shared Render Core feature metadata loaded from the build registry."""

from __future__ import annotations

from pathlib import Path


REGISTRY_PATH = Path(__file__).resolve().parents[1] / "cmake" / "render_core_feature_registry.csv"


def _load_registry() -> tuple[dict[str, frozenset[str]], dict[str, dict[str, str]]]:
    dependencies: dict[str, frozenset[str]] = {}
    metadata: dict[str, dict[str, str]] = {}
    for line_number, raw_line in enumerate(REGISTRY_PATH.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("|")
        if len(fields) != 5 or any(not field for field in fields):
            raise ValueError(f"malformed Render Core feature registry row {line_number}")
        feature, dependency_field, option, suffix, order = fields
        if feature in dependencies:
            raise ValueError(f"duplicate Render Core feature: {feature}")
        if not order.isdigit():
            raise ValueError(f"non-numeric Render Core feature order: {feature}")
        dependency_set = frozenset(
            dependency for dependency in dependency_field.split(",") if dependency != "-"
        )
        dependencies[feature] = dependency_set
        metadata[feature] = {
            "option": "" if option == "-" else option,
            "suffix": "" if suffix == "-" else suffix,
            "order": order,
        }
    unknown_dependencies = sorted(
        dependency
        for feature_dependencies in dependencies.values()
        for dependency in feature_dependencies
        if dependency not in dependencies
    )
    if unknown_dependencies:
        raise ValueError("unknown Render Core dependency: " + ", ".join(unknown_dependencies))
    return dependencies, metadata


RENDER_CORE_FEATURE_DEPENDENCIES, RENDER_CORE_FEATURE_METADATA = _load_registry()
KNOWN_RENDER_CORE_FEATURES = frozenset(RENDER_CORE_FEATURE_DEPENDENCIES)


def missing_feature_dependencies(features: list[str]) -> list[str]:
    """Return stable, human-readable dependency edges absent from a profile."""
    available = set(features)
    return sorted(
        f"{feature} -> {dependency}"
        for feature in features
        for dependency in RENDER_CORE_FEATURE_DEPENDENCIES[feature]
        if dependency not in available
    )
