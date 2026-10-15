#!/usr/bin/env python3
"""Validate benchmark YAML files with schema/benchmark.schema.yml."""
from __future__ import annotations

import argparse
from datetime import datetime
import math
from pathlib import Path
import sys

from jsonschema import Draft202012Validator, FormatChecker
import yaml

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "schema/benchmark.schema.yml"


class UniqueKeyLoader(yaml.SafeLoader):
    """Reject duplicate keys instead of silently discarding earlier values."""

    def construct_mapping(self, node, deep=False):
        self.flatten_mapping(node)
        result = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            if not isinstance(key, str):
                raise ValueError(f"line {key_node.start_mark.line + 1}: mapping keys must be strings")
            if key in result:
                raise ValueError(f"line {key_node.start_mark.line + 1}: duplicate key {key!r}")
            result[key] = self.construct_object(value_node, deep=deep)
        return result


def load_yaml(path):
    with Path(path).open(encoding="utf-8") as stream:
        return yaml.load(stream, Loader=UniqueKeyLoader)


def finite_numbers(value, path="$"):
    if isinstance(value, float) and not math.isfinite(value):
        yield f"{path}: numbers must be finite"
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from finite_numbers(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from finite_numbers(item, f"{path}[{index}]")


def validator():
    schema = load_yaml(SCHEMA)
    Draft202012Validator.check_schema(schema)
    formats = FormatChecker()

    @formats.checks("date-time", raises=ValueError)
    def timestamp(value):
        return not isinstance(value, str) or datetime.fromisoformat(value.upper()).tzinfo is not None

    return Draft202012Validator(schema, format_checker=formats)


def validation_errors(document, check):
    yield from finite_numbers(document)
    for error in sorted(check.iter_errors(document), key=lambda e: str(list(e.absolute_path))):
        path = "$" + "".join(f"[{p}]" if isinstance(p, int) else f".{p}" for p in error.absolute_path)
        yield f"{path}: {error.message}"


def benchmark_files():
    # Include both extensions and named benchmark variants in every project.
    return sorted(path for path in (ROOT / "projects").rglob("*")
                  if path.is_file() and path.suffix in {".yml", ".yaml"}
                  and "benchmark" in path.stem)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path, help="Default: all *benchmark*.yml/yaml under projects/")
    args = parser.parse_args(argv)
    files = args.files or benchmark_files()
    if not files:
        parser.error("no benchmark YAML files found")
    check = validator()
    failed = False
    for path in files:
        try:
            errors = list(validation_errors(load_yaml(path), check))
        except (OSError, ValueError, yaml.YAMLError) as error:
            errors = [str(error)]
        if errors:
            failed = True
            for error in errors:
                print(f"{path}: {error}", file=sys.stderr)
        else:
            print(f"OK {path}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
