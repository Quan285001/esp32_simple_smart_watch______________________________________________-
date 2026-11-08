#!/usr/bin/env python3
"""Check that the committed golden face fixture fits the ESP32-C3 renderer profile."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE_PATH = ROOT / "shared/capabilities/esp32-c3-gc9a01-240.json"
FIXTURE_PATH = ROOT / "shared/schemas/examples/minimal-watch-face-v1.json"

SUPPORTED_TYPES = {
    "text", "digital-clock", "analog-clock", "date", "weather", "shape", "image"
}
SUPPORTED_WEATHER_FIELDS = {"temperature", "condition"}
SUPPORTED_SHAPES = {"rect", "circle", "rounded-rect", "line"}
SUPPORTED_FONTS = {"system-ui", "sans-serif", "inherit"}


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    profile = load_json(PROFILE_PATH)
    design = load_json(FIXTURE_PATH)
    display = profile["display"]
    limits = profile.get("nominalLimits", {})
    errors: list[str] = []

    if design.get("deviceProfileId") != profile.get("id"):
        errors.append("fixture targets a different device profile")

    canvas = design.get("canvas", {})
    if (canvas.get("width"), canvas.get("height")) != (
        display.get("width"), display.get("height")
    ):
        errors.append("fixture canvas dimensions do not match the device display")
    if canvas.get("shape") != display.get("shape"):
        errors.append("fixture canvas shape does not match the device display")

    objects = design.get("objects", [])
    max_objects = limits.get("maxObjects", 16)
    if len(objects) > max_objects:
        errors.append(f"fixture has {len(objects)} objects; device limit is {max_objects}")

    for index, obj in enumerate(objects):
        label = obj.get("name") or obj.get("id") or f"object[{index}]"
        element_type = obj.get("type")
        transform = obj.get("transform", {})
        text_style = obj.get("textStyle", {})
        binding = obj.get("dataBinding", {})

        if element_type not in SUPPORTED_TYPES:
            errors.append(f"{label}: unsupported element type {element_type!r}")

        if transform.get("anchor", "top-left") != "top-left":
            errors.append(f"{label}: only top-left anchoring is supported")
        if transform.get("rotation", 0) != 0:
            errors.append(f"{label}: object rotation is unsupported")
        if transform.get("opacity", 1) < 0.999:
            errors.append(f"{label}: partial object opacity is unsupported")

        if element_type == "shape" and obj.get("shape", "rect") not in SUPPORTED_SHAPES:
            errors.append(f"{label}: unsupported shape kind")
        if element_type == "weather":
            fields = set(obj.get("weatherFields", ["temperature"]))
            if not fields.issubset(SUPPORTED_WEATHER_FIELDS):
                errors.append(f"{label}: unsupported weather field(s) {sorted(fields - SUPPORTED_WEATHER_FIELDS)}")

        if element_type == "image":
            width = transform.get("width", 0)
            height = transform.get("height", 0)
            if width > limits.get("maxBitmapWidth", 240) or height > limits.get("maxBitmapHeight", 240):
                errors.append(f"{label}: image exceeds device bitmap dimensions")
            if not obj.get("assetId"):
                errors.append(f"{label}: image has no assetId")

        if text_style:
            weight = int(text_style.get("fontWeight", 400))
            if weight not in (400, 600):
                errors.append(f"{label}: renderer supports only font weights 400 and 600")
            size = float(text_style.get("fontSize", 16))
            if not 0 < size <= 256:
                errors.append(f"{label}: font size is outside renderer limits")
            for axis in ("scaleX", "scaleY"):
                scale = float(text_style.get(axis, 1))
                if not 0.1 <= scale <= 8:
                    errors.append(f"{label}: {axis} is outside renderer limits")
            font = text_style.get("fontFamily")
            if font and font not in SUPPORTED_FONTS:
                errors.append(f"{label}: font family {font!r} is not available on device")
            text = obj.get("text", "")
            if any(ord(char) < 32 or ord(char) > 126 for char in text):
                errors.append(f"{label}: renderer currently accepts printable ASCII only")

        expected_binding = {
            "digital-clock": "time",
            "date": "date",
            "weather": "temperature",
        }.get(element_type)
        if expected_binding and binding.get("source") != expected_binding:
            errors.append(f"{label}: expected data binding source {expected_binding!r}")

    if errors:
        print("[DEVICE-COMPAT] Fixture is not compatible with the device profile:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(
        "[DEVICE-COMPAT] Golden fixture fits "
        f"{profile['id']} ({display['width']}x{display['height']}, "
        f"{max_objects} objects max)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
