#!/usr/bin/env python3
"""Generate searchable CSSWG support tables from one normalized CSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from collections import Counter
from pathlib import Path


STATUSES = ("supported", "partial", "host_dependent", "unsupported", "out_of_scope")
SOURCE_AUDIT_DATE = "2026-07-22"
APPLIES_TO_VERSION = "0.6.0-dev"
REQUIRED_COLUMNS = (
    "feature_id", "kind", "area", "name", "context", "status", "app_author_note", "spec_url",
)


def read_rows(csv_path: Path) -> list[dict[str, str]]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or any(column not in reader.fieldnames for column in REQUIRED_COLUMNS):
            raise ValueError("support CSV is missing one or more required columns")
        rows = list(reader)

    seen_ids: set[str] = set()
    for row in rows:
        feature_id = row["feature_id"]
        if not feature_id or feature_id in seen_ids:
            raise ValueError(f"support CSV contains an empty or duplicate feature_id: {feature_id!r}")
        if row["status"] not in STATUSES:
            raise ValueError(f"unsupported status for {feature_id}: {row['status']!r}")
        seen_ids.add(feature_id)
    return rows


def source_sha256(csv_path: Path) -> str:
    # Git may check this source out as CRLF on Windows. The generated table must
    # describe logical CSV content, not checkout-specific line endings.
    source = csv_path.read_text(encoding="utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    return hashlib.sha256(source.encode("utf-8")).hexdigest()


def markdown_cell(value: str) -> str:
    return " ".join(value.replace("|", "\\|").splitlines())


def render_table(rows: list[dict[str, str]]) -> str:
    lines = [
        "| ID | Kind | CSS Module | Feature | Context | Status | App Author Note | Spec |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            "| {feature_id} | {kind} | {area} | {name} | {context} | `{status}` | {note} | [link]({url}) |".format(
                feature_id=markdown_cell(row["feature_id"]),
                kind=markdown_cell(row["kind"]),
                area=markdown_cell(row["area"]),
                name=markdown_cell(row["name"]),
                context=markdown_cell(row["context"]),
                status=markdown_cell(row["status"]),
                note=markdown_cell(row["app_author_note"]),
                url=markdown_cell(row["spec_url"]),
            )
        )
    return "\n".join(lines)


def render_document(rows: list[dict[str, str]], source_name: str, source_sha256: str, chinese: bool) -> str:
    counts = Counter(row["status"] for row in rows)
    if chinese:
        title = "CSSWG 支持表"
        introduction = "这是 JellyFrame 面向 App 作者的 CSSWG 特性支持表。使用属性、函数、选择器、值或 at-rule 前，可以先在本文中 Ctrl+F 搜索。"
        purpose = "本文保留所有已审计行，包括不支持项和超出范围项；它描述的是受资源预算约束的 CSS 子集，不是完整浏览器兼容性声明。少量源审计 ID 在不同语法上下文中重复，后续行以 `#2`、`#3` 后缀区分。"
        matrix = "部分支持项的具体语义、降级和性能边界仍以 [developer_capability_matrix_zh.md](developer_capability_matrix_zh.md) 为准。"
        descriptions = {
            "supported": "可按 JellyFrame 文档化 CSS 子集使用。",
            "partial": "只支持子集，或仅在特定所属属性/值位置可用；使用前请查能力矩阵。",
            "host_dependent": "依赖 target profile、宿主服务、文本后端、codec 或预算。",
            "unsupported": "不要在 JellyFrame app 中依赖。",
            "out_of_scope": "旧兼容语法、规范细节，或明确排除的浏览器级机制。",
        }
        source_line = f"> 生成来源：`{source_name}`；源 SHA-256：`{source_sha256}`。"
        generated_line = "> 本表由生成器生成，请勿手工编辑。"
        freshness_line = f"> 审计快照：{SOURCE_AUDIT_DATE}；适用版本：{APPLIES_TO_VERSION}。"
        machine_line = f"{matrix} 机器消费可优先读取 [csswg_support_table.csv](csswg_support_table.csv)。"
        meanings, statistics, full_table = "## 状态含义", "## 统计", "## 全量表"
    else:
        title = "CSSWG Support Table"
        introduction = "This is the searchable JellyFrame support table for CSSWG features. Search it before using a property, function, selector, value or at-rule in an app."
        purpose = "The table retains every audited row, including unsupported and out-of-scope items. It describes a resource-bounded CSS subset, not a claim of complete browser compatibility. A small number of source audit IDs repeat across grammar contexts; later rows use `#2`, `#3` suffixes to remain addressable."
        matrix = "For exact subset behavior, degradation and performance limits, use [developer_capability_matrix.md](developer_capability_matrix.md)."
        descriptions = {
            "supported": "usable in the documented JellyFrame CSS subset.",
            "partial": "supported only as a subset or in accepted owning property/value positions; check the capability matrix.",
            "host_dependent": "depends on target profile, host services, text backend, codecs or budgets.",
            "unsupported": "do not rely on it in JellyFrame apps.",
            "out_of_scope": "legacy compatibility syntax, specification detail or intentionally excluded browser-scale machinery.",
        }
        source_line = f"> Generated from: `{source_name}`; source SHA-256: `{source_sha256}`."
        generated_line = "> Do not edit this generated table by hand."
        freshness_line = f"> Source audit: {SOURCE_AUDIT_DATE}; Applies to: {APPLIES_TO_VERSION}."
        machine_line = f"{matrix} For machine-oriented use, consume [csswg_support_table.csv](csswg_support_table.csv)."
        meanings, statistics, full_table = "## Status Meanings", "## Snapshot", "## Full Table"

    stat_lines = "\n".join(f"| `{status}` | {counts[status]} |" for status in STATUSES)
    meaning_lines = "\n".join(f"- `{status}`: {descriptions[status]}" for status in STATUSES)
    return "\n".join((
        f"# {title}", "", source_line, generated_line, freshness_line, "", introduction, "", purpose, "",
        machine_line, "", meanings, "", meaning_lines, "", statistics, "", "| Status | Rows |",
        "| --- | ---: |", stat_lines, "", full_table, "", render_table(rows), "",
    ))


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
    parser.add_argument("--csv", type=Path, default=Path("docs/csswg_support_table.csv"))
    parser.add_argument("--english", type=Path, default=Path("docs/csswg_support_table.md"))
    parser.add_argument("--chinese", type=Path, default=Path("docs/csswg_support_table_zh.md"))
    parser.add_argument("--check", action="store_true", help="fail instead of rewriting stale generated tables")
    args = parser.parse_args()
    try:
        rows = read_rows(args.csv)
    except (OSError, ValueError) as error:
        print(f"support table generation failed: {error}", file=sys.stderr)
        return 2
    source_hash = source_sha256(args.csv)
    english = render_document(rows, args.csv.name, source_hash, chinese=False)
    chinese = render_document(rows, args.csv.name, source_hash, chinese=True)
    current = write_or_check(args.english, english, args.check)
    current = write_or_check(args.chinese, chinese, args.check) and current
    return 0 if current else 1


if __name__ == "__main__":
    raise SystemExit(main())
