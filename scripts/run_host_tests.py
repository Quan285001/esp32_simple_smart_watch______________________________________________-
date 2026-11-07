#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "communication" / "protocol.cpp"
TEST = ROOT / "tests" / "host" / "protocol_codec_test.cpp"

with tempfile.TemporaryDirectory(prefix="wfs-host-test-") as tmp:
    binary = Path(tmp) / "protocol_codec_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         str(TEST), str(SOURCE), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("WFS protocol codec host tests passed.")
