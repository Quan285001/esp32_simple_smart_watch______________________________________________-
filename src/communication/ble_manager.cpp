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
#include "digest.h"
#include "../storage/storage_manager.h"
#include "../storage/face_storage.h"
#include "../watchface/face_model.h"
#include "../display/display_profile.h"
#include "../app/app_state.h"
#include "../system/system_manager.h"
#include "../services/weather_service.h"
#include "../services/ble_service.h"
#include <WiFi.h>
#include "../watchface/face_manager.h"
#include "transfer_manager.h"

extern bool canStartWfsTransfer();
#include "command_handler.h"
#include <stdlib.h>

extern uint8_t getAutoSleepTimeoutSeconds();

namespace {
constexpr size_t PACKET_CAPACITY = WfsProtocol::MAX_DATA_FRAME_BYTES;
constexpr UBaseType_t QUEUE_DEPTH = 8;
constexpr uint8_t MAX_PACKETS_PER_LOOP = 2;

struct WfsPacket {
    uint16_t length;
    uint8_t channel;
    uint32_t generation;
    uint8_t bytes[PACKET_CAPACITY];
};

BLEService* wfsService = nullptr;
BLECharacteristic* controlTx = nullptr;
BLECharacteristic* dataTx = nullptr;
QueueHandle_t rxQueue = nullptr;
uint32_t sessionId = 0;
uint16_t txSequence = 0;
uint16_t lastControlSequence = 0;
uint32_t queueDrops = 0;
volatile bool sessionResetPending = false;
volatile uint32_t rxGeneration = 0;

class WfsWriteCallbacks final : public BLECharacteristicCallbacks {
public:
    explicit WfsWriteCallbacks(QueueHandle_t* target, uint8_t channel)
        : targetQueue(target), channel_(channel) {}

