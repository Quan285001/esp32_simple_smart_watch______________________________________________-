#include "ble_manager.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "../storage/storage_manager.h"
#include "../storage/face_storage.h"
#include "../watchface/face_model.h"
#include "../watchface/face_manager.h"
#include "transfer_manager.h"
#include <stdlib.h>

namespace {
constexpr size_t PACKET_CAPACITY = WfsProtocol::MAX_DATA_FRAME_BYTES;
constexpr UBaseType_t QUEUE_DEPTH = 4;

struct WfsPacket {
    uint16_t length;
    uint8_t bytes[PACKET_CAPACITY];
};

BLEService* wfsService = nullptr;
BLECharacteristic* controlTx = nullptr;
BLECharacteristic* dataTx = nullptr;
QueueHandle_t controlQueue = nullptr;
QueueHandle_t dataQueue = nullptr;
uint32_t sessionId = 0;
uint16_t txSequence = 0;
uint16_t lastControlSequence = 0;
uint32_t queueDrops = 0;

class WfsWriteCallbacks final : public BLECharacteristicCallbacks {
public:
    explicit WfsWriteCallbacks(QueueHandle_t* target) : targetQueue(target) {}

    void onWrite(BLECharacteristic* characteristic) override {
        QueueHandle_t queue = targetQueue != nullptr ? *targetQueue : nullptr;
        if (queue == nullptr || characteristic == nullptr) return;
        const std::string value = characteristic->getValue();
        if (value.empty() || value.size() > PACKET_CAPACITY) return;

        WfsPacket packet{};
        packet.length = static_cast<uint16_t>(value.size());
        memcpy(packet.bytes, value.data(), value.size());
        if (xQueueSend(queue, &packet, 0) != pdTRUE) {
            __atomic_fetch_add(&queueDrops, 1, __ATOMIC_RELAXED);
        }
    }

private:
    QueueHandle_t* targetQueue;
};

bool sendFrame(BLECharacteristic* characteristic, WfsProtocol::MessageType type,
               const uint8_t* payload, uint16_t payloadLength) {
    if (characteristic == nullptr) return false;
    uint8_t bytes[PACKET_CAPACITY] = {};
    size_t length = 0;
    if (!WfsProtocol::encodeFrame(type, txSequence++, sessionId,
                                  payload, payloadLength, bytes, sizeof(bytes), length)) {
        return false;
    }
    characteristic->setValue(bytes, length);
    characteristic->notify();
    return true;
}

void sendError(uint16_t code, const char* message) {
    char payload[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
    const int length = snprintf(payload, sizeof(payload),
                                "{\"code\":%u,\"message\":\"%s\"}",
                                code, message != nullptr ? message : "error");
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::Error,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    }
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void writeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool parseDigest(const char* hex, uint8_t output[32]) {
    if (hex == nullptr || strlen(hex) != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\\0'};
        char* end = nullptr;
        const unsigned long value = strtoul(pair, &end, 16);
        if (end == nullptr || *end != '\\0' || value > 255) return false;
        output[i] = static_cast<uint8_t>(value);
    }
    return true;
}

void sendDataAck(uint32_t transferId) {
    const TransferManager::Snapshot status = TransferManager::snapshot();
    uint8_t payload[14] = {};
    writeU32(payload, transferId);
    writeU32(payload + 4, status.nextChunkIndex == 0 ? 0 : status.nextChunkIndex - 1);
    writeU32(payload + 8, status.receivedBytes);
    payload[12] = static_cast<uint8_t>(status.error);
    payload[13] = 0;
    sendFrame(dataTx, WfsProtocol::MessageType::DataAck, payload, sizeof(payload));
}

void sendHelloAck() {
    char payload[160] = {};
    const int length = snprintf(payload, sizeof(payload),
        "{\"major\":%u,\"minor\":%u,\"device\":\"ESP32_Watch_C3\",\"session\":%lu}",
        WfsProtocol::PROTOCOL_MAJOR, WfsProtocol::PROTOCOL_MINOR,
        static_cast<unsigned long>(sessionId));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::HelloAck,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    }
}

void sendCapabilities() {
    const size_t inactiveBytes = FaceStorage::slotSize(FaceStorage::inactiveSlot());
    const size_t stageCapacity = StorageManager::freeBytes() + inactiveBytes;
    char payload[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
    const int length = snprintf(payload, sizeof(payload),
        "{\"schema\":1,\"package\":1,\"profile\":\"esp32-c3-gc9a01-240\","
        "\"maxObjects\":%u,\"maxLayoutBytes\":16384,\"maxFrameBytes\":%u,"
        "\"mtu\":%u,\"elements\":[\"text\",\"digital-clock\",\"analog-clock\",\"date\",\"weather\",\"shape\"],"
        "\"assetEncodings\":[],\"packageInstall\":false,\"storageFree\":%lu,\"stageCapacity\":%lu,\"heapFree\":%lu}",
        FACE_MAX_OBJECTS, static_cast<unsigned int>(WfsProtocol::MAX_DATA_FRAME_BYTES),
        static_cast<unsigned int>(BLEDevice::getMTU()),
        static_cast<unsigned long>(StorageManager::freeBytes()),
        static_cast<unsigned long>(stageCapacity),
        static_cast<unsigned long>(ESP.getFreeHeap()));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::Capabilities,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    } else {
        sendError(0x0002, "capabilities_payload_too_large");
    }
}

void handleControlPacket(const WfsPacket& packet) {
    WfsProtocol::FrameView frame{};
    const WfsProtocol::FrameError decodeError =
        WfsProtocol::decodeFrame(packet.bytes, packet.length, sessionId, frame);
    if (decodeError != WfsProtocol::FrameError::None) {
        sendError(0x0002, WfsProtocol::frameErrorName(decodeError));
        return;
    }

    if (frame.type == WfsProtocol::MessageType::Hello) {
        JsonDocument hello;
        const DeserializationError jsonError =
            deserializeJson(hello, frame.payload, frame.payloadLength,
                            DeserializationOption::NestingLimit(4));
        if (jsonError) {
            sendError(0x0002, "hello_json_invalid");
            return;
        }
        const uint8_t minMajor = hello["minMajor"] | WfsProtocol::PROTOCOL_MAJOR;
        const uint8_t maxMajor = hello["maxMajor"] | WfsProtocol::PROTOCOL_MAJOR;
        if (minMajor > WfsProtocol::PROTOCOL_MAJOR || maxMajor < WfsProtocol::PROTOCOL_MAJOR) {
            sendError(0x0001, "protocol_version_unsupported");
            return;
        }
        sessionId = esp_random();
        if (sessionId == 0) sessionId = 1;
        txSequence = 0;
        lastControlSequence = frame.sequence;
        sendHelloAck();
        return;
    }

    if (sessionId == 0 || frame.sessionId != sessionId) {
        sendError(0x0003, "session_required");
        return;
    }
    if (frame.sequence != static_cast<uint16_t>(lastControlSequence + 1U)) {
        sendError(0x0004, "sequence_invalid");
        return;
    }
    lastControlSequence = frame.sequence;

    switch (frame.type) {
        case WfsProtocol::MessageType::GetCapabilities:
            sendCapabilities();
            break;
        case WfsProtocol::MessageType::Ping:
            sendFrame(controlTx, WfsProtocol::MessageType::Pong, nullptr, 0);
            break;
        case WfsProtocol::MessageType::TransferBegin: {
            JsonDocument request;
            if (deserializeJson(request, frame.payload, frame.payloadLength,
                                DeserializationOption::NestingLimit(4))) {
                sendError(0x0022, "transfer_begin_json_invalid");
                break;
            }
            const uint32_t transferId = request["transferId"] | 0;
            const uint32_t packageLength = request["packageLength"] | 0;
            const char* designId = request["designId"] | "";
            const char* digestText = request["payloadSha256"] | "";
            uint8_t digest[32] = {};
            if (!parseDigest(digestText, digest) ||
                !TransferManager::begin(transferId, packageLength, digest, designId)) {
                sendError(0x0023, TransferManager::errorName(TransferManager::snapshot().error));
                break;
            }
            char response[160] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\\"transferId\\":%lu,\\"chunkSize\\":216,\\"nextChunk\\":0}",
                static_cast<unsigned long>(transferId));
            if (responseLength > 0) sendFrame(controlTx, WfsProtocol::MessageType::TransferReady,
                reinterpret_cast<const uint8_t*>(response), static_cast<uint16_t>(responseLength));
            break;
        }
        case WfsProtocol::MessageType::TransferEnd: {
            JsonDocument request;
            if (deserializeJson(request, frame.payload, frame.payloadLength,
                                DeserializationOption::NestingLimit(4))) {
                sendError(0x0022, "transfer_end_json_invalid");
                break;
            }
            const uint32_t transferId = request["transferId"] | 0;
            const bool verified = TransferManager::finish(transferId);
            char response[160] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\\"transferId\\":%lu,\\"ok\\":%s,\\"error\\":\\"%s\\"}",
                static_cast<unsigned long>(transferId), verified ? "true" : "false",
                TransferManager::errorName(TransferManager::snapshot().error));
            if (responseLength > 0) sendFrame(controlTx, WfsProtocol::MessageType::VerifyResult,
                reinterpret_cast<const uint8_t*>(response), static_cast<uint16_t>(responseLength));
            break;
        }
        case WfsProtocol::MessageType::Apply: {
            JsonDocument request;
            if (deserializeJson(request, frame.payload, frame.payloadLength,
                                DeserializationOption::NestingLimit(4))) {
                sendError(0x0022, "apply_json_invalid");
                break;
            }
            const uint32_t transferId = request["transferId"] | 0;
            const bool applied = TransferManager::apply(transferId);
            const FaceModel* active = FaceManager::getActiveFace();
            char response[180] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\\"transferId\\":%lu,\\"ok\\":%s,\\"designId\\":\\"%s\\"}",
                static_cast<unsigned long>(transferId), applied ? "true" : "false",
                applied && active != nullptr ? active->designId : "");
            if (responseLength > 0) sendFrame(controlTx, WfsProtocol::MessageType::ApplyResult,
                reinterpret_cast<const uint8_t*>(response), static_cast<uint16_t>(responseLength));
            break;
        }
        case WfsProtocol::MessageType::Cancel: {
            JsonDocument request;
            if (deserializeJson(request, frame.payload, frame.payloadLength,
                                DeserializationOption::NestingLimit(4))) {
                sendError(0x0022, "cancel_json_invalid");
                break;
            }
            const uint32_t transferId = request["transferId"] | 0;
            TransferManager::cancel(transferId);
            sendError(0x0000, "transfer_cancelled");
            break;
        }
        default:
            sendError(0x0010, "command_not_implemented");
            break;
    }
}

