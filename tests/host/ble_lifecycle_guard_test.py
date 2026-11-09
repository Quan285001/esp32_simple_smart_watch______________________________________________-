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

# WFS session reset is deferred out of BLE host callbacks. The callback only
# advances the generation and marks a reset; the application loop owns aborts.
manager = (ROOT / "src/communication/ble_manager.cpp").read_text()
service_start = source.index("void serviceBLE()")
service_end = source.index("void shutdownBLEForSleep()", service_start)
service_loop = source[service_start:service_end]
assert "serviceWfsBle();" in service_loop

request_start = manager.index("void requestWfsSessionReset()")
request_end = manager.index("void resetWfsSession()", request_start)
request_reset = manager[request_start:request_end]
assert "__atomic_add_fetch(&rxGeneration, 1" in request_reset
assert "__atomic_store_n(&sessionResetPending, true" in request_reset
assert "TransferManager::" not in request_reset
assert "xQueueReset" not in request_reset

apply_start = manager.index("void applyPendingSessionReset()")
apply_end = manager.index("void serviceWfsBle()", apply_start)
apply_reset = manager[apply_start:apply_end]
assert "__atomic_exchange_n(&sessionResetPending, false" in apply_reset
assert "TransferManager::abortSession()" in apply_reset

# A packet queued before a connect/disconnect generation change must not be
# processed as part of the new session.
callback_start = manager.index("void onWrite(BLECharacteristic* characteristic) override")
callback_end = manager.index("private:", callback_start)
wfs_write = manager[callback_start:callback_end]
assert "packet.generation = __atomic_load_n(&rxGeneration" in wfs_write
assert "xQueueSend(queue, &packet, 0)" in wfs_write
assert "TransferManager::" not in wfs_write

# BLE host/controller teardown must finish before callback-owned WFS resources
# and the legacy command queue are destroyed.
sleep_start = source.index("void shutdownBLEForSleep()")
sleep_end = source.index("bool isBLEEnabled()", sleep_start)
sleep = source[sleep_start:sleep_end]
assert sleep.index("BLEDevice::deinit(false)") < sleep.index("shutdownWfsGatt()")
assert sleep.index("shutdownWfsGatt()") < sleep.index("vQueueDelete(commandQueue)")
assert "commandQueue = nullptr" in sleep

shutdown_start = manager.index("void shutdownWfsGatt()")
shutdown = manager[shutdown_start:]
assert shutdown.index("resetWfsSession()") < shutdown.index("vQueueDelete(rxQueue)")
assert "rxQueue = nullptr" in shutdown

# Disconnect callbacks request a deferred advertising restart rather than
# invoking BLE advertising APIs from the host callback context.
disconnect_start = source.index("void onDisconnect(BLEServer *) override")
disconnect_end = source.index("\n    }", disconnect_start)
disconnect = source[disconnect_start:disconnect_end]
assert "requestWfsSessionReset()" in disconnect
assert "advertisingRestartPending" in disconnect
assert "->start()" not in disconnect
assert "->stop()" not in disconnect

print("BLE session generation and teardown ordering guard tests passed.")

# The smartwatch BLE transport is intentionally open: no pairing/bonding or
# encrypted GATT permissions are required for Windows/Web Bluetooth clients.
assert "BLESecurity" not in source
assert "setEncryptionLevel" not in source
assert "ESP_LE_AUTH_BOND" not in source
assert "ESP_GATT_PERM_READ_ENCRYPTED" not in source
assert "ESP_GATT_PERM_WRITE_ENCRYPTED" not in source
assert "ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE" in source
assert "ESP_GATT_PERM_READ_ENCRYPTED" not in manager
assert "ESP_GATT_PERM_WRITE_ENCRYPTED" not in manager
assert "ESP_GATT_PERM_READ" in manager
assert "ESP_GATT_PERM_WRITE" in manager

print("BLE open-access policy guard tests passed.")
