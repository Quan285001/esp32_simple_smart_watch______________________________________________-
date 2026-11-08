#!/usr/bin/env python3
"""Compile and run the Watch Face renderer against a deterministic host canvas."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / "tests" / "host" / "stubs"
TEST = ROOT / "tests" / "host" / "face_renderer_test.cpp"
RENDERER = ROOT / "src" / "watchface" / "face_renderer.cpp"

with tempfile.TemporaryDirectory(prefix="wfs-face-renderer-") as tmp:
    binary = Path(tmp) / "face_renderer_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-I", str(STUBS), "-I", str(ROOT / "src"),
         "-I", str(ROOT / "src" / "watchface"),
         "-I", str(ROOT / "src" / "display"),
         str(TEST), str(RENDERER), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("WFS Face renderer host tests passed.")