void handleDataPacket(const WfsPacket& packet) {
    WfsProtocol::FrameView frame{};
    const WfsProtocol::FrameError error =
        WfsProtocol::decodeFrame(packet.bytes, packet.length, sessionId, frame);
    if (error != WfsProtocol::FrameError::None) {
        sendError(0x0002, WfsProtocol::frameErrorName(error));
        return;
    }
    if (sessionId == 0 || frame.sessionId != sessionId ||
        frame.sequence != static_cast<uint16_t>(lastControlSequence + 1U)) {
        sendError(0x0004, "data_sequence_or_session_invalid");
        return;
    }
    lastControlSequence = frame.sequence;
    if (frame.type != WfsProtocol::MessageType::Data || frame.payloadLength <= 8) {
        sendError(0x0022, "data_frame_invalid");
        return;
    }
    const uint32_t transferId = readU32(frame.payload);
    const uint32_t chunkIndex = readU32(frame.payload + 4);
    const uint8_t* chunkBytes = frame.payload + 8;
    const size_t chunkLength = frame.payloadLength - 8;
    if (!TransferManager::writeChunk(transferId, chunkIndex, chunkBytes, chunkLength)) {
        sendError(0x0025, TransferManager::errorName(TransferManager::snapshot().error));
        return;
    }
    sendDataAck(transferId);
}
}

