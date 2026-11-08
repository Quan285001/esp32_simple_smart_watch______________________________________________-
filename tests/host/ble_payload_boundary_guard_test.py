from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/communication/ble_manager.cpp").read_text()
protocol = (ROOT / "src/communication/protocol.h").read_text()

assert "MAX_CONTROL_PAYLOAD = 220" in protocol
assert "MAX_DATA_FRAME_BYTES = 240" in protocol
assert "frame.payloadLength > WfsProtocol::MAX_CONTROL_PAYLOAD" in source
assert 'sendError(0x0002, "control_payload_too_large")' in source
assert "frame.payloadLength > WfsProtocol::MAX_DATA_FRAME_BYTES - WfsProtocol::FRAME_HEADER_SIZE" in source
assert 'sendError(0x0004, "data_payload_too_large")' in source

print("WFS BLE channel payload boundary guard tests passed.")
