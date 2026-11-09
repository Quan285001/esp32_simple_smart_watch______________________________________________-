#!/usr/bin/env python3
"""Ensure the browser preview embeds the exact GLCD font used by TFT_eSPI."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
font_path = ROOT / ".pio" / "libdeps" / "c3_clock" / "TFT_eSPI" / "Fonts" / "glcdfont.c"
web_path = ROOT / "web" / "app.js"

if not font_path.is_file():
    print(f"ERROR: TFT_eSPI GLCD font source not found: {font_path}")
    sys.exit(1)

font_source = font_path.read_text(encoding="utf-8", errors="replace")
font_match = re.search(
    r"static\s+const\s+unsigned\s+char\s+font\[\]\s+PROGMEM\s*=\s*\{(.*?)\};",
    font_source,
    re.DOTALL,
)
if not font_match:
    print("ERROR: could not locate TFT_eSPI GLCD font table")
    sys.exit(1)

font_bytes = bytes(int(value, 16) for value in re.findall(
    r"0x([0-9a-fA-F]{1,2})", font_match.group(1)
))
if len(font_bytes) != 256 * 5:
    print(f"ERROR: TFT_eSPI GLCD font has {len(font_bytes)} bytes; expected 1280")
    sys.exit(1)

web_source = web_path.read_text(encoding="utf-8")
web_match = re.search(r"const\s+TFT_FONT1_GLYPHS='([0-9a-fA-F]+)'", web_source)
if not web_match:
    print("ERROR: embedded browser GLCD font table is missing")
    sys.exit(1)

web_bytes = bytes.fromhex(web_match.group(1))
if web_bytes != font_bytes:
    mismatch = next((i for i, (left, right) in enumerate(zip(web_bytes, font_bytes))
                     if left != right), min(len(web_bytes), len(font_bytes)))
    print(f"ERROR: browser GLCD font differs from TFT_eSPI at byte {mismatch}")
    sys.exit(1)

print("Browser GLCD font exactly matches the installed TFT_eSPI library (256 glyphs).")

# The WFS image backend must follow the same native-word byte-order contract
# as TFT_eSPI's own 16-bit sprite push path. This is a source-level check only;
# it does not claim to verify the physical panel's RGB/BGR configuration.
sprite_path = font_path.parents[1] / "Extensions" / "Sprite.cpp"
tft_path = font_path.parents[1] / "TFT_eSPI.cpp"
display_path = ROOT / "src" / "display" / "display_manager.cpp"
for required in (sprite_path, tft_path, display_path):
    if not required.is_file():
        print(f"ERROR: TFT image transport source not found: {required}")
        sys.exit(1)

sprite_source = sprite_path.read_text(encoding="utf-8", errors="replace")
tft_source = tft_path.read_text(encoding="utf-8", errors="replace")
display_source = display_path.read_text(encoding="utf-8", errors="replace")

sprite_start = sprite_source.find("void TFT_eSprite::pushSprite(int32_t x, int32_t y)")
if sprite_start < 0:
    print("ERROR: TFT_eSPI 16-bit sprite push implementation is missing")
    sys.exit(1)
sprite_body = sprite_source[sprite_start:sprite_source.find("\n}", sprite_start) + 2]
if "setSwapBytes(false)" not in sprite_body or "_tft->pushImage(" not in sprite_body:
    print("ERROR: TFT_eSPI sprite path no longer documents native-word push semantics")
    sys.exit(1)

push_start = tft_source.find(
    "void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)"
)
if push_start < 0:
    print("ERROR: TFT_eSPI mutable 16-bit pushImage implementation is missing")
    sys.exit(1)
push_body = tft_source[push_start:tft_source.find("\n}", push_start) + 2]
if "pushPixels(data" not in push_body:
    print("ERROR: TFT_eSPI pushImage no longer forwards native words to pushPixels")
    sys.exit(1)

backend_start = display_source.find("void pushRgb565Row(")
backend_body = display_source[backend_start:display_source.find("\\n    }", backend_start) + 6]
ordered_calls = [
    "tft.getSwapBytes()",
    "tft.setSwapBytes(false)",
    "tft.pushImage(x, y, count, 1, pixels)",
    "tft.setSwapBytes(previousSwapBytes)",
]
positions = [backend_body.find(token) for token in ordered_calls]
if any(position < 0 for position in positions) or positions != sorted(positions):
    print("ERROR: WFS RGB565 row transport does not preserve TFT_eSPI native-word swap state")
    sys.exit(1)

print("WFS RGB565 transport matches TFT_eSPI's native 16-bit sprite push convention.")

