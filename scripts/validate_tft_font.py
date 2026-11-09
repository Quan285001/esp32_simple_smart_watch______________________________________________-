#!/usr/bin/env python3
"""Verify the browser GLCD font and the ESP32-C3 RGB565 SPI byte contract."""
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

sprite_path = font_path.parents[1] / "Extensions" / "Sprite.cpp"
tft_path = font_path.parents[1] / "TFT_eSPI.cpp"
processor_path = font_path.parents[1] / "Processors" / "TFT_eSPI_ESP32_C3.c"
display_path = ROOT / "src" / "display" / "display_manager.cpp"
for required in (sprite_path, tft_path, processor_path, display_path):
    if not required.is_file():
        print(f"ERROR: TFT image transport source not found: {required}")
        sys.exit(1)

sprite_source = sprite_path.read_text(encoding="utf-8", errors="replace")
tft_source = tft_path.read_text(encoding="utf-8", errors="replace")
processor_source = processor_path.read_text(encoding="utf-8", errors="replace")
display_source = display_path.read_text(encoding="utf-8", errors="replace")

sprite_start = sprite_source.find("void TFT_eSprite::pushSprite(int32_t x, int32_t y)")
if sprite_start < 0:
    print("ERROR: TFT_eSPI 16-bit sprite push implementation is missing")
    sys.exit(1)
sprite_end = sprite_source.find("\n}", sprite_start)
sprite_body = sprite_source[sprite_start:sprite_end + 2]
if "setSwapBytes(false)" not in sprite_body or "_tft->pushImage(" not in sprite_body:
    print("ERROR: TFT_eSPI sprite path contract changed; re-check sprite byte layout")
    sys.exit(1)

push_start = tft_source.find(
    "void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)"
)
if push_start < 0:
    print("ERROR: TFT_eSPI mutable 16-bit pushImage implementation is missing")
    sys.exit(1)
push_end = tft_source.find("\n}", push_start)
push_body = tft_source[push_start:push_end + 2]
if "pushPixels(data" not in push_body:
    print("ERROR: TFT_eSPI pushImage no longer forwards native words to pushPixels")
    sys.exit(1)

# GC9A01 uses the normal 16-bit SPI branch. Do not inspect the earlier
# ESP32-C3 parallel-GPIO implementation: it has different write macros.
spi_branch_marker = "#elif !defined (SPI_18BIT_DRIVER) && !defined (TFT_PARALLEL_8_BIT)"
spi_branch_start = processor_source.find(spi_branch_marker)
if spi_branch_start < 0:
    print("ERROR: ESP32-C3 standard SPI processor branch is missing")
    sys.exit(1)

push_pixels_start = processor_source.find(
    "void TFT_eSPI::pushPixels(const void* data_in, uint32_t len)", spi_branch_start
)
if push_pixels_start < 0:
    print("ERROR: ESP32-C3 standard SPI pushPixels implementation is missing")
    sys.exit(1)
push_pixels_end = processor_source.find("\n}", push_pixels_start)
push_pixels_body = processor_source[push_pixels_start:push_pixels_end + 2]
if ("if(_swapBytes)" not in push_pixels_body
        or "pushSwapBytePixels(data_in, len)" not in push_pixels_body
        or "uint32_t *data = (uint32_t*)data_in" not in push_pixels_body
        or "WRITE_PERI_REG(SPI_W0_REG(SPI_PORT),  *data++)" not in push_pixels_body):
    print("ERROR: ESP32-C3 standard SPI RGB565 transport contract changed")
    sys.exit(1)

swap_pixels_start = processor_source.find(
    "void TFT_eSPI::pushSwapBytePixels(const void* data_in, uint32_t len)", spi_branch_start
)
if swap_pixels_start < 0:
    print("ERROR: ESP32-C3 swapped-byte SPI helper is missing")
    sys.exit(1)
swap_pixels_end = processor_source.find("\n}", swap_pixels_start)
swap_pixels_body = processor_source[swap_pixels_start:swap_pixels_end + 2]
if "DAT8TO32(data)" not in swap_pixels_body:
    print("ERROR: ESP32-C3 swapped-byte SPI helper contract changed")
    sys.exit(1)

# RGB565LE payloads are decoded into native uint16_t words. The ESP32-C3
# standard SPI path already emits those native words in the required order.
# Enabling swapBytes selects the explicit byte-pair swap helper instead.
backend_start = display_source.find("void pushRgb565Row(")
if backend_start < 0:
    print("ERROR: WFS RGB565 display backend is missing")
    sys.exit(1)
backend_end = display_source.find("\n    }", backend_start)
backend_body = display_source[backend_start:backend_end + 6]
ordered_calls = [
    "tft.getSwapBytes()",
    "tft.setSwapBytes(false)",
    "tft.pushImage(x, y, count, 1, pixels)",
    "tft.setSwapBytes(previousSwapBytes)",
]
positions = [backend_body.find(token) for token in ordered_calls]
if any(position < 0 for position in positions) or positions != sorted(positions):
    print("ERROR: WFS RGB565 row transport does not preserve native SPI word order")
    sys.exit(1)

print("WFS RGB565LE native words use the ESP32-C3 normal SPI path (byte swapping disabled).")
