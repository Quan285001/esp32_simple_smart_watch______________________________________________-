#!/usr/bin/env python3
"""Keep the firmware's active display profile aligned with the shared device profile."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "src/display/display_profile.h").read_text(encoding="utf-8")
PROFILE = json.loads(
    (ROOT / "shared/capabilities/esp32-c3-gc9a01-240.json").read_text(encoding="utf-8")
)

def string_constant(name):
    match = re.search(rf'static const char {name}\[\] = "([^"]+)";', HEADER)
    if not match:
        raise AssertionError(f"Missing firmware profile string: {name}")
    return match.group(1)

def integer_constant(name):
    match = re.search(rf"static const uint16_t {name} = (\d+);", HEADER)
    if not match:
        match = re.search(rf"static const uint8_t {name} = (\d+);", HEADER)
    if not match:
        raise AssertionError(f"Missing firmware profile integer: {name}")
    return int(match.group(1))

display = PROFILE["display"]
limits = PROFILE["nominalLimits"]
checks = {
    "profile id": (string_constant("ID"), PROFILE["id"]),
    "display width": (integer_constant("WIDTH"), display["width"]),
    "display height": (integer_constant("HEIGHT"), display["height"]),
    "maximum bitmap width": (integer_constant("MAX_BITMAP_WIDTH"), limits["maxBitmapWidth"]),
    "maximum bitmap height": (integer_constant("MAX_BITMAP_HEIGHT"), limits["maxBitmapHeight"]),
    "maximum objects": (integer_constant("MAX_OBJECTS"), limits["maxObjects"]),
    "maximum assets": (integer_constant("MAX_ASSETS"), limits["maxAssets"]),
}
for label, (firmware_value, shared_value) in checks.items():
    if firmware_value != shared_value:
        raise AssertionError(
            f"{label} mismatch: firmware={firmware_value!r}, shared={shared_value!r}"
        )

shape_flag = re.search(r"static const bool IS_CIRCULAR = (true|false);", HEADER)
if not shape_flag:
    raise AssertionError("Missing firmware display shape flag")
if (shape_flag.group(1) == "true") != (display["shape"] == "circle"):
    raise AssertionError("Display shape mismatch between firmware and shared profile")


# Keep the WFS capability payload and Web preflight on the same contract.
BLE_SOURCE = (ROOT / "src/communication/ble_manager.cpp").read_text(encoding="utf-8")
WEB_SOURCE = (ROOT / "web/app.js").read_text(encoding="utf-8")
for key in ("pid", "cw", "ch", "cs", "bw", "bh", "obj", "assets", "json", "img", "chunk", "stage"):
    if f'\\\"{key}\\\"' not in BLE_SOURCE:
        raise AssertionError(f"WFS firmware capability payload is missing {key!r}")
    if key != "chunk" and f"caps?.{key}" not in WEB_SOURCE and f"caps.{key}" not in WEB_SOURCE:
        raise AssertionError(f"Web compatibility preflight does not consume {key!r}")

protocol_header = (ROOT / "src/communication/protocol.h").read_text(encoding="utf-8")
payload_limit_match = re.search(r"MAX_CONTROL_PAYLOAD = (\d+)", protocol_header)
if not payload_limit_match:
    raise AssertionError("Missing WFS control payload limit")
payload_limit = int(payload_limit_match.group(1))
worst_case_payload = {
    "sv": 1, "pv": 1, "pid": PROFILE["id"],
    "cw": display["width"], "ch": display["height"], "cs": display["shape"],
    "bw": limits["maxBitmapWidth"], "bh": limits["maxBitmapHeight"],
    "obj": limits["maxObjects"], "assets": limits["maxAssets"],
    "json": 16384, "img": 1, "upload": 1, "chunk": 216,
    "stage": 4294967295,
}
payload_size = len(json.dumps(worst_case_payload, separators=(",", ":")).encode("utf-8"))
if payload_size > payload_limit:
    raise AssertionError(
        f"WFS capability JSON exceeds control payload limit: {payload_size} > {payload_limit}"
    )

print("Firmware/shared display profile and WFS capability contracts passed.")
