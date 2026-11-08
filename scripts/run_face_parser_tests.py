#!/usr/bin/env python3
"""Compile and run the Watch Face JSON parser against ArduinoJson's host headers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARDUINOJSON = ROOT / ".pio/libdeps/c3_clock/ArduinoJson/src"
STUBS = ROOT / "tests/host/stubs"
TEST = ROOT / "tests/host/face_parser_test.cpp"
PARSER = ROOT / "src/watchface/face_parser.cpp"
FIXTURE = ROOT / "shared/schemas/examples/minimal-watch-face-v1.json"

if not ARDUINOJSON.is_dir():
    raise SystemExit("ArduinoJson headers are unavailable; run the PlatformIO build first.")

with tempfile.TemporaryDirectory(prefix="wfs-face-parser-") as tmp:
    binary = Path(tmp) / "face_parser_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra",
         "-I", str(STUBS), "-I", str(ARDUINOJSON),
         str(TEST), str(PARSER), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary), str(FIXTURE)], cwd=ROOT, check=True)
