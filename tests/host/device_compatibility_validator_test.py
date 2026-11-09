#!/usr/bin/env python3
"""Unit tests for exact device font-weight compatibility validation."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "validate_device_compatibility.py"
SPEC = importlib.util.spec_from_file_location("device_compatibility_validator", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> None:
    for supported in (400, 600, 400.0, 600.0):
        assert MODULE.is_supported_font_weight(supported), supported

    for unsupported in (400.5, 600.5, 300, 700, "400", True, False, float("nan"), float("inf")):
        assert not MODULE.is_supported_font_weight(unsupported), repr(unsupported)

    print("WFS exact font-weight compatibility validator tests passed.")


if __name__ == "__main__":
    main()
