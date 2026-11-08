#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src" / "watchface" / "elements" / "image.cpp").read_text(encoding="utf-8")
assert "asset->byteLength != static_cast<size_t>(asset->width) * asset->height * sizeof(uint16_t)" in SOURCE
assert "const size_t fileSize = file.size();" in SOURCE
assert "asset->byteLength > fileSize - asset->packageOffset" in SOURCE
assert "file.seek(asset->packageOffset, fs::SeekSet)" in SOURCE
print("WFS image reader package-range and RGB565 byte-length guards passed.")
