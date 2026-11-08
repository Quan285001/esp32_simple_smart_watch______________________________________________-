from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/services/ble_service.cpp").read_text()

start = source.index("void beginBLE()")
end = source.index("void toggleBLE()", start)
begin = source[start:end]
initialized = begin.index("bleStackInitialized = true;")
for failure_guard in (
    "if (bleServer == nullptr)",
    "if (service == nullptr)",
    "if (commandCharacteristic == nullptr)",
    "if (bleAdvertising == nullptr)",
):
    assert begin.index(failure_guard) < initialized, failure_guard

for reason in (
    "server_creation_failed",
    "service_creation_failed",
    "characteristic_creation_failed",
    "advertising_unavailable",
):
    assert reason in begin, reason

rollback_start = source.index("void rollbackBleInitialization")
rollback_end = source.index("} // namespace", rollback_start)
rollback = source[rollback_start:rollback_end]
assert "BLEDevice::deinit(false)" in rollback
assert "shutdownWfsGatt()" in rollback
assert "bleStackInitialized = false" in rollback
assert "bleAdvertising = nullptr" in rollback

print("BLE partial-initialization rollback guard tests passed.")
