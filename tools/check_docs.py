"""Validate the repository's public Markdown contract before MkDocs builds."""

from __future__ import annotations

import datetime as dt
import re
import sys
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
REQUIRED_METADATA = {"title", "summary", "doc_type", "status", "last_updated"}
DOC_TYPES = {"tutorial", "how-to", "reference", "explanation", "index"}
STATUSES = {"draft", "verified", "partial", "deprecated", "historical"}
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*$")
FENCE_RE = re.compile(r"^\s*(`{3,}|~{3,})(.*)$")
KEBAB_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
RAW_HTML_RE = re.compile(r"</?[A-Za-z][^>]*>")


def report(errors: list[str], path: Path, line: int, message: str) -> None:
    errors.append(f"{path.relative_to(ROOT)}:{line}: {message}")


def split_front_matter(path: Path, lines: list[str], errors: list[str]) -> tuple[dict, int]:
    if not lines or lines[0] != "---":
        report(errors, path, 1, "page must begin with YAML front matter")
        return {}, 0

    try:
        closing = lines.index("---", 1)
    except ValueError:
        report(errors, path, 1, "YAML front matter is not closed")
        return {}, 0

    try:
        metadata = yaml.safe_load("\n".join(lines[1:closing])) or {}
    except yaml.YAMLError as exc:
        report(errors, path, 1, f"invalid YAML front matter: {exc}")
        return {}, closing + 1
    if not isinstance(metadata, dict):
        report(errors, path, 1, "front matter must be a YAML mapping")
        return {}, closing + 1
    return metadata, closing + 1


def validate_metadata(path: Path, metadata: dict, errors: list[str]) -> None:
    missing = sorted(REQUIRED_METADATA - metadata.keys())
    if missing:
        report(errors, path, 1, f"missing required metadata: {', '.join(missing)}")
    if metadata.get("doc_type") not in DOC_TYPES:
        report(errors, path, 1, f"invalid doc_type: {metadata.get('doc_type')!r}")
    if metadata.get("status") not in STATUSES:
        report(errors, path, 1, f"invalid status: {metadata.get('status')!r}")

    updated = metadata.get("last_updated")
    if not isinstance(updated, (dt.date, str)):
        report(errors, path, 1, "last_updated must be an ISO date")
    elif isinstance(updated, str):
        try:
            dt.date.fromisoformat(updated)
        except ValueError:
            report(errors, path, 1, "last_updated must be an ISO date")

    for key in ("title", "summary"):
        value = metadata.get(key)
        if not isinstance(value, str) or not value.strip():
            report(errors, path, 1, f"{key} must be a non-empty string")
    summary = metadata.get("summary")
    if isinstance(summary, str) and "\n" in summary:
        report(errors, path, 1, "summary must be one plain YAML sentence")
    elif isinstance(summary, str) and not summary.rstrip().endswith((".", "!", "?")):
        report(errors, path, 1, "summary must end as a complete sentence")

    tags = metadata.get("tags")
    if tags is not None and (
        not isinstance(tags, list)
        or not 3 <= len(tags) <= 8
        or any(not isinstance(tag, str) or not tag for tag in tags)
    ):
        report(errors, path, 1, "tags must contain three to eight non-empty strings")

    source_files = metadata.get("source_files")
    if source_files is not None:
        if not isinstance(source_files, list) or any(not isinstance(item, str) for item in source_files):
            report(errors, path, 1, "source_files must be a list of repository paths")
        else:
            for source in source_files:
                if not (ROOT / source).is_file():
                    report(errors, path, 1, f"source_files entry does not exist: {source}")


def validate_markdown(
    path: Path, lines: list[str], body_start: int, metadata: dict, errors: list[str]
) -> None:
    headings: list[tuple[int, str, int]] = []
    in_fence = False
    fence_character = ""
    fence_length = 0

    for index in range(body_start, len(lines)):
        line = lines[index]
        line_number = index + 1
        if not in_fence:
            match = FENCE_RE.match(line)
            if match:
                marker, info = match.groups()
                if not info.strip():
                    report(errors, path, line_number, "fenced code block needs a language")
                if index > body_start and lines[index - 1]:
                    report(errors, path, line_number, "fenced code block needs a blank line before it")
                in_fence = True
                fence_character = marker[0]
                fence_length = len(marker)
                continue
        elif re.fullmatch(rf"\s*{re.escape(fence_character)}{{{fence_length},}}\s*", line):
            if index + 1 < len(lines) and lines[index + 1]:
                report(errors, path, line_number, "fenced code block needs a blank line after it")
            in_fence = False
            continue

        if in_fence:
            continue
        without_inline_code = re.sub(r"`[^`]*`", "", line)
        if RAW_HTML_RE.search(without_inline_code):
            report(errors, path, line_number, "raw HTML is not allowed in public Markdown")
        heading = HEADING_RE.match(line)
        if heading:
            headings.append((len(heading.group(1)), heading.group(2).strip(), line_number))

    if in_fence:
        report(errors, path, len(lines), "fenced code block is not closed")

    h1_count = sum(level == 1 for level, _, _ in headings)
    if h1_count != 1:
        report(errors, path, body_start + 1, f"page must have exactly one H1, found {h1_count}")

    previous_level: int | None = None
    seen: dict[str, int] = {}
    for level, name, line_number in headings:
        if previous_level is not None and level > previous_level + 1:
            report(errors, path, line_number, f"heading depth jumps from H{previous_level} to H{level}")
        previous_level = level
        key = name.casefold()
        if key in seen:
            report(errors, path, line_number, f"duplicate heading first used on line {seen[key]}: {name}")
        else:
            seen[key] = line_number

    h2_names = [name.casefold() for level, name, _ in headings if level == 2]
    if metadata.get("doc_type") == "reference" and (not h2_names or h2_names[0] != "scope"):
        report(errors, path, body_start + 1, "reference page must begin its H2 sections with Scope")


def main() -> int:
    errors: list[str] = []
    pages = sorted(DOCS.rglob("*.md"))
    for path in pages:
        relative = path.relative_to(DOCS)
        names = [*relative.parts[:-1], path.stem]
        if any(not KEBAB_RE.fullmatch(name) for name in names):
            report(errors, path, 1, "documentation paths must use lowercase kebab-case")
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        metadata, body_start = split_front_matter(path, lines, errors)
        validate_metadata(path, metadata, errors)
        validate_markdown(path, lines, body_start, metadata, errors)

    if errors:
        print("Documentation validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1
    print(f"Validated {len(pages)} public documentation pages.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
