#include "protocol.h"

#include <string.h>

namespace WfsProtocol {
namespace {
constexpr uint8_t MAGIC_0 = 0x57;
constexpr uint8_t MAGIC_1 = 0x46;

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool isKnownMessage(uint8_t value) {
    switch (static_cast<MessageType>(value)) {
        case MessageType::Hello: case MessageType::HelloAck:
        case MessageType::GetCapabilities: case MessageType::Capabilities:
        case MessageType::GetStatus: case MessageType::Status:
        case MessageType::Command: case MessageType::Response:
        case MessageType::Event: case MessageType::TransferBegin:
        case MessageType::TransferReady: case MessageType::Data:
        case MessageType::DataAck: case MessageType::TransferEnd:
        case MessageType::VerifyResult: case MessageType::Apply:
        case MessageType::ApplyResult: case MessageType::Cancel:
        case MessageType::TransferProgress: case MessageType::Ping:
        case MessageType::Pong: case MessageType::Error:
            return true;
        default:
            return false;
    }
}
}

uint16_t crc16CcittFalse(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    if (data == nullptr) return crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

FrameError decodeFrame(const uint8_t* bytes, size_t length,
                       uint32_t expectedSessionId, FrameView& output) {
    output = {};
    if (bytes == nullptr || length < FRAME_HEADER_SIZE) return FrameError::TooShort;
    if (bytes[0] != MAGIC_0 || bytes[1] != MAGIC_1) return FrameError::InvalidMagic;
    if (bytes[2] != PROTOCOL_MAJOR || bytes[3] > PROTOCOL_MINOR) {
        return FrameError::UnsupportedVersion;
    }
    if (!isKnownMessage(bytes[4])) return FrameError::UnknownMessage;
    if (bytes[5] != 0) return FrameError::InvalidFlags;

    const uint16_t payloadLength = readU16(bytes + 10);
    if (length != FRAME_HEADER_SIZE + payloadLength) return FrameError::InvalidLength;

    const uint16_t expectedCrc = readU16(bytes + 14);
    if (crc16CcittFalse(bytes, 14) != expectedCrc) return FrameError::InvalidHeaderCrc;

    const uint32_t sessionId = readU32(bytes + 8);
    if (expectedSessionId != 0 && sessionId != expectedSessionId) {
        return FrameError::InvalidSession;
    }

    output.type = static_cast<MessageType>(bytes[4]);
    output.sequence = readU16(bytes + 6);
    output.sessionId = sessionId;
    output.payload = bytes + FRAME_HEADER_SIZE;
    output.payloadLength = payloadLength;
    return FrameError::None;
}

bool encodeFrame(MessageType type, uint16_t sequence, uint32_t sessionId,
                 const uint8_t* payload, uint16_t payloadLength,
                 uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    outputLength = 0;
    if (output == nullptr || (payloadLength > 0 && payload == nullptr)) return false;
    if (!isKnownMessage(static_cast<uint8_t>(type))) return false;
    const size_t required = FRAME_HEADER_SIZE + payloadLength;
    if (required > outputCapacity) return false;

    output[0] = MAGIC_0;
    output[1] = MAGIC_1;
    output[2] = PROTOCOL_MAJOR;
    output[3] = PROTOCOL_MINOR;
    output[4] = static_cast<uint8_t>(type);
    output[5] = 0;
    writeU16(output + 6, sequence);
    writeU32(output + 8, sessionId);
    writeU16(output + 10, payloadLength);
    writeU16(output + 14, crc16CcittFalse(output, 14));
    if (payloadLength) memcpy(output + FRAME_HEADER_SIZE, payload, payloadLength);
    outputLength = required;
    return true;
}

const char* frameErrorName(FrameError error) {
    switch (error) {
        case FrameError::None: return "NONE";
        case FrameError::TooShort: return "TOO_SHORT";
        case FrameError::InvalidMagic: return "INVALID_MAGIC";
        case FrameError::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case FrameError::UnknownMessage: return "UNKNOWN_MESSAGE";
        case FrameError::InvalidFlags: return "INVALID_FLAGS";
        case FrameError::InvalidLength: return "INVALID_LENGTH";
        case FrameError::InvalidHeaderCrc: return "INVALID_HEADER_CRC";
        case FrameError::InvalidSession: return "INVALID_SESSION";
        default: return "UNKNOWN";
    }
}

} // namespace WfsProtocol
