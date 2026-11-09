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
processor_header_path = font_path.parents[1] / "Processors" / "TFT_eSPI_ESP32_C3.h"
display_path = ROOT / "src" / "display" / "display_manager.cpp"
for required in (sprite_path, tft_path, processor_path, processor_header_path, display_path):
    if not required.is_file():
        print(f"ERROR: TFT image transport source not found: {required}")
        sys.exit(1)

sprite_source = sprite_path.read_text(encoding="utf-8", errors="replace")
tft_source = tft_path.read_text(encoding="utf-8", errors="replace")
processor_source = processor_path.read_text(encoding="utf-8", errors="replace")
processor_header = processor_header_path.read_text(encoding="utf-8", errors="replace")
display_source = display_path.read_text(encoding="utf-8", errors="replace")

# Sprite buffers are not native uint16_t RGB565 memory: drawPixel swaps the
# stored word first, so pushSprite() correctly disables the later byte swap.
sprite_draw_start = sprite_source.find("void TFT_eSprite::drawPixel(int32_t x, int32_t y, uint32_t color)")
if sprite_draw_start < 0:
    print("ERROR: TFT_eSPI 16-bit sprite drawPixel implementation is missing")
    sys.exit(1)
sprite_draw_end = sprite_source.find("\n}", sprite_draw_start)
sprite_draw_body = sprite_source[sprite_draw_start:sprite_draw_end + 2]
if ("color = (color >> 8) | (color << 8)" not in sprite_draw_body
        or "_img[x+y*_iwidth] = (uint16_t) color" not in sprite_draw_body):
    print("ERROR: TFT_eSPI sprite storage byte layout changed")
    sys.exit(1)

sprite_push_start = sprite_source.find("void TFT_eSprite::pushSprite(int32_t x, int32_t y)")
if sprite_push_start < 0:
    print("ERROR: TFT_eSPI sprite push implementation is missing")
    sys.exit(1)
sprite_push_end = sprite_source.find("\n}", sprite_push_start)
sprite_push_body = sprite_source[sprite_push_start:sprite_push_end + 2]
if "setSwapBytes(false)" not in sprite_push_body or "_tft->pushImage(" not in sprite_push_body:
    print("ERROR: TFT_eSPI sprite push no longer matches its pre-swapped storage")
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

# GC9A01 uses the normal 16-bit SPI branch. The earlier ESP32-C3 parallel
# GPIO branch is unrelated and must not be used to infer SPI byte order.
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
        or "uint32_t *data = (uint32_t*)data_in" not in push_pixels_body):
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

# DAT8TO32 converts each low-byte/high-byte pair in little-endian RAM into
# high-byte/low-byte order in the SPI FIFO's byte stream.
dat8_start = processor_header.find("#define DAT8TO32(P)")
if dat8_start < 0:
    print("ERROR: ESP32-C3 SPI byte-pair packing macro is missing")
    sys.exit(1)
dat8_body = processor_header[dat8_start:dat8_start + 150]
dat8_tokens = ["P[0]<<8", "P[1]", "P[2]<<24", "P[3]<<16"]
dat8_positions = [dat8_body.find(token) for token in dat8_tokens]
if any(position < 0 for position in dat8_positions) or dat8_positions != sorted(dat8_positions):
    print("ERROR: ESP32-C3 SPI byte-pair packing order changed")
    sys.exit(1)

# WFS payloads decode to native uint16_t words whose memory bytes are LE.
# Therefore use the byte-pair swap helper before the panel's MSB-first pixels.
backend_start = display_source.find("void pushRgb565Row(")
if backend_start < 0:
    print("ERROR: WFS RGB565 display backend is missing")
    sys.exit(1)
backend_end = display_source.find("\n    }", backend_start)
backend_body = display_source[backend_start:backend_end + 6]
ordered_calls = [
    "tft.getSwapBytes()",
    "tft.setSwapBytes(true)",
    "tft.pushImage(x, y, count, 1, pixels)",
    "tft.setSwapBytes(previousSwapBytes)",
]
positions = [backend_body.find(token) for token in ordered_calls]
if any(position < 0 for position in positions) or positions != sorted(positions):
    print("ERROR: WFS RGB565LE rows are not byte-swapped before SPI output")
    sys.exit(1)

print("WFS RGB565LE native words are byte-swapped into the SPI pixel order.")