bool beginWfsGatt(BLEServer* server) {
    if (server == nullptr) return false;
    if (controlQueue == nullptr) controlQueue = xQueueCreate(QUEUE_DEPTH, sizeof(WfsPacket));
    if (dataQueue == nullptr) dataQueue = xQueueCreate(QUEUE_DEPTH, sizeof(WfsPacket));
    if (controlQueue == nullptr || dataQueue == nullptr) {
        Serial.println("[WFS] Queue allocation failed.");
        return false;
    }

    wfsService = server->createService(WfsProtocol::SERVICE_UUID);
    if (wfsService == nullptr) return false;

    BLECharacteristic* controlRx = wfsService->createCharacteristic(
        WfsProtocol::CONTROL_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    controlTx = wfsService->createCharacteristic(
        WfsProtocol::CONTROL_TX_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* dataRx = wfsService->createCharacteristic(
        WfsProtocol::DATA_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    dataTx = wfsService->createCharacteristic(
        WfsProtocol::DATA_TX_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    if (controlRx == nullptr || controlTx == nullptr || dataRx == nullptr || dataTx == nullptr) {
        Serial.println("[WFS] Characteristic creation failed.");
        return false;
    }

    controlTx->addDescriptor(new BLE2902());
    dataTx->addDescriptor(new BLE2902());
    controlRx->setCallbacks(new WfsWriteCallbacks(&controlQueue));
    dataRx->setCallbacks(new WfsWriteCallbacks(&dataQueue));
    controlTx->setValue("WFS1_READY");
    dataTx->setValue("WFS1_DATA_READY");
    wfsService->start();

    Serial.println("[WFS] Protocol v1 GATT service started (transfer disabled until installer is ready).");
    return true;
}

void serviceWfsBle() {
    const uint32_t dropped = __atomic_exchange_n(&queueDrops, 0, __ATOMIC_RELAXED);
    if (dropped) Serial.printf("[WFS] RX queue dropped %lu frame(s).\n", static_cast<unsigned long>(dropped));

    if (controlQueue != nullptr) {
        WfsPacket packet{};
        if (xQueueReceive(controlQueue, &packet, 0) == pdTRUE) handleControlPacket(packet);
    }
    if (dataQueue != nullptr) {
        WfsPacket packet{};
        if (xQueueReceive(dataQueue, &packet, 0) == pdTRUE) handleDataPacket(packet);
    }
}

void resetWfsSession() {
    sessionId = 0;
    txSequence = 0;
    lastControlSequence = 0;
    if (controlQueue != nullptr) xQueueReset(controlQueue);
    if (dataQueue != nullptr) xQueueReset(dataQueue);
}

void shutdownWfsGatt() {
    resetWfsSession();
    wfsService = nullptr;
    controlTx = nullptr;
    dataTx = nullptr;
    if (controlQueue != nullptr) {
        vQueueDelete(controlQueue);
        controlQueue = nullptr;
    }
    if (dataQueue != nullptr) {
        vQueueDelete(dataQueue);
        dataQueue = nullptr;
    }
}
