"""Shared Render Core feature IDs and dependency rules for desktop tooling."""

from __future__ import annotations


KNOWN_RENDER_CORE_FEATURES = frozenset({
    "core.document",
    "core.paint",
    "css.flex-grid",
    "css.modern-paint",
    "forms.advanced",
    "graphics.canvas2d",
})

# Feature families are vertical slices.  A profile must advertise every
# prerequisite, while an App only declares the family it directly uses.
RENDER_CORE_FEATURE_DEPENDENCIES = {
    "core.document": frozenset(),
    "core.paint": frozenset({"core.document"}),
    "css.flex-grid": frozenset({"core.document"}),
    "css.modern-paint": frozenset({"core.paint"}),
    "forms.advanced": frozenset({"core.document"}),
    "graphics.canvas2d": frozenset({"core.paint"}),
}


def missing_feature_dependencies(features: list[str]) -> list[str]:
    """Return stable, human-readable dependency edges absent from a profile."""
    available = set(features)
    return sorted(
        f"{feature} -> {dependency}"
        for feature in features
        for dependency in RENDER_CORE_FEATURE_DEPENDENCIES[feature]
        if dependency not in available
    )
