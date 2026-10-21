#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Search local wiki bodies and source PR descriptions.

Run from helium-wiki (the installed skill root):
    uv run scripts/grep_wiki.py "tcgen05.fence"
    uv run scripts/grep_wiki.py "2-CTA backward" --only wiki
    uv run scripts/grep_wiki.py "ping-pong" --context 3
    uv run scripts/grep_wiki.py "nvfp4 block_scale" --any

Searches Markdown files under wiki/ and sources/, excluding YAML frontmatter.
Queries are case-insensitive literal phrases by default. Multiple phrases must
all appear on the same line. --any splits phrases into whitespace-separated
words and matches any word. --regex treats each argument as a regular expression;
with --any, any expression may match (expressions are not split into words).

Matches use path:line:text; context uses path-line-text. Line numbers refer to
the original file. Overlapping context is merged, and separate groups use --.
Paths are relative to the skill root, resolved from this script's location.
No network requests are made. Missing optional sources/ is ignored.
Exit codes: 0 = matches, 1 = no matches, 2 = invalid input or a read error.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SKILL_ROOT = Path(__file__).resolve().parent.parent


@dataclass
class SearchResult:
    path: Path
    lines: list[str]
    offset: int
    matches: list[int]


def compile_patterns(queries: list[str], any_word=False, regex=False) -> list[re.Pattern]:
    if not queries or any(not query.strip() for query in queries):
        raise ValueError("search text must not be empty")
    terms = [word for query in queries for word in query.split()] if any_word and not regex else queries
    return [re.compile(term if regex else re.escape(term), re.IGNORECASE) for term in terms]


def read_body_lines(file: Path) -> tuple[list[str], int]:
    lines = file.read_text(encoding="utf-8-sig").splitlines()
    if lines and lines[0].rstrip() == "---":
        for index in range(1, len(lines)):
            if lines[index].rstrip() in ("---", "..."):
                return lines[index + 1 :], index + 1
        raise ValueError("missing YAML frontmatter closing delimiter")
    return lines, 0


def search_files(
    patterns: list[re.Pattern], only="all", any_word=False, skill_root: Path = SKILL_ROOT
) -> tuple[list[SearchResult], list[str]]:
    skill_root = skill_root.resolve()
    folders = ("wiki", "sources") if only == "all" else (only,)
    directories = [skill_root / folder for folder in folders if (skill_root / folder).is_dir()]
    if not directories:
        raise ValueError("no selected wiki or sources directory exists inside the skill folder")
    results, warnings = [], []
    files = sorted(file for directory in directories for file in directory.rglob("*.md"))
    for file in files:
        try:
            if not file.resolve().is_relative_to(skill_root):
                raise ValueError("file must stay inside the skill folder")
            lines, offset = read_body_lines(file)
            matches = [
                index for index, line in enumerate(lines)
                if (any if any_word else all)(pattern.search(line) is not None for pattern in patterns)
            ]
            if matches:
                results.append(SearchResult(file, lines, offset, matches))
        except (OSError, UnicodeError, ValueError) as error:
            warnings.append(f"Skipping {file.relative_to(skill_root).as_posix()}: {error}")
    return results, warnings


def context_ranges(matches: list[int], line_count: int, context: int) -> list[tuple[int, int]]:
    ranges = []
    for index in matches:
        start, end = max(0, index - context), min(line_count, index + context + 1)
        if ranges and start <= ranges[-1][1]:
            ranges[-1] = (ranges[-1][0], max(ranges[-1][1], end))
        else:
            ranges.append((start, end))
    return ranges


def render_results(results: list[SearchResult], context=0, skill_root: Path = SKILL_ROOT) -> str:
    groups = []
    for result in results:
        path = result.path.relative_to(skill_root.resolve()).as_posix()
        matches = set(result.matches)
        for start, end in context_ranges(result.matches, len(result.lines), context):
            group = []
            for index in range(start, end):
                separator = ":" if index in matches else "-"
                number = result.offset + index + 1
                group.append(f"{path}{separator}{number}{separator}{result.lines[index]}")
            groups.append("\n".join(group))
    return "\n--\n".join(groups)


def nonnegative(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("context must be a nonnegative integer") from None
    if number < 0:
        raise argparse.ArgumentTypeError("context must be a nonnegative integer")
    return number


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("query", nargs="+", help="literal phrase(s), or expressions with --regex")
    parser.add_argument("--only", choices=("all", "wiki", "sources"), default="all", help="folders to search (default: all)")
    parser.add_argument("--context", type=nonnegative, default=0, metavar="N", help="lines before and after each match (default: 0)")
    parser.add_argument("--any", dest="any_word", action="store_true", help="match any word, or any expression with --regex")
    parser.add_argument("--regex", action="store_true", help="interpret each query argument as a regular expression")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        patterns = compile_patterns(args.query, args.any_word, args.regex)
        results, warnings = search_files(patterns, args.only, args.any_word, SKILL_ROOT)
    except (OSError, ValueError, re.error) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    for warning in warnings:
        print(f"Warning: {warning}", file=sys.stderr)
    output = render_results(results, args.context, SKILL_ROOT)
    if output:
        print(output)
    if warnings:
        return 2
    return 0 if results else 1


if __name__ == "__main__":
    raise SystemExit(main())
