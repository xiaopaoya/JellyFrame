"""Stable JFDP feature names and explicit installed text-route negotiation."""

# Existing bit assignments are a wire contract; append rather than renumber.
FEATURE_FAMILIES = {
    1 << 0: "core.document",
    1 << 1: "core.paint",
    1 << 2: "css.flex-grid",
    1 << 3: "css.modern-paint",
    1 << 4: "forms.advanced",
    1 << 5: "graphics.canvas2d",
    1 << 6: "text.shaping",
    1 << 7: "text.contextual-jfsp2",
    1 << 8: "text.contextual-layout",
    1 << 9: "css.length-math",
    1 << 10: "css.counters",
    1 << 11: "css.borders",
    1 << 12: "css.tables",
    1 << 13: "css.positioned-sizing",
}
KNOWN_FEATURE_FAMILIES = frozenset(FEATURE_FAMILIES.values())
SHAPED_PIPELINES = frozenset(("jfsp1-block-v11", "jfsp2-block-v12"))


def supported_text_pipelines(core_abi, feature_families):
    """Derive routes only from a matched image identity, never packet magic.

    Contextual text without contextual layout is a valid Core capability but
    cannot run an installed contextual DOM app. Unknown or conflicting identity
    declarations refuse instead of silently granting a different text route.
    """
    if type(core_abi) is not int or not 0 < core_abi <= 0xffffffff:
        raise ValueError("invalid Render Core ABI")
    if not isinstance(feature_families, (list, tuple, set, frozenset)):
        raise ValueError("invalid Render Core feature families")
    if any(not isinstance(item, str) for item in feature_families):
        raise ValueError("invalid Render Core feature family")
    families = set(feature_families)
    if (len(families) != len(feature_families) or families - KNOWN_FEATURE_FAMILIES or
            not {"core.document", "core.paint"}.issubset(families)):
        raise ValueError("invalid Render Core feature families")
    contextual = "text.contextual-jfsp2" in families
    layout = "text.contextual-layout" in families
    shaping = "text.shaping" in families
    if layout and not contextual:
        raise ValueError("contextual layout requires contextual JFSP2")
    if (contextual or layout) and core_abi != 9:
        raise ValueError("contextual pipeline requires matched Core ABI9")
    if shaping and (core_abi != 4 or contextual):
        raise ValueError("legacy shaping requires matched Core ABI4")
    if contextual and layout:
        return ("jfsp2-block-v12",)
    return ("jfsp1-block-v11",) if shaping else ()
