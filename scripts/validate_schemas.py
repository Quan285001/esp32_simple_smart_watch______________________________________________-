#!/usr/bin/env python3
"""Validate Watch Face Studio JSON Schemas and committed golden fixtures."""
from __future__ import annotations

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "shared" / "schemas"
FIXTURES = [
    (SCHEMA_DIR / "watch-face.schema.json",
     SCHEMA_DIR / "examples" / "minimal-watch-face-v1.json",
     "minimal watch-face v1 fixture"),
    (SCHEMA_DIR / "device-profile.schema.json",
     ROOT / "shared" / "capabilities" / "esp32-c3-gc9a01-240.json",
     "ESP32-C3 GC9A01 device profile"),
    (SCHEMA_DIR / "watch-face-project.schema.json",
     SCHEMA_DIR / "examples" / "minimal-watch-face-project-v1.json",
     "portable watch-face project v1 fixture"),
]


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    schemas = {}
    for schema_path, _, _ in FIXTURES:
        if schema_path not in schemas:
            schema = load_json(schema_path)
            Draft202012Validator.check_schema(schema)
            schemas[schema_path] = schema
            print(f"[SCHEMA] Valid schema: {schema_path.relative_to(ROOT)}")

    failures = 0
    for schema_path, instance_path, label in FIXTURES:
        validator = Draft202012Validator(schemas[schema_path])
        errors = sorted(validator.iter_errors(load_json(instance_path)),
                        key=lambda error: list(map(str, error.absolute_path)))
        if errors:
            failures += len(errors)
            print(f"[FAIL] {label}")
            for error in errors:
                location = "/".join(map(str, error.absolute_path)) or "<root>"
                print(f"       {location}: {error.message}")
        else:
            print(f"[OK] {label}")

    if failures:
        print(f"[SCHEMA] {failures} validation error(s).")
        return 1
    print("[SCHEMA] All schemas and fixtures passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
