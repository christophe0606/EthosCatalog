#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["PyYAML>=6.0"]
# ///
"""Search wiki YAML frontmatter, with ranked keywords and metadata filters.

Run from helium-wiki (the installed skill root):
    uv run scripts/query.py "how to enable caches on m55"
    uv run scripts/query.py --tag helium --type techniques
    uv run scripts/query.py --repo cutlass --language cute-dsl
    uv run scripts/query.py --symptom memory-bound

Page aliases live in data/aliases.yaml: page ID -> alias list, generated from
wiki headers by the repository's scripts/gen_aliases.py. They participate in
keyword ranking. Metadata alias groups are also accepted when configured.
Different filters, and repeated occurrences of a filter, are ANDed.
Tag filters search tags and architectures; language/symptom filters also
search tags. Language-page IDs such as languages-c-... identify a language.
Paths are relative to the skill root, such as wiki/techniques/example.md.
Wiki pages and alias configuration are resolved relative to this script.
Keywords search headers only, not Markdown bodies. Missing optional fields
simply do not match their filters. Exit codes: 0 = success (including no
matches), 2 = invalid arguments/configuration or an unreadable wiki.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

SKILL_ROOT = Path(__file__).resolve().parent.parent
WIKI_ROOT = SKILL_ROOT / "wiki"
ALIASES_PATH = SKILL_ROOT / "data" / "aliases.yaml"

FILTER_FIELDS = {
    "type": ("type",),
    "tag": ("tags", "architectures", "architecture"),
    "language": ("languages", "language", "tags"),
    "architecture": ("architectures", "architecture"),
    "symptom": ("symptoms", "symptom", "performance_symptoms", "tags"),
    "repo": ("repo", "repos", "repository", "repositories"),
    "confidence": ("confidence",),
}
ALIAS_CATEGORIES = {
    "type": "types",
    "tag": "tags",
    "language": "languages",
    "architecture": "architectures",
    "symptom": "symptoms",
    "repo": "repos",
    "confidence": "confidence",
}
STOPWORDS = frozenset(
    "a an and are as at be by can do does for from how i in is it of on or "
    "the this to use using what when where which with would".split()
)
SEARCH_FIELDS = {
    "title": 6,
    "aliases": 5,
    "id": 3,
    "tags": 4,
    "architectures": 3,
    "architecture": 3,
    "type": 1,
    "language": 3,
    "languages": 3,
    "repo": 3,
    "repos": 3,
    "repository": 3,
    "repositories": 3,
    "symptom": 4,
    "symptoms": 4,
    "performance_symptoms": 4,
    "confidence": 1,
    "summary": 2,
    "description": 2,
    "prerequisites": 1,
    "evidence_basis": 1,
}
FIELD_CATEGORY = {
    name: ALIAS_CATEGORIES[field]
    for field, names in FILTER_FIELDS.items()
    for name in names
    if name != "tags"
}
FIELD_CATEGORY["tags"] = "tags"


def normalize(value: str) -> str:
    return value.strip().casefold()


def strings(value) -> list[str]:
    if isinstance(value, str):
        return [value]
    if isinstance(value, (list, tuple)):
        return [item for item in value if isinstance(item, str)]
    return []


def tokenize(text: str) -> list[str]:
    """Split identifiers as well as prose; lightly normalize common plurals."""
    words = re.findall(r"[a-z0-9]+(?:\.[a-z0-9]+)*(?:\+\+)?", text.casefold())
    return [
        word[:-1] if len(word) > 3 and word.endswith("s") and not word.endswith("ss") else word
        for word in words
        if word not in STOPWORDS
    ]


class AliasMap:
    def __init__(self, configuration=None):
        self.groups: dict[str, dict[str, set[str]]] = {}
        self.page_aliases: dict[str, set[str]] = {}
        for category, definitions in (configuration or {}).items():
            if category not in set(ALIAS_CATEGORIES.values()) | {"pages"}:
                raise ValueError(f"Unknown alias category: {category!r}")
            if not isinstance(definitions, dict):
                raise ValueError(f"Alias category {category!r} must be a mapping")
            graph: dict[str, set[str]] = {}
            for canonical, aliases in definitions.items():
                if not isinstance(canonical, str) or not canonical.strip():
                    raise ValueError(f"Invalid canonical value in {category!r}")
                if not isinstance(aliases, list) or any(
                    not isinstance(alias, str) or not alias.strip() for alias in aliases
                ):
                    raise ValueError(f"Aliases for {category}.{canonical} must be a list of strings")
                if category == "pages":
                    self.page_aliases[normalize(canonical)] = {normalize(alias) for alias in aliases}
                    continue
                group = {normalize(canonical), *(normalize(alias) for alias in aliases)}
                for value in group:
                    graph.setdefault(value, set()).update(group)
            self.groups[category] = graph

    def expand(self, category: str, value: str) -> set[str]:
        """Follow overlapping groups transitively; unknown values match themselves."""
        found = set()
        pending = [normalize(value)]
        graph = self.groups.get(category, {})
        while pending:
            candidate = pending.pop()
            if candidate not in found:
                found.add(candidate)
                pending.extend(graph.get(candidate, set()) - found)
        return found


def load_aliases(file: Path = ALIASES_PATH) -> AliasMap:
    configuration = yaml.safe_load(file.read_text(encoding="utf-8-sig"))
    if configuration is None:
        configuration = {}
    if not isinstance(configuration, dict):
        raise ValueError("data/aliases.yaml must contain a mapping")
    if all(isinstance(aliases, list) for aliases in configuration.values()):
        configuration = {"pages": configuration}
    return AliasMap(configuration)


@dataclass
class Page:
    path: Path
    header: dict

    @property
    def title(self) -> str:
        return self.header.get("title") if isinstance(self.header.get("title"), str) else self.path.stem

    @property
    def id(self) -> str:
        return self.header.get("id") if isinstance(self.header.get("id"), str) else self.path.stem


@dataclass
class Match:
    page: Page
    score: float
    matched_terms: int


def load_pages(directory: Path = WIKI_ROOT) -> tuple[list[Page], list[str]]:
    if not directory.is_dir():
        raise ValueError(f"Wiki directory does not exist: {directory}")
    pages = []
    warnings = []
    for file in sorted(directory.rglob("*.md")):
        try:
            with file.open(encoding="utf-8-sig") as stream:
                if stream.readline().rstrip() != "---":
                    raise ValueError("missing YAML frontmatter opening delimiter")
                lines = []
                for line in stream:
                    if line.rstrip() in ("---", "..."):
                        break
                    lines.append(line)
                else:
                    raise ValueError("missing YAML frontmatter closing delimiter")
            header = yaml.safe_load("".join(lines))
            if not isinstance(header, dict):
                raise ValueError("frontmatter must be a YAML mapping")
            pages.append(Page(file, header))
        except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
            warnings.append(f"Skipping {file}: {error}")
    return pages, warnings


def matches_filter(page: Page, field: str, value: str, aliases: AliasMap) -> bool:
    requested = aliases.expand(ALIAS_CATEGORIES[field], value)
    if field == "tag":
        requested |= aliases.expand("architectures", value)
    candidates = {
        normalize(item)
        for key in FILTER_FIELDS[field]
        for item in strings(page.header.get(key))
    }
    if candidates & requested:
        return True
    if field == "language" and normalize(str(page.header.get("type", ""))) == "languages":
        page_id = normalize(page.id)
        return any(page_id.startswith(f"languages-{language}-") for language in requested)
    return False


def keyword_weights(page: Page, aliases: AliasMap) -> dict[str, float]:
    weights: Counter = Counter()
    for field, weight in SEARCH_FIELDS.items():
        values = strings(page.header.get(field))
        if field == "aliases":
            values = sorted({normalize(value) for value in values} | aliases.page_aliases.get(normalize(page.id), set()))
        for value in values:
            expanded = {value}
            if field in FIELD_CATEGORY:
                expanded |= aliases.expand(FIELD_CATEGORY[field], value)
            # Count each token once per value so alias lists cannot inflate ranking.
            for term in {word for text in expanded for word in tokenize(text)}:
                weights[term] += weight
    return weights


def search_pages(
    pages: list[Page], query: str, filters: dict[str, list[str]], aliases: AliasMap
) -> list[Match]:
    terms = set(tokenize(query))
    if query.strip() and not terms:
        raise ValueError("Query contains no searchable keywords")
    indexed = [(page, keyword_weights(page, aliases)) for page in pages]
    frequency = Counter(term for _, weights in indexed for term in weights)
    matches = []
    for page, weights in indexed:
        if any(
            not matches_filter(page, field, value, aliases)
            for field, values in filters.items()
            for value in values
        ):
            continue
        matched = terms & weights.keys()
        if terms and not matched:
            continue
        score = sum(
            weights[term] * (1 + math.log((1 + len(pages)) / (1 + frequency[term])))
            for term in matched
        )
        if terms:
            score *= 1 + len(matched) / len(terms)
        matches.append(Match(page, score, len(matched)))
    # Balance query coverage with weighted/rarer terms, then use stable ties.
    return sorted(
        matches,
        key=lambda match: (
            -match.score, -match.matched_terms, match.page.title.casefold(),
            match.page.path.as_posix(),
        ),
    )


def display_path(file: Path) -> str:
    try:
        return file.relative_to(SKILL_ROOT).as_posix()
    except ValueError:
        return file.as_posix()


def render_results(matches: list[Match], compact=False, paths_only=False) -> str:
    if paths_only:
        return "\n".join(display_path(match.page.path) for match in matches)
    if not matches:
        return "No matching pages."
    lines = []
    for number, match in enumerate(matches, 1):
        page = match.page
        header = page.header
        kind = ", ".join(strings(header.get("type"))) or "unknown"
        confidence = ", ".join(strings(header.get("confidence"))) or "unknown"
        if compact:
            lines.append(
                f"{number}. {page.title} [{kind}; {confidence}; score={match.score:.2f}]"
                f" | {display_path(page.path)}"
            )
            continue
        lines += [
            f"{number}. {page.title}",
            f"   Path: {display_path(page.path)}",
            f"   ID: {page.id}",
            f"   Type: {kind} | Confidence: {confidence} | Score: {match.score:.2f}",
        ]
        for label, keys in (
            ("Architectures", ("architectures", "architecture")),
            ("Tags", ("tags",)),
            ("Languages", ("languages", "language")),
            ("Repos", ("repo", "repos", "repository", "repositories")),
            ("Symptoms", ("symptom", "symptoms", "performance_symptoms")),
            ("Reproducibility", ("reproducibility",)),
        ):
            values = [value for key in keys for value in strings(header.get(key))]
            if values:
                lines.append(f"   {label}: {', '.join(values)}")
        lines.append("")
    return "\n".join(lines).rstrip()


def positive_integer(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a positive integer") from None
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Search and rank wiki YAML headers. Aliases: data/aliases.yaml.",
        epilog="Repeat a filter to require all values. Tag filters also inspect architectures.",
    )
    parser.add_argument("query", nargs="?", default="", help="natural-language keywords")
    for field in FILTER_FIELDS:
        parser.add_argument(
            f"--{field}", action="append", default=[], metavar="VALUE",
            help=f"filter by {field}; repeatable (AND)",
        )
    parser.add_argument("--limit", type=positive_integer, default=10, help="maximum results (default: 10)")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--compact", action="store_true", help="one line per result")
    output.add_argument("--paths-only", action="store_true", help="only skill-relative paths; no-match output is empty")
    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if yaml is None:
        print("Error: PyYAML is required. Run this script with uv run.", file=sys.stderr)
        return 2
    try:
        aliases = load_aliases()
        pages, warnings = load_pages()
        for warning in warnings:
            print(f"Warning: {warning}", file=sys.stderr)
        if warnings and not pages:
            raise ValueError("No readable wiki pages")
        filters = {field: getattr(args, field) for field in FILTER_FIELDS}
        matches = search_pages(pages, args.query, filters, aliases)[:args.limit]
    except (OSError, UnicodeError, ValueError, yaml.YAMLError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 2
    output = render_results(matches, compact=args.compact, paths_only=args.paths_only)
    if output:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

