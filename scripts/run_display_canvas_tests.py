#!/usr/bin/env python3
"""Compile and run the backend-neutral DisplayCanvas contract without TFT libraries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tests/host/display_canvas_contract_test.cpp"
STUBS = ROOT / "tests/host/stubs"
DISPLAY = ROOT / "src/display"

with tempfile.TemporaryDirectory(prefix="wfs-canvas-") as temp:
    binary = Path(temp) / "display_canvas_contract_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra",
         "-I", str(STUBS), "-I", str(DISPLAY),
         str(TEST), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("[CANVAS-CONTRACT] Host compile and contract test passed.")
