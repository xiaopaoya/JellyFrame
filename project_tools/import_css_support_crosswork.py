#!/usr/bin/env python3
"""Normalize a CSSWG crosswork CSV into JellyFrame's public support schema."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


REQUIRED_COLUMNS = (
    "feature_id",
    "kind",
    "name",
    "context",
    "spec",
    "url",
    "jellyframe_status",
)

# The crosswork has useful internal distinctions for prioritization. Public
# documentation uses the same five labels as the HTML support table so authors
# do not need to learn a second status vocabulary.
STATUS_MAP = {
    "supported": "supported",
    "partial": "partial",
    "partial_detail": "partial",
    "supported_detail": "partial",
    "lazy-flattened": "partial",
    "missing": "unsupported",
    "missing_detail": "unsupported",
    "deferred": "unsupported",
    "lazy-ignored": "unsupported",
    "missing_or_lazy": "out_of_scope",
    "excluded_legacy_compat": "out_of_scope",
}


def note_for(source_status: str) -> str:
    if source_status == "supported":
        return "Usable in the documented JellyFrame CSS subset."
    if source_status in {"partial", "partial_detail", "supported_detail", "lazy-flattened"}:
        return (
            "Supported only in the documented subset or accepted owning property/value position; "
            "check the capability matrix for limits."
        )
    if source_status in {"missing_or_lazy", "excluded_legacy_compat"}:
        return (
            "Legacy compatibility syntax, specification detail, or browser-scale machinery outside "
            "the public JellyFrame CSS contract."
        )
    return "Not in the documented JellyFrame CSS subset; do not rely on it in an app."


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or any(column not in reader.fieldnames for column in REQUIRED_COLUMNS):
            raise ValueError("crosswork CSV is missing one or more required columns")
        rows = list(reader)

    occurrence_counts: dict[str, int] = {}
    normalized: list[dict[str, str]] = []
    for row in rows:
        source_feature_id = row["feature_id"].strip()
        source_status = row["jellyframe_status"].strip()
        if not source_feature_id:
            raise ValueError("crosswork CSV has an empty feature_id")
        if source_status not in STATUS_MAP:
            raise ValueError(f"unmapped crosswork status for {source_feature_id}: {source_status!r}")
        occurrence = occurrence_counts.get(source_feature_id, 0) + 1
        occurrence_counts[source_feature_id] = occurrence
        # CSSWG detail rows can share a source identifier while referring to
        # different grammar contexts. Keep every row addressable for editors.
        feature_id = source_feature_id if occurrence == 1 else f"{source_feature_id}#{occurrence}"
        normalized.append({
            "feature_id": feature_id,
            "kind": row["kind"].strip(),
            "area": row["spec"].strip(),
            "name": row["name"].strip(),
            "context": row["context"].strip(),
            "status": STATUS_MAP[source_status],
            "app_author_note": note_for(source_status),
            "spec_url": row["url"].strip(),
        })
    return normalized


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="CSS crosswork matrix CSV")
    parser.add_argument("--output", required=True, type=Path, help="normalized public support CSV")
    args = parser.parse_args()

    try:
        rows = read_rows(args.input)
    except (OSError, ValueError) as error:
        print(f"CSS support import failed: {error}", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=(
            "feature_id", "kind", "area", "name", "context", "status", "app_author_note", "spec_url",
        ))
        writer.writeheader()
        writer.writerows(rows)
    duplicate_suffixes = sum(1 for row in rows if "#" in row["feature_id"])
    print(f"imported: {args.output} ({len(rows)} rows; {duplicate_suffixes} duplicate source IDs disambiguated)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
