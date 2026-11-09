from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ble = (ROOT / "src/communication/ble_manager.cpp").read_text()
legacy = (ROOT / "src/services/ble_service.cpp").read_text()
transfer = (ROOT / "src/communication/transfer_manager.cpp").read_text()
transfer_header = (ROOT / "src/communication/transfer_manager.h").read_text()

# GATT callbacks must remain bounded: copy into a queue, never parse a frame,
# touch SPIFFS, or mutate transfer state from the Bluetooth host task.
callback_start = ble.index("void onWrite(BLECharacteristic* characteristic) override")
callback_end = ble.index("\n    }\n\nprivate:", callback_start)
callback = ble[callback_start:callback_end]
assert "xQueueSend(queue, &packet, 0)" in callback
assert "packet.generation" in callback
for forbidden in ("handleControlPacket(", "handleDataPacket(",
                  "TransferManager::", "FaceStorage::", "SPIFFS."):
    assert forbidden not in callback, forbidden

# A failed optional WFS GATT setup must release its RX queue instead of
# retaining nearly 2 KiB of queue storage while legacy BLE continues.
setup_start = ble.index("void rollbackWfsGattSetup()")
setup_end = ble.index(chr(10) + "}", setup_start)
setup_rollback = ble[setup_start:setup_end]
assert "vQueueDelete(rxQueue)" in setup_rollback
assert "rxQueue = nullptr" in setup_rollback
begin_start = ble.index("bool beginWfsGatt(")
begin_end = ble.index("void applyPendingSessionReset()", begin_start)
begin_wfs = ble[begin_start:begin_end]
assert begin_wfs.count("rollbackWfsGattSetup();") == 2

# HELLO is the explicit session-replacement handshake. Decode it without
# the previous session constraint, require session zero, and abort old transfer
# state before publishing the replacement session ID.
control_start = ble.index("void handleControlPacket(")
control_end = ble.index("\nvoid handleDataPacket(", control_start)
control = ble[control_start:control_end]
assert "decodeFrame(packet.bytes, packet.length, 0, frame)" in control
hello_start = control.index("if (frame.type == WfsProtocol::MessageType::Hello)")
hello_end = control.index("if (sessionId == 0 || frame.sessionId != sessionId)", hello_start)
hello = control[hello_start:hello_end]
assert "frame.sessionId != 0" in hello
assert "TransferManager::abortSession();" in hello
assert hello.index("TransferManager::abortSession();") < hello.index("sessionId = esp_random();")
assert "transfer_begin_fields_invalid" in control
assert "payload_sha256_invalid" in control
assert "transferIdValue.is<uint32_t>()" in control
assert "packageLengthValue.is<uint32_t>()" in control
for action in ("TransferEnd", "Apply", "Cancel"):
    action_start = control.index(f"case WfsProtocol::MessageType::{action}:")
    action_end = control.find("case WfsProtocol::MessageType::", action_start + 10)
    if action_end < 0:
        action_end = len(control)
    assert "transferIdValue.is<uint32_t>()" in control[action_start:action_end], action

# Connect/disconnect callbacks invalidate queued packets by advancing an epoch.
server_start = legacy.index("class ServerCallbacks")
server_end = legacy.index("class CharacteristicCallbacks", server_start)
server_callbacks = legacy[server_start:server_end]
assert server_callbacks.count("requestWfsSessionReset();") == 2

reset_start = ble.index("void requestWfsSessionReset()")
reset_end = ble.index("\nvoid resetWfsSession()", reset_start)
request_reset = ble[reset_start:reset_end]
assert "__atomic_add_fetch(&rxGeneration, 1" in request_reset
assert "__atomic_store_n(&sessionResetPending, true" in request_reset

service_start = ble.index("void serviceWfsBle()")
service_end = ble.index("\nvoid requestWfsSessionReset()", service_start)
service = ble[service_start:service_end]
assert "applyPendingSessionReset();" in service
assert "packet.generation != __atomic_load_n(&rxGeneration" in service
assert service.index("applyPendingSessionReset();") < service.index("xQueueReceive(")
assert service.index("xQueueReceive(") < service.rindex("applyPendingSessionReset();")

reset_start = ble.index("void resetWfsSession()")
reset_end = ble.index("\nvoid shutdownWfsGatt()", reset_start)
reset = ble[reset_start:reset_end]
assert "__atomic_add_fetch(&rxGeneration, 1" in reset
assert "xQueueReset(rxQueue)" in reset
assert "TransferManager::abortSession()" in reset

# BLE host/controller teardown must precede queue destruction, so no callback
# can retain a queue handle after it has been deleted.
sleep_start = legacy.index("void shutdownBLEForSleep()")
sleep_end = legacy.index("\nbool isBLEEnabled()", sleep_start)
sleep = legacy[sleep_start:sleep_end]
assert sleep.index("BLEDevice::deinit(false)") < sleep.index("shutdownWfsGatt()")
assert sleep.index("shutdownWfsGatt()") < sleep.index("vQueueDelete(commandQueue)")
assert "commandQueue = nullptr" in sleep

cancel_start = transfer.index("bool cancel(uint32_t transferId)")
cancel_end = transfer.index("\nvoid abortSession()", cancel_start)
cancel = transfer[cancel_start:cancel_end]
assert "return false;" in cancel
assert "return true;" in cancel
assert "!FaceStorage::removeSlot(stagingSlot)" in cancel
assert "bool cancel(uint32_t transferId);" in transfer_header
assert "if (!TransferManager::cancel(transferId))" in ble
assert "transfer_not_cancellable" in ble

print("WFS BLE session-generation, teardown and cancel-result guard tests passed.")
