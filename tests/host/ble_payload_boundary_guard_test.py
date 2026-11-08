from pathlib import Path
import ast
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/communication/ble_manager.cpp").read_text()
protocol = (ROOT / "src/communication/protocol.h").read_text()

assert "MAX_CONTROL_PAYLOAD = 220" in protocol
assert "MAX_DATA_FRAME_BYTES = 240" in protocol
assert "frame.payloadLength > WfsProtocol::MAX_CONTROL_PAYLOAD" in source
assert 'sendError(0x0002, "control_payload_too_large")' in source
assert "frame.payloadLength > WfsProtocol::MAX_DATA_FRAME_BYTES - WfsProtocol::FRAME_HEADER_SIZE" in source
assert 'sendError(0x0004, "data_payload_too_large")' in source

# Keep the compact capability advertisement inside the strict Control payload
# ceiling. Substitute worst-case representative profile/storage values into
# the actual adjacent C string literals used by snprintf.
capability_start = source.index("void sendCapabilities()")
capability_end = source.index("\nvoid handleControlPacket", capability_start)
capability_source = source[capability_start:capability_end]
assert '\\"el\\":\\"t,dc,ac,d,w,s,i\\"' in capability_source
assert '\\"wc\\":1' in capability_source
format_literals = re.findall(r'"(?:\\.|[^"\\])*"', capability_source)
assert len(format_literals) >= 2
format_string = ''.join(ast.literal_eval(value) for value in format_literals[:4])
format_string = format_string.replace("%lu", "%u")
capability_payload = format_string % (
    "esp32-c3-gc9a01-240", 240, 240, "circle", 240, 240,
    16, 16, 1, 216, 1300000
)
assert len(capability_payload.encode("utf-8")) <= 220, (
    f"CAPABILITIES payload is {len(capability_payload.encode('utf-8'))} bytes; "
    "Control payload limit is 220"
)

print("WFS BLE channel payload boundary and capability-size guard tests passed.")
