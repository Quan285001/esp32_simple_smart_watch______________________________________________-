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
