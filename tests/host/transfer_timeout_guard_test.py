#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "src" / "communication" / "transfer_manager.h").read_text(encoding="utf-8")
TRANSFER = (ROOT / "src" / "communication" / "transfer_manager.cpp").read_text(encoding="utf-8")
BLE = (ROOT / "src" / "communication" / "ble_manager.cpp").read_text(encoding="utf-8")
SEQUENCE = (ROOT / "src" / "communication" / "transfer_sequence.h").read_text(encoding="utf-8")

assert "void service();" in HEADER
assert "    Timeout" in HEADER
assert "constexpr uint32_t TRANSFER_IDLE_TIMEOUT_MS = 120000;" in TRANSFER
assert "activityTimer.start(transferStartedAtMs);" in TRANSFER
assert "activityTimer.touch(millis());" in TRANSFER
assert "activityTimer.expired(millis(), TRANSFER_IDLE_TIMEOUT_MS)" in TRANSFER
assert "currentState != State::Receiving && currentState != State::Verified" in TRANSFER
assert "currentError = Error::Timeout;" in TRANSFER
assert 'case Error::Timeout: return "TIMEOUT";' in TRANSFER
service_start = BLE.index("void serviceWfsBle()")
queued_data = BLE.find("if (packet.channel == 1)", service_start)
timeout_service = BLE.find("TransferManager::service();", service_start)
assert queued_data >= 0 and timeout_service > queued_data, (
    "queued BLE chunks must refresh transfer activity before idle timeout is serviced"
)
assert "class InactivityTimer" in SEQUENCE
assert "static_cast<uint32_t>(now - lastActivity_) >= timeoutMs" in SEQUENCE

print("WFS abandoned-transfer timeout integration guards passed.")
