#!/usr/bin/env python3
"""Guard the Watch Face renderer's dependency on the logical display canvas."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src/watchface/face_renderer.cpp"
IMAGE = ROOT / "src/watchface/elements/image.cpp"
CANVAS = ROOT / "src/display/display_canvas.h"
MANAGER = ROOT / "src/display/display_manager.cpp"


def main() -> int:
    errors = []
    renderer = RENDERER.read_text(encoding="utf-8")
    image = IMAGE.read_text(encoding="utf-8")
    canvas = CANVAS.read_text(encoding="utf-8")
    manager = MANAGER.read_text(encoding="utf-8")

    for label, source in (("face renderer", renderer), ("image element", image)):
        if "TFT_eSPI" in source or "tft." in source:
            errors.append(f"{label} depends directly on the TFT_eSPI driver")
        if "display_manager.h" in source:
            errors.append(f"{label} includes the physical display manager instead of the canvas contract")

    if "TFT_eSPI" in canvas:
        errors.append("DisplayCanvas interface must not include TFT_eSPI")
    if "class DisplayCanvas" not in canvas or "getDisplayCanvas()" not in canvas:
        errors.append("DisplayCanvas interface or accessor is missing")
    if "class TftDisplayCanvas final : public DisplayCanvas" not in manager:
        errors.append("GC9A01/TFT backend does not implement DisplayCanvas")

    if errors:
        print("[CANVAS-BOUNDARY] Validation failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("[CANVAS-BOUNDARY] Watch Face renderer and image element use DisplayCanvas.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
