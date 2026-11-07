#include "../../src/communication/protocol.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(WfsProtocol::crc16CcittFalse(check, sizeof(check)) == 0x29B1);

    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t frame[64] = {};
    size_t frameLength = 0;
    assert(WfsProtocol::encodeFrame(WfsProtocol::MessageType::Ping, 7, 0x12345678,
                                    payload, sizeof(payload), frame, sizeof(frame), frameLength));
    assert(frameLength == WfsProtocol::FRAME_HEADER_SIZE + sizeof(payload));

    WfsProtocol::FrameView decoded{};
    assert(WfsProtocol::decodeFrame(frame, frameLength, 0x12345678, decoded) ==
           WfsProtocol::FrameError::None);
    assert(decoded.type == WfsProtocol::MessageType::Ping);
    assert(decoded.sequence == 7);
    assert(decoded.sessionId == 0x12345678);
    assert(decoded.payloadLength == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    assert(WfsProtocol::decodeFrame(frame, frameLength, 0x87654321, decoded) ==
           WfsProtocol::FrameError::InvalidSession);

    uint8_t corrupted[64] = {};
    memcpy(corrupted, frame, frameLength);
    corrupted[6] ^= 0x01;
    assert(WfsProtocol::decodeFrame(corrupted, frameLength, 0, decoded) ==
           WfsProtocol::FrameError::InvalidHeaderCrc);

    assert(WfsProtocol::decodeFrame(frame, frameLength - 1, 0, decoded) ==
           WfsProtocol::FrameError::InvalidLength);

    assert(!WfsProtocol::encodeFrame(WfsProtocol::MessageType::Ping, 0, 0,
                                     payload, sizeof(payload), frame, 8, frameLength));
    return 0;
}
