#!/usr/bin/env python3
"""Validate network YAML files with schema/network.schema.yml."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

from jsonschema import Draft202012Validator
import yaml

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "schema/network.schema.yml"


class UniqueKeyLoader(yaml.SafeLoader):
    """Reject duplicate and non-string keys before schema validation."""

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


def validator():
    schema = load_yaml(SCHEMA)
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def validation_errors(document, check):
    for error in sorted(check.iter_errors(document), key=lambda e: str(list(e.absolute_path))):
        path = "$" + "".join(f"[{p}]" if isinstance(p, int) else f".{p}" for p in error.absolute_path)
        yield f"{path}: {error.message}"


def load_network(path):
    """Read validated settings, preserving network-dependent error measurements."""
    document = load_yaml(path)
    errors = list(validation_errors(document, validator()))
    if errors:
        raise ValueError("\n".join(f"{path}: {error}" for error in errors))
    return document["network"]


def network_files():
    return sorted(path for path in (ROOT / "projects").rglob("*")
                  if path.is_file() and path.name in {"network.yml", "network.yaml"})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path, help="Default: all network.yml/yaml under projects/")
    args = parser.parse_args(argv)
    files = args.files or network_files()
    if not files:
        parser.error("no network YAML files found")
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
