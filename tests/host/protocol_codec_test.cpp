#include "../../src/communication/protocol.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(WfsProtocol::crc16CcittFalse(check, sizeof(check)) == 0x29B1);

    // Golden frame generated with the Web DataView layout:
    // sessionId at 8..11, payloadLength at 12..13, CRC at 14..15.
    const uint8_t webGolden[] = {
        0x57, 0x46, 0x01, 0x00, 0x30, 0x00, 0x07, 0x00,
        0x78, 0x56, 0x34, 0x12, 0x04, 0x00, 0xDF, 0x1F,
        0x10, 0x20, 0x30, 0x40
    };

    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t frame[64] = {};
    size_t frameLength = 0;
    assert(WfsProtocol::encodeFrame(WfsProtocol::MessageType::Ping, 7, 0x12345678,
                                    payload, sizeof(payload), frame, sizeof(frame), frameLength));
    assert(frameLength == sizeof(webGolden));
    assert(memcmp(frame, webGolden, sizeof(webGolden)) == 0);

    // Decode the Web-produced golden frame independently of our encoder.
    WfsProtocol::FrameView decoded{};
    assert(WfsProtocol::decodeFrame(webGolden, sizeof(webGolden), 0x12345678, decoded) ==
           WfsProtocol::FrameError::None);
    assert(decoded.type == WfsProtocol::MessageType::Ping);
    assert(decoded.sequence == 7);
    assert(decoded.sessionId == 0x12345678);
    assert(decoded.payloadLength == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    assert(WfsProtocol::decodeFrame(webGolden, sizeof(webGolden), 0x87654321, decoded) ==
           WfsProtocol::FrameError::InvalidSession);

    uint8_t corrupted[sizeof(webGolden)] = {};
    memcpy(corrupted, webGolden, sizeof(webGolden));
    corrupted[6] ^= 0x01;
    assert(WfsProtocol::decodeFrame(corrupted, sizeof(corrupted), 0, decoded) ==
           WfsProtocol::FrameError::InvalidHeaderCrc);

    assert(WfsProtocol::decodeFrame(webGolden, sizeof(webGolden) - 1, 0, decoded) ==
           WfsProtocol::FrameError::InvalidLength);

    const auto refreshHeaderCrc = [](uint8_t* bytes) {
        const uint16_t crc = WfsProtocol::crc16CcittFalse(bytes, 14);
        bytes[14] = static_cast<uint8_t>(crc & 0xFF);
        bytes[15] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    };

    uint8_t malformed[sizeof(webGolden)] = {};
    memcpy(malformed, webGolden, sizeof(webGolden));
    malformed[0] = 0x00;
    assert(WfsProtocol::decodeFrame(malformed, sizeof(malformed), 0, decoded) ==
           WfsProtocol::FrameError::InvalidMagic);

    memcpy(malformed, webGolden, sizeof(webGolden));
    malformed[2] = 2;
    refreshHeaderCrc(malformed);
    assert(WfsProtocol::decodeFrame(malformed, sizeof(malformed), 0, decoded) ==
           WfsProtocol::FrameError::UnsupportedVersion);

    memcpy(malformed, webGolden, sizeof(webGolden));
    malformed[4] = 0xFE;
    refreshHeaderCrc(malformed);
    assert(WfsProtocol::decodeFrame(malformed, sizeof(malformed), 0, decoded) ==
           WfsProtocol::FrameError::UnknownMessage);

    memcpy(malformed, webGolden, sizeof(webGolden));
    malformed[5] = 1;
    refreshHeaderCrc(malformed);
    assert(WfsProtocol::decodeFrame(malformed, sizeof(malformed), 0, decoded) ==
           WfsProtocol::FrameError::InvalidFlags);

    memcpy(malformed, webGolden, sizeof(webGolden));
    malformed[12] = 5;
    refreshHeaderCrc(malformed);
    assert(WfsProtocol::decodeFrame(malformed, sizeof(malformed), 0, decoded) ==
           WfsProtocol::FrameError::InvalidLength);

    assert(WfsProtocol::decodeFrame(nullptr, 0, 0, decoded) ==
           WfsProtocol::FrameError::TooShort);

    assert(!WfsProtocol::encodeFrame(WfsProtocol::MessageType::Ping, 0, 0,
                                     payload, sizeof(payload), frame, 8, frameLength));
    assert(!WfsProtocol::encodeFrame(static_cast<WfsProtocol::MessageType>(0xFE),
                                     0, 0, payload, sizeof(payload),
                                     frame, sizeof(frame), frameLength));
    assert(!WfsProtocol::encodeFrame(WfsProtocol::MessageType::Ping, 0, 0,
                                     nullptr, sizeof(payload), frame,
                                     sizeof(frame), frameLength));
    return 0;
}
