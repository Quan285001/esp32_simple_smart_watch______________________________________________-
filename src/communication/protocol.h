#pragma once

#include <stddef.h>
#include <stdint.h>

namespace WfsProtocol {

constexpr uint8_t PROTOCOL_MAJOR = 1;
constexpr uint8_t PROTOCOL_MINOR = 0;
constexpr size_t FRAME_HEADER_SIZE = 16;
constexpr size_t MAX_CONTROL_PAYLOAD = 220;
constexpr size_t MAX_DATA_FRAME_BYTES = 240;

constexpr char SERVICE_UUID[] = "f2c6b100-2f3a-4c1b-9d70-8e4a5c6b7100";
constexpr char CONTROL_RX_UUID[] = "f2c6b101-2f3a-4c1b-9d70-8e4a5c6b7100";
constexpr char CONTROL_TX_UUID[] = "f2c6b102-2f3a-4c1b-9d70-8e4a5c6b7100";
constexpr char DATA_RX_UUID[] = "f2c6b103-2f3a-4c1b-9d70-8e4a5c6b7100";
constexpr char DATA_TX_UUID[] = "f2c6b104-2f3a-4c1b-9d70-8e4a5c6b7100";

enum class MessageType : uint8_t {
    Hello = 0x01,
    HelloAck = 0x02,
    GetCapabilities = 0x03,
    Capabilities = 0x04,
    Command = 0x10,
    Response = 0x11,
    Event = 0x12,
    TransferBegin = 0x20,
    TransferReady = 0x21,
    Data = 0x22,
    DataAck = 0x23,
    TransferEnd = 0x24,
    VerifyResult = 0x25,
    Apply = 0x26,
    ApplyResult = 0x27,
    Cancel = 0x28,
    TransferProgress = 0x29,
    Ping = 0x30,
    Pong = 0x31,
    Error = 0x7F
};

enum class FrameError : uint8_t {
    None,
    TooShort,
    InvalidMagic,
    UnsupportedVersion,
    UnknownMessage,
    InvalidFlags,
    InvalidLength,
    InvalidHeaderCrc,
    InvalidSession
};

struct FrameView {
    MessageType type;
    uint16_t sequence;
    uint32_t sessionId;
    const uint8_t* payload;
    uint16_t payloadLength;
};

uint16_t crc16CcittFalse(const uint8_t* data, size_t length);
FrameError decodeFrame(const uint8_t* bytes, size_t length,
                       uint32_t expectedSessionId, FrameView& output);
bool encodeFrame(MessageType type, uint16_t sequence, uint32_t sessionId,
                 const uint8_t* payload, uint16_t payloadLength,
                 uint8_t* output, size_t outputCapacity, size_t& outputLength);
const char* frameErrorName(FrameError error);

} // namespace WfsProtocol
