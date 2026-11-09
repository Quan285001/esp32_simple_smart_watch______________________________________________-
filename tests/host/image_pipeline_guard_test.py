#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "watchface" / "elements" / "image.cpp").read_text(encoding="utf-8")
assert "asset->byteLength != static_cast<size_t>(asset->width) * asset->height * sizeof(uint16_t)" in SOURCE
assert "const size_t fileSize = file.size();" in SOURCE
assert "asset->byteLength > fileSize - asset->packageOffset" in SOURCE
assert "file.seek(asset->packageOffset, fs::SeekSet)" in SOURCE
assert "decodeRgb565LittleEndianRow(encodedRow, rowBytes, row, asset->width)" in SOURCE
assert "uint8_t encodedRow[DisplayProfileConfig::MAX_BITMAP_WIDTH * 2]" in SOURCE

RGB565_HEADER = (ROOT / "src" / "watchface" / "elements" / "image_rgb565.h").read_text(encoding="utf-8")
assert "decodeRgb565LittleEndianRow" in RGB565_HEADER
assert "encodedLength != pixelCount * 2U" in RGB565_HEADER

PACKAGE_VALIDATOR = (ROOT / "src" / "watchface" / "package_validator.cpp").read_text(encoding="utf-8")
assert '#include "../communication/digest.h"' in PACKAGE_VALIDATOR
assert PACKAGE_VALIDATOR.count("WfsDigest::parseSha256Hex(") == 2
assert "hexToBytes(" not in PACKAGE_VALIDATOR
assert "bool isIntegerJsonNumber(JsonVariantConst value)" in PACKAGE_VALIDATOR
for field in ("width", "height", "byteLength", "offset"):
    assert f'isIntegerJsonNumber(asset["{field}"])' in PACKAGE_VALIDATOR, (
        f"package validator must reject non-integer asset metadata: {field}"
    )
print("WFS image reader package-range and RGB565 byte-length guards passed.")
