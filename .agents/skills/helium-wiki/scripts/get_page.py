#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["PyYAML>=6.0"]
# ///
"""Retrieve a page by YAML id or a path relative to the installed skill root.

Run from helium-wiki:
    uv run scripts/get_page.py languages-c-float16-half-precision
    uv run scripts/get_page.py wiki/languages/languages-c-float16-half-precision.md
    uv run scripts/get_page.py languages-c-float16-half-precision --body-only
    uv run scripts/get_page.py languages-c-float16-half-precision --frontmatter-only
    uv run scripts/get_page.py languages-c-float16-half-precision --follow-sources

IDs are searched in wiki/ and sources/ (if present). Paths and local sources
stay inside the skill folder. Source previews contain the first 500 characters
of local files, shown as stored. Source following is one level deep and never
makes network requests. URL sources, unresolved names and binary sources are
reported without fetching them.
Exit codes: 0 = page retrieved (source failures are warnings), 1 = not found,
2 = invalid arguments, ambiguous IDs, or an unreadable/malformed page.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

SKILL_ROOT = Path(__file__).resolve().parent.parent
PREVIEW_LENGTH = 500


@dataclass
class Page:
    path: Path
    header: dict
    frontmatter: str
    body: str

    @property
    def text(self) -> str:
        return self.frontmatter + self.body


def read_frontmatter(stream) -> tuple[str, dict]:
    opening = stream.readline()
    if opening.rstrip() != "---":
        raise ValueError("missing YAML frontmatter opening delimiter")
    lines = [opening]
    for line in stream:
        lines.append(line)
        if line.rstrip() in ("---", "..."):
            break
    else:
        raise ValueError("missing YAML frontmatter closing delimiter")
    header = yaml.safe_load("".join(lines[1:-1]))
    if not isinstance(header, dict):
        raise ValueError("frontmatter must be a YAML mapping")
    return "".join(lines), header


def read_page(file: Path) -> Page:
    with file.open(encoding="utf-8-sig") as stream:
        frontmatter, header = read_frontmatter(stream)
        body = stream.read()
    return Page(file, header, frontmatter, body)


def contained_path(value: str, base: Path, skill_root: Path) -> Path:
    file = Path(value.replace("\\", "/"))
    if not file.is_absolute():
        file = base / file
    file = file.resolve()
    if not file.is_relative_to(skill_root.resolve()):
        raise ValueError("path must stay inside the skill folder")
    return file


def display_path(file: Path, skill_root: Path = SKILL_ROOT) -> str:
    return file.relative_to(skill_root.resolve()).as_posix()


def find_by_id(reference: str, skill_root: Path) -> tuple[Page, list[str]]:
    matches = []
    warnings = []
    for folder in ("wiki", "sources"):
        directory = skill_root / folder
        if not directory.is_dir():
            continue
        for file in sorted(directory.rglob("*.md")):
            try:
                file = contained_path(str(file), skill_root, skill_root)
                with file.open(encoding="utf-8-sig") as stream:
                    if stream.readline().rstrip() != "---":
                        continue  # README/index documents do not have page IDs.
                    stream.seek(0)
                    _, header = read_frontmatter(stream)
                page_id = header.get("id")
                if isinstance(page_id, str) and page_id.casefold() == reference.casefold():
                    matches.append(file)
            except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
                warnings.append(f"Skipping {file}: {error}")
    if not matches:
        detail = f"; skipped {len(warnings)} unreadable page(s)" if warnings else ""
        raise FileNotFoundError(f"No page with id {reference!r}{detail}")
    if len(matches) > 1:
        paths = ", ".join(display_path(file, skill_root) for file in matches)
        raise ValueError(f"Ambiguous id {reference!r}: {paths}. Use an explicit path.")
    return read_page(matches[0]), warnings


def get_page(reference: str, skill_root: Path = SKILL_ROOT) -> tuple[Page, list[str]]:
    skill_root = skill_root.resolve()
    if not reference.strip():
        raise ValueError("page id or path must not be empty")
    if "/" in reference or "\\" in reference or reference.lower().endswith(".md"):
        file = contained_path(reference, skill_root, skill_root)
        if file.suffix.lower() != ".md":
            raise ValueError("page paths must refer to Markdown (.md) files")
        if not file.is_file():
            raise FileNotFoundError(f"No page at {reference!r}")
        return read_page(file), []
    return find_by_id(reference, skill_root)


def local_preview(file: Path) -> str:
    if file.suffix.lower() == ".pdf":
        raise ValueError("PDF source has no text preview")
    with file.open(encoding="utf-8-sig", errors="replace", newline="") as stream:
        text = stream.read(PREVIEW_LENGTH)
    if "\x00" in text:
        raise ValueError("binary source has no text preview")
    return text


def source_preview(source: str, page: Page, skill_root: Path = SKILL_ROOT) -> str:
    skill_root = skill_root.resolve()
    if "://" in source:
        raise ValueError("URL sources cannot be previewed; source following is local-only")
    for base in (skill_root, page.path.parent):
        try:
            file = contained_path(source, base, skill_root)
        except ValueError:
            continue
        if file.is_file():
            return local_preview(file)
    # Official document names may have no local content; try exact source-page IDs.
    source_page, _ = find_by_id(source, skill_root)
    return local_preview(source_page.path)


def sources_from_header(header: dict) -> list[str]:
    sources = header.get("sources")
    if sources is None:
        return []
    if isinstance(sources, str):
        return [sources]
    if isinstance(sources, list) and all(isinstance(source, str) for source in sources):
        return sources
    raise ValueError("sources must be a string or a list of strings")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("page", help="YAML page id or skill-relative Markdown path")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--body-only", action="store_true", help="print only the Markdown body")
    mode.add_argument("--frontmatter-only", action="store_true", help="print the original YAML block, including delimiters")
    parser.add_argument("--follow-sources", action="store_true", help="append the first 500 characters of each local source")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if yaml is None:
        print("Error: PyYAML is required. Run this script with uv run.", file=sys.stderr)
        return 2
    try:
        page, warnings = get_page(args.page)
        sources = sources_from_header(page.header) if args.follow_sources else []
    except FileNotFoundError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    for warning in warnings:
        print(f"Warning: {warning}", file=sys.stderr)
    if args.body_only:
        output = page.body.lstrip("\r\n")
    elif args.frontmatter_only:
        output = page.frontmatter
    else:
        output = page.text
    sys.stdout.write(output)
    if output and not output.endswith("\n"):
        sys.stdout.write("\n")
    if args.follow_sources:
        if not sources:
            print("\n(No sources listed.)")
        for source in sources:
            print(f"\n## Source: {source}\n")
            try:
                preview = source_preview(source, page)
            except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
                print(f"Warning: source {source!r} unavailable: {error}", file=sys.stderr)
                print("(Source preview unavailable.)")
            else:
                print(preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