    void onWrite(BLECharacteristic* characteristic) override {
        QueueHandle_t queue = targetQueue != nullptr ? *targetQueue : nullptr;
        if (queue == nullptr || characteristic == nullptr) return;
        const std::string value = characteristic->getValue();
        if (value.empty() || value.size() > PACKET_CAPACITY) return;

        WfsPacket packet{};
        packet.length = static_cast<uint16_t>(value.size());
        packet.channel = channel_;
        packet.generation = __atomic_load_n(&rxGeneration, __ATOMIC_ACQUIRE);
        memcpy(packet.bytes, value.data(), value.size());
        if (xQueueSend(queue, &packet, 0) != pdTRUE) {
            __atomic_fetch_add(&queueDrops, 1, __ATOMIC_RELAXED);
        }
    }

private:
    QueueHandle_t* targetQueue;
    uint8_t channel_;
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

void sendDeviceStatus() {
    const WeatherData weather = getWeatherSnapshot();
    char payload[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
    const int length = snprintf(payload, sizeof(payload),
        "{\"wifiEnabled\":%s,\"wifiConnected\":%s,\"bleConnected\":%s,"
        "\"weatherValid\":%s,\"temperature\":%d,\"screen\":%u,"
        "\"autoSleepSeconds\":%u,\"powerState\":\"%s\",\"heapFree\":%lu}",
        wifiEnabled ? "true" : "false",
        WiFi.status() == WL_CONNECTED ? "true" : "false",
        isBLEConnected() ? "true" : "false",
        weather.valid ? "true" : "false",
        weather.currentTemp,
        static_cast<unsigned int>(currentScreen),
        static_cast<unsigned int>(getAutoSleepTimeoutSeconds()),
        SystemManager::powerStateName(SystemManager::powerState()),
        static_cast<unsigned long>(ESP.getFreeHeap()));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::Status,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    } else {
        sendError(0x0002, "status_payload_too_large");
    }
}

void sendFaceStatus() {
    const FaceModel* face = FaceManager::getActiveFace();
    const TransferManager::Snapshot transfer = TransferManager::snapshot();
    const size_t fsTotal = SPIFFS.totalBytes();
    const size_t fsUsed = SPIFFS.usedBytes();
    const size_t fsFree = fsTotal > fsUsed ? fsTotal - fsUsed : 0;
    char payload[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
    const int length = snprintf(payload, sizeof(payload),
        "{\"faceId\":\"%s\",\"faceName\":\"%.24s\",\"transfer\":\"%s\","
        "\"rx\":%lu,\"total\":%lu,\"fsUsed\":%lu,\"fsFree\":%lu}",
        face ? face->designId : "",
        face ? face->name : "",
        TransferManager::stateName(transfer.state),
        static_cast<unsigned long>(transfer.receivedBytes),
        static_cast<unsigned long>(transfer.totalBytes),
        static_cast<unsigned long>(fsUsed),
        static_cast<unsigned long>(fsFree));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::FaceStatus,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    } else {
        sendError(0x0002, "face_status_payload_too_large");
    }
}

uint16_t negotiatedChunkSize() {
    const uint16_t mtu = BLEDevice::getMTU();
    if (mtu <= 27) return 0;
    const uint16_t available = static_cast<uint16_t>(mtu - 27);
    return available < 216 ? available : 216;
}

void sendCapabilities() {
    const size_t inactiveBytes = FaceStorage::slotSize(FaceStorage::inactiveSlot());
    const size_t stageCapacity = StorageManager::freeBytes() + inactiveBytes;
    const uint16_t chunkLimit = negotiatedChunkSize();
    char payload[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
    const int length = snprintf(payload, sizeof(payload),
        "{\"sv\":1,\"pv\":1,\"pid\":\"%s\",\"cw\":%u,\"ch\":%u,"
        "\"cs\":\"%s\",\"bw\":%u,\"bh\":%u,\"obj\":%u,"
        "\"assets\":%u,\"json\":16384,\"img\":1,\"el\":\"t,dc,ac,d,w,s,i\",\"wc\":1,\"upload\":%u,"
        "\"chunk\":%u,\"stage\":%lu}",
        DisplayProfileConfig::ID,
        static_cast<unsigned>(DisplayProfileConfig::WIDTH),
        static_cast<unsigned>(DisplayProfileConfig::HEIGHT),
        DisplayProfileConfig::IS_CIRCULAR ? "circle" : "rectangle",
        static_cast<unsigned>(DisplayProfileConfig::MAX_BITMAP_WIDTH),
        static_cast<unsigned>(DisplayProfileConfig::MAX_BITMAP_HEIGHT),
        static_cast<unsigned>(DisplayProfileConfig::MAX_OBJECTS),
        static_cast<unsigned>(DisplayProfileConfig::MAX_ASSETS),
        chunkLimit >= 16 ? 1U : 0U,
        static_cast<unsigned>(chunkLimit),
        static_cast<unsigned long>(stageCapacity));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload)) {
        sendFrame(controlTx, WfsProtocol::MessageType::Capabilities,
                  reinterpret_cast<const uint8_t*>(payload), static_cast<uint16_t>(length));
    } else {
        sendError(0x0002, "capabilities_payload_too_large");
    }
}

void handleControlPacket(const WfsPacket& packet) {
    WfsProtocol::FrameView frame{};
    // Decode without a session constraint first so a fresh HELLO (session 0)
    // can explicitly replace an existing session. All other control messages
    // are checked against the active session immediately below.
    const WfsProtocol::FrameError decodeError =
        WfsProtocol::decodeFrame(packet.bytes, packet.length, 0, frame);
    if (decodeError != WfsProtocol::FrameError::None) {
        sendError(0x0002, WfsProtocol::frameErrorName(decodeError));
        return;
    }
    if (frame.payloadLength > WfsProtocol::MAX_CONTROL_PAYLOAD) {
        sendError(0x0002, "control_payload_too_large");
        return;
    }

    if (frame.type == WfsProtocol::MessageType::Hello) {
        if (frame.sessionId != 0) {
            sendError(0x0003, "hello_session_must_be_zero");
            return;
        }
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
        // A repeated HELLO starts a new protocol session. Tear down any
        // unfinished transfer from the previous session before changing IDs.
        TransferManager::abortSession();
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
        case WfsProtocol::MessageType::GetStatus:
            sendDeviceStatus();
            break;
        case WfsProtocol::MessageType::GetFaceStatus:
            if (frame.payloadLength != 0) sendError(0x0010, "face_status_payload_not_empty");
            else sendFaceStatus();
            break;
        case WfsProtocol::MessageType::Command: {
            CommandHandler::Result result{};
            CommandHandler::dispatch(frame.payload, frame.payloadLength, result);
            char response[WfsProtocol::MAX_CONTROL_PAYLOAD + 1] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\"requestId\":\"%s\",\"ok\":%s,\"code\":%u,\"message\":\"%s\",\"value\":%ld}",
                result.requestId, result.ok ? "true" : "false",
                static_cast<unsigned int>(result.errorCode), result.message,
                static_cast<long>(result.value));
            if (responseLength > 0 && static_cast<size_t>(responseLength) < sizeof(response)) {
                sendFrame(controlTx, WfsProtocol::MessageType::Response,
                          reinterpret_cast<const uint8_t*>(response),
                          static_cast<uint16_t>(responseLength));
            }
            if (result.requestSleep) {
                delay(80);
                CommandHandler::executeDeferred(result);
            }
            break;
        }
        case WfsProtocol::MessageType::Ping:
            sendFrame(controlTx, WfsProtocol::MessageType::Pong, nullptr, 0);
            break;
        case WfsProtocol::MessageType::TransferBegin: {
            if (!canStartWfsTransfer()) {
                sendError(0x0024, "resource_busy");
                break;
            }
            JsonDocument request;
            if (deserializeJson(request, frame.payload, frame.payloadLength,
                                DeserializationOption::NestingLimit(4))) {
                sendError(0x0022, "transfer_begin_json_invalid");
                break;
            }
            const JsonVariantConst transferIdValue = request["transferId"];
            const JsonVariantConst packageLengthValue = request["packageLength"];
            const JsonVariantConst designIdValue = request["designId"];
            const JsonVariantConst digestValue = request["payloadSha256"];
            if (!transferIdValue.is<uint32_t>() || !packageLengthValue.is<uint32_t>() ||
                !designIdValue.is<const char*>() || !digestValue.is<const char*>()) {
                sendError(0x0022, "transfer_begin_fields_invalid");
                break;
            }
            const uint32_t transferId = transferIdValue.as<uint32_t>();
            const uint32_t packageLength = packageLengthValue.as<uint32_t>();
            const char* designId = designIdValue.as<const char*>();
            const char* digestText = digestValue.as<const char*>();
            uint8_t digest[32] = {};
            const uint16_t chunkSize = negotiatedChunkSize();
            if (chunkSize < 16) {
                sendError(0x0023, "negotiated_mtu_too_small");
                break;
            }
            if (!WfsDigest::parseSha256Hex(digestText, digest) ||
                !TransferManager::begin(transferId, packageLength, digest, designId)) {
                sendError(0x0023, TransferManager::errorName(TransferManager::snapshot().error));
                break;
            }
            char response[160] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\"transferId\":%lu,\"chunkSize\":%u,\"nextChunk\":0}",
                static_cast<unsigned long>(transferId), static_cast<unsigned int>(chunkSize));
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
            const JsonVariantConst transferIdValue = request["transferId"];
            if (!transferIdValue.is<uint32_t>() || transferIdValue.as<uint32_t>() == 0) {
                sendError(0x0022, "transfer_end_transfer_id_invalid");
                break;
            }
            const uint32_t transferId = transferIdValue.as<uint32_t>();
            const bool verified = TransferManager::finish(transferId);
            char response[160] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\"transferId\":%lu,\"ok\":%s,\"error\":\"%s\"}",
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
            const JsonVariantConst transferIdValue = request["transferId"];
            if (!transferIdValue.is<uint32_t>() || transferIdValue.as<uint32_t>() == 0) {
                sendError(0x0022, "apply_transfer_id_invalid");
                break;
            }
            const uint32_t transferId = transferIdValue.as<uint32_t>();
            const bool applied = TransferManager::apply(transferId);
            const FaceModel* active = FaceManager::getActiveFace();
            char response[180] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\"transferId\":%lu,\"ok\":%s,\"designId\":\"%s\"}",
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
            const JsonVariantConst transferIdValue = request["transferId"];
            if (!transferIdValue.is<uint32_t>() || transferIdValue.as<uint32_t>() == 0) {
                sendError(0x0022, "cancel_transfer_id_invalid");
                break;
            }
            const uint32_t transferId = transferIdValue.as<uint32_t>();
            if (!TransferManager::cancel(transferId)) {
                sendError(0x0026, "transfer_not_cancellable");
                break;
            }
            char response[96] = {};
            const int responseLength = snprintf(response, sizeof(response),
                "{\"event\":\"TRANSFER_CANCELLED\",\"transferId\":%lu}",
                static_cast<unsigned long>(transferId));
            if (responseLength > 0) sendFrame(controlTx, WfsProtocol::MessageType::Event,
                reinterpret_cast<const uint8_t*>(response), static_cast<uint16_t>(responseLength));
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
    if (frame.payloadLength > WfsProtocol::MAX_DATA_FRAME_BYTES - WfsProtocol::FRAME_HEADER_SIZE) {
        sendError(0x0004, "data_payload_too_large");
        return;
    }
    if (sessionId == 0 || frame.sessionId != sessionId) {
        sendError(0x0004, "data_sequence_or_session_invalid");
        return;
    }
    const bool newSequence =
        frame.sequence == static_cast<uint16_t>(lastControlSequence + 1U);
    const bool retrySequence =
        frame.type == WfsProtocol::MessageType::Data &&
        frame.sequence == lastControlSequence;
    if (!newSequence && !retrySequence) {
        sendError(0x0004, "data_sequence_or_session_invalid");
        return;
    }
    if (newSequence) lastControlSequence = frame.sequence;
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

void rollbackWfsGattSetup() {
    wfsService = nullptr;
    controlTx = nullptr;
    dataTx = nullptr;
    if (rxQueue != nullptr) {
        vQueueDelete(rxQueue);
        rxQueue = nullptr;
    }
    sessionId = 0;
    txSequence = 0;
    lastControlSequence = 0;
    queueDrops = 0;
    __atomic_store_n(&sessionResetPending, false, __ATOMIC_RELEASE);
}
}

bool beginWfsGatt(BLEServer* server) {
    if (server == nullptr) return false;
    if (rxQueue == nullptr) rxQueue = xQueueCreate(QUEUE_DEPTH, sizeof(WfsPacket));
    if (rxQueue == nullptr) {
        Serial.println("[WFS] Queue allocation failed.");
        return false;
    }

    wfsService = server->createService(WfsProtocol::SERVICE_UUID);
    if (wfsService == nullptr) {
        rollbackWfsGattSetup();
        return false;
    }

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
        rollbackWfsGattSetup();
        return false;
    }

