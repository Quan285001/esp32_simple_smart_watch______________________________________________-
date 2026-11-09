#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
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

print("WFS C++ protocol codec host tests passed.")

subprocess.run(
    ["node", str(ROOT / "tests" / "host" / "web_protocol_codec_test.cjs")],
    cwd=ROOT,
    check=True,
)

print("WFS cross-language golden-frame host tests passed.")

TRANSFER_TEST = ROOT / "tests" / "host" / "transfer_sequence_test.cpp"

with tempfile.TemporaryDirectory(prefix="wfs-transfer-test-") as tmp:
    binary = Path(tmp) / "transfer_sequence_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         str(TRANSFER_TEST), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("WFS transfer sequencing host tests passed.")

LIFECYCLE_TEST = ROOT / "tests" / "host" / "transfer_lifecycle_policy_test.cpp"

with tempfile.TemporaryDirectory(prefix="wfs-lifecycle-test-") as tmp:
    binary = Path(tmp) / "transfer_lifecycle_policy_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         str(LIFECYCLE_TEST), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("WFS transfer lifecycle policy host tests passed.")

STORAGE_POLICY_TEST = ROOT / "tests" / "host" / "face_storage_policy_test.cpp"

with tempfile.TemporaryDirectory(prefix="wfs-storage-policy-test-") as tmp:
    binary = Path(tmp) / "face_storage_policy_test"
    subprocess.run(
        ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         str(STORAGE_POLICY_TEST), "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("WFS dual-slot storage recovery policy host tests passed.")

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "face_storage_recovery_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "wifi_radio_arbitration_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "boot_coordinator_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "app_runtime_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "ble_lifecycle_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "ble_payload_boundary_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "wfs_session_lifecycle_guard_test.py")],
    cwd=ROOT,
    check=True,
)



subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "web_project_import_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "web_image_import_guard_test.py")],
    cwd=ROOT,
    check=True,
)


subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "worker_diagnostics_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    ["node", str(ROOT / "tests" / "host" / "device_compatibility_test.cjs")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "image_pipeline_guard_test.py")],
    cwd=ROOT,
    check=True,
)

subprocess.run(
    [sys.executable, str(ROOT / "tests" / "host" / "device_compatibility_validator_test.py")],
    cwd=ROOT,
    check=True,
)

print("WFS device compatibility preflight host tests passed.")
