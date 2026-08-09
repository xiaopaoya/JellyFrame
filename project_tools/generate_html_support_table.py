#!/usr/bin/env python3
"""Generate the searchable HTML Living Standard support tables from one CSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from collections import Counter
from pathlib import Path


STATUSES = (
    "supported",
    "partial",
    "host_dependent",
    "unsupported",
    "out_of_scope",
)
SOURCE_AUDIT_DATE = "2026-07-12"
APPLIES_TO_VERSION = "0.6.0-dev"
REQUIRED_COLUMNS = (
    "feature_id",
    "type",
    "area",
    "name",
    "spec_section",
    "status",
    "app_author_note",
    "spec_url",
)


def read_rows(csv_path: Path) -> list[dict[str, str]]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source, restkey="_overflow")
        if reader.fieldnames is None or any(column not in reader.fieldnames for column in REQUIRED_COLUMNS):
            raise ValueError("support CSV is missing one or more required columns")
        rows = list(reader)

    seen_ids: set[str] = set()
    for row in rows:
        # Early crosswork CSV snapshots wrote some app-author notes without CSV
        # quoting. The spec URL is always final, so recover those note fragments
        # instead of silently treating one as a malformed URL.
        overflow = row.pop("_overflow", None)
        if overflow:
            if not overflow[-1].strip().startswith(("http://", "https://")):
                raise ValueError(f"malformed overflow fields for {row['feature_id']!r}")
            row["app_author_note"] = ",".join(
                [row["app_author_note"], row["spec_url"], *overflow[:-1]]
            )
            row["spec_url"] = overflow[-1]
        feature_id = row["feature_id"]
        if not feature_id or feature_id in seen_ids:
            raise ValueError(f"support CSV contains an empty or duplicate feature_id: {feature_id!r}")
        if row["status"] not in STATUSES:
            raise ValueError(f"unsupported status for {feature_id}: {row['status']!r}")
        seen_ids.add(feature_id)
    return rows


def markdown_cell(value: str) -> str:
    return " ".join(value.replace("|", "\\|").splitlines())


def render_table(rows: list[dict[str, str]]) -> str:
    lines = [
        "| ID | Type | Area | Name | Section | Status | App Author Note | Spec |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            "| {feature_id} | {type} | {area} | {name} | {spec_section} | `{status}` | {note} | [link]({url}) |".format(
                feature_id=markdown_cell(row["feature_id"]),
                type=markdown_cell(row["type"]),
                area=markdown_cell(row["area"]),
                name=markdown_cell(row["name"]),
                spec_section=markdown_cell(row["spec_section"]),
                status=markdown_cell(row["status"]),
                note=markdown_cell(row["app_author_note"]),
                url=markdown_cell(row["spec_url"]),
            )
        )
    return "\n".join(lines)


def render_document(rows: list[dict[str, str]], source_name: str, source_sha256: str, chinese: bool) -> str:
    counts = Counter(row["status"] for row in rows)
    if chinese:
        title = "HTML Living Standard 支持表"
        introduction = (
            "这是 JellyFrame 面向 App 作者的 HTML Living Standard 特性支持表。使用某个 HTML 标签、DOM API "
            "或浏览器行为前，可以先在本文中 Ctrl+F 搜索。"
        )
        purpose = (
            "本文保留所有已审计行，包括不支持项和超出范围项，目的不是宣称完整浏览器兼容，而是让支持边界可检索、"
            "可维护、未来可被 VS Code 扩展消费。"
        )
        matrix = "部分支持项的详细行为仍以 [developer_capability_matrix_zh.md](developer_capability_matrix_zh.md) 为准。"
        machine = "机器消费可优先读取"
        css = "CSS 不包含在本表中；请使用独立的 [csswg_support_table_zh.md](csswg_support_table_zh.md) 查询 CSSWG 特性。"
        meanings = "## 状态含义"
        statistics = "## 统计"
        full_table = "## 全量表"
        descriptions = {
            "supported": "可按 JellyFrame 文档化子集使用。",
            "partial": "只有子集、降级或普通元素保留；使用前请查能力矩阵。",
            "host_dependent": "依赖 manifest capability、target profile、宿主服务、codec、文本后端或预算。",
            "unsupported": "不要在 JellyFrame app 中依赖。",
            "out_of_scope": "规范说明文字、旧浏览器兼容机制或明确排除的浏览器级能力。",
        }
        source_line = f"> 生成来源：`{source_name}`；源 SHA-256：`{source_sha256}`。"
        generated_line = "> 本表由生成器生成，请勿手工编辑。"
        freshness_line = f"> 审计快照：{SOURCE_AUDIT_DATE}；适用版本：{APPLIES_TO_VERSION}。"
        machine_line = f"{matrix} {machine} [html_living_standard_support_table.csv](html_living_standard_support_table.csv)。"
    else:
        title = "HTML Living Standard Support Table"
        introduction = (
            "This is the searchable JellyFrame support table for HTML Living Standard features. Use it before choosing a "
            "tag, DOM API or browser-facing behavior in an app."
        )
        purpose = (
            "The table intentionally contains every audited row, including unsupported and out-of-scope items, so app authors "
            "can use Ctrl+F before they write code."
        )
        matrix = "For readable guidance on partially supported items, use [developer_capability_matrix.md](developer_capability_matrix.md)."
        machine = "For machine-oriented use, consume"
        css = "CSS is not included in this table. Use the separate [csswg_support_table.md](csswg_support_table.md) for CSSWG feature lookup."
        meanings = "## Status Meanings"
        statistics = "## Snapshot"
        full_table = "## Full Table"
        descriptions = {
            "supported": "usable in the documented JellyFrame subset.",
            "partial": "supported only as a subset, fallback or ordinary-element preservation; check the capability matrix.",
            "host_dependent": "depends on manifest capabilities, target profile, host services, codecs, text backend or budgets.",
            "unsupported": "do not rely on it in JellyFrame apps.",
            "out_of_scope": "spec prose, legacy browser compatibility or intentionally excluded browser machinery.",
        }
        source_line = f"> Generated from: `{source_name}`; source SHA-256: `{source_sha256}`."
        generated_line = "> Do not edit this generated table by hand."
        freshness_line = f"> Source audit: {SOURCE_AUDIT_DATE}; Applies to: {APPLIES_TO_VERSION}."
        machine_line = f"{matrix} {machine} [html_living_standard_support_table.csv](html_living_standard_support_table.csv)."

    stat_lines = "\n".join(f"| `{status}` | {counts[status]} |" for status in STATUSES)
    meaning_lines = "\n".join(f"- `{status}`: {descriptions[status]}" for status in STATUSES)
    return "\n".join(
        (
            f"# {title}",
            "",
            source_line,
            generated_line,
            freshness_line,
            "",
            introduction,
            "",
            purpose,
            "",
            machine_line,
            "",
            css,
            "",
            meanings,
            "",
            meaning_lines,
            "",
            statistics,
            "",
            "| Status | Rows |",
            "| --- | ---: |",
            stat_lines,
            "",
            full_table,
            "",
            render_table(rows),
            "",
        )
    )


def write_or_check(path: Path, expected: str, check: bool) -> bool:
    actual = path.read_text(encoding="utf-8-sig") if path.exists() else None
    if actual == expected:
        return True
    if check:
        print(f"out of date: {path}")
        return False
    path.write_text(expected, encoding="utf-8", newline="\n")
    print(f"generated: {path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=Path("docs/html_living_standard_support_table.csv"))
    parser.add_argument("--english", type=Path, default=Path("docs/html_living_standard_support_table.md"))
    parser.add_argument("--chinese", type=Path, default=Path("docs/html_living_standard_support_table_zh.md"))
    parser.add_argument("--check", action="store_true", help="fail instead of rewriting stale generated tables")
    args = parser.parse_args()

    try:
        rows = read_rows(args.csv)
    except (OSError, ValueError) as error:
        print(f"support table generation failed: {error}", file=sys.stderr)
        return 2

    source_sha256 = hashlib.sha256(args.csv.read_bytes()).hexdigest()
    english = render_document(rows, args.csv.name, source_sha256, chinese=False)
    chinese = render_document(rows, args.csv.name, source_sha256, chinese=True)
    current = write_or_check(args.english, english, args.check)
    current = write_or_check(args.chinese, chinese, args.check) and current
    return 0 if current else 1


if __name__ == "__main__":
    raise SystemExit(main())