    controlRx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
    controlTx->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
    dataRx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
    dataTx->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
    controlTx->addDescriptor(new BLE2902());
    dataTx->addDescriptor(new BLE2902());
    controlRx->setCallbacks(new WfsWriteCallbacks(&rxQueue, 0));
    dataRx->setCallbacks(new WfsWriteCallbacks(&rxQueue, 1));
    controlTx->setValue("WFS1_READY");
    dataTx->setValue("WFS1_DATA_READY");
    wfsService->start();

    Serial.println("[WFS] Protocol v1 GATT service started.");
    return true;
}

void applyPendingSessionReset() {
    if (!__atomic_exchange_n(&sessionResetPending, false, __ATOMIC_ACQ_REL)) return;
    sessionId = 0;
    txSequence = 0;
    lastControlSequence = 0;
    TransferManager::abortSession();
}

void serviceWfsBle() {
    applyPendingSessionReset();
    const uint32_t dropped = __atomic_exchange_n(&queueDrops, 0, __ATOMIC_RELAXED);
    if (dropped) Serial.printf("[WFS] RX queue dropped %lu frame(s).\n", static_cast<unsigned long>(dropped));

    if (rxQueue != nullptr) {
        for (uint8_t processed = 0; processed < MAX_PACKETS_PER_LOOP; ++processed) {
            WfsPacket packet{};
            if (xQueueReceive(rxQueue, &packet, 0) != pdTRUE) break;
            // A connect/disconnect callback may run between the outer check and dequeue.
            applyPendingSessionReset();
            if (packet.generation != __atomic_load_n(&rxGeneration, __ATOMIC_ACQUIRE)) continue;
            if (packet.channel == 1) handleDataPacket(packet);
            else handleControlPacket(packet);
        }
    }
}

void requestWfsSessionReset() {
    __atomic_add_fetch(&rxGeneration, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&sessionResetPending, true, __ATOMIC_RELEASE);
}

void resetWfsSession() {
    __atomic_add_fetch(&rxGeneration, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&sessionResetPending, false, __ATOMIC_RELEASE);
    sessionId = 0;
    txSequence = 0;
    lastControlSequence = 0;
    if (rxQueue != nullptr) xQueueReset(rxQueue);
    TransferManager::abortSession();
}

void shutdownWfsGatt() {
    resetWfsSession();
    wfsService = nullptr;
    controlTx = nullptr;
    dataTx = nullptr;
    if (rxQueue != nullptr) {
        vQueueDelete(rxQueue);
        rxQueue = nullptr;
    }
}
