#include "face_image.h"

#include <Preferences.h>
#include <esp_partition.h>
#include <esp_err.h>
#include <cstring>

#include "../services/ble_service.h"

namespace {
constexpr uint16_t IMAGE_WIDTH = 160;
constexpr uint16_t IMAGE_HEIGHT = 160;
constexpr uint32_t IMAGE_BYTES = IMAGE_WIDTH * IMAGE_HEIGHT * 2U;
constexpr uint32_t SLOT_BYTES = 0x10000;
constexpr uint32_t HEADER_BYTES = 32;
constexpr uint32_t IMAGE_MAGIC = 0x474D4946; // "FIMG"
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t PACKET_BEGIN = 0xF0;
constexpr uint8_t PACKET_DATA = 0xF1;
constexpr uint8_t PACKET_END = 0xF2;
constexpr uint8_t PACKET_CLEAR = 0xF3;

struct __attribute__((packed)) ImageHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
    uint32_t dataLength;
    uint32_t crc32;
};
static_assert(sizeof(ImageHeader) == HEADER_BYTES, "Unexpected image header size");

const esp_partition_t* activePartition = nullptr;
const esp_partition_t* stagingPartition = nullptr;
bool activeLookupDone = false;
bool transferActive = false;
uint8_t stagingSlot = 0xFF;
uint32_t expectedCrc = 0;
uint32_t receivedBytes = 0;
uint32_t lastPacketAt = 0;

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc;
}

const esp_partition_t* findSlot(uint8_t slot) {
    if (slot == 0) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_ANY, "face_a");
    }
    if (slot == 1) {
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                        ESP_PARTITION_SUBTYPE_ANY, "face_b");
    }
    return nullptr;
}

bool headerIsValid(const esp_partition_t* partition, ImageHeader& header) {
    if (partition == nullptr || partition->size < HEADER_BYTES + IMAGE_BYTES) return false;
    if (esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK) return false;
    return header.magic == IMAGE_MAGIC &&
           header.version == PROTOCOL_VERSION &&
           header.width == IMAGE_WIDTH &&
           header.height == IMAGE_HEIGHT &&
           header.dataLength == IMAGE_BYTES;
}

void loadActivePartition() {
    if (activeLookupDone) return;
    activeLookupDone = true;

    Preferences prefs;
    if (!prefs.begin("face", true)) return;
    const uint8_t slot = prefs.getUChar("imageSlot", 0xFF);
    prefs.end();
    if (slot > 1) return;

    const esp_partition_t* partition = findSlot(slot);
    ImageHeader header{};
    if (headerIsValid(partition, header)) {
        activePartition = partition;
    }
}

void abortTransfer(const char* reason) {
    transferActive = false;
    stagingPartition = nullptr;
    stagingSlot = 0xFF;
    receivedBytes = 0;
    Serial.printf("[FACE][IMAGE] Transfer aborted: %s\n", reason);
    sendBLENotification(String("IMG_ERR:") + reason);
}

bool beginTransfer(const uint8_t* packet, size_t length) {
    if (length != 13 || packet[1] != PROTOCOL_VERSION ||
        readU16(packet + 2) != IMAGE_WIDTH ||
        readU16(packet + 4) != IMAGE_HEIGHT ||
        readU32(packet + 6) != IMAGE_BYTES) {
        sendBLENotification("IMG_ERR:FORMAT");
        return false;
    }

    Preferences prefs;
    uint8_t activeSlot = 0xFF;
    if (prefs.begin("face", true)) {
        activeSlot = prefs.getUChar("imageSlot", 0xFF);
        prefs.end();
    }
    stagingSlot = (activeSlot == 0) ? 1 : 0;
    stagingPartition = findSlot(stagingSlot);
    if (stagingPartition == nullptr ||
        stagingPartition->size < HEADER_BYTES + IMAGE_BYTES ||
        (stagingPartition->address % 4096U) != 0 ||
        (stagingPartition->size % 4096U) != 0) {
        abortTransfer("PARTITION");
        return false;
    }

    const esp_err_t erased = esp_partition_erase_range(
        stagingPartition, 0, stagingPartition->size);
    if (erased != ESP_OK) {
        abortTransfer("ERASE");
        return false;
    }

    expectedCrc = readU32(packet + 9);
    receivedBytes = 0;
    lastPacketAt = millis();
    transferActive = true;
    Serial.printf("[FACE][IMAGE] Begin slot=%u bytes=%lu crc=%08lX\n",
                  stagingSlot,
                  static_cast<unsigned long>(IMAGE_BYTES),
                  static_cast<unsigned long>(expectedCrc));
    sendBLENotification("IMG_READY");
    return true;
}

bool writeChunk(const uint8_t* packet, size_t length) {
    if (!transferActive || stagingPartition == nullptr || length <= 5) {
        sendBLENotification("IMG_ERR:STATE");
        return false;
    }
    if (millis() - lastPacketAt > 60000UL) {
        abortTransfer("TIMEOUT");
        return false;
    }

    const uint32_t offset = readU32(packet + 1);
    const uint32_t payloadLength = static_cast<uint32_t>(length - 5);
    if ((offset & 0x03U) != 0 || (payloadLength & 0x03U) != 0 ||
        offset != receivedBytes || payloadLength > IMAGE_BYTES - receivedBytes) {
        abortTransfer("SEQUENCE");
        return false;
    }

    const esp_err_t result = esp_partition_write(
        stagingPartition, HEADER_BYTES + offset, packet + 5, payloadLength);
    if (result != ESP_OK) {
        abortTransfer("WRITE");
        return false;
    }

    receivedBytes += payloadLength;
    lastPacketAt = millis();
    return true;
}

bool finishTransfer() {
    if (!transferActive || stagingPartition == nullptr || receivedBytes != IMAGE_BYTES) {
        abortTransfer("INCOMPLETE");
        return false;
    }

    uint8_t buffer[512];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0;
    while (offset < IMAGE_BYTES) {
        const size_t count = (IMAGE_BYTES - offset < sizeof(buffer))
            ? static_cast<size_t>(IMAGE_BYTES - offset) : sizeof(buffer);
        if (esp_partition_read(stagingPartition, HEADER_BYTES + offset,
                               buffer, count) != ESP_OK) {
            abortTransfer("READ");
            return false;
        }
        crc = crc32Update(crc, buffer, count);
        offset += static_cast<uint32_t>(count);
    }
    crc ^= 0xFFFFFFFFUL;
    if (crc != expectedCrc) {
        abortTransfer("CRC");
        return false;
    }

    ImageHeader header{
        IMAGE_MAGIC, PROTOCOL_VERSION, IMAGE_WIDTH, IMAGE_HEIGHT,
        0, IMAGE_BYTES, expectedCrc
    };
    if (esp_partition_write(stagingPartition, 0, &header, sizeof(header)) != ESP_OK) {
        abortTransfer("HEADER");
        return false;
    }

    Preferences prefs;
    if (!prefs.begin("face", false)) {
        abortTransfer("NVS");
        return false;
    }
    const size_t written = prefs.putUChar("imageSlot", stagingSlot);
    const uint8_t verify = prefs.getUChar("imageSlot", 0xFF);
    prefs.end();
    if (written != sizeof(uint8_t) || verify != stagingSlot) {
        abortTransfer("ACTIVATE");
        return false;
    }

    activePartition = stagingPartition;
    activeLookupDone = true;
    transferActive = false;
    Serial.printf("[FACE][IMAGE] Activated slot=%u crc=%08lX\n",
                  stagingSlot, static_cast<unsigned long>(crc));
    stagingPartition = nullptr;
    stagingSlot = 0xFF;
    receivedBytes = 0;
    sendBLENotification("IMG_OK");
    return true;
}

void clearImage() {
    transferActive = false;
    stagingPartition = nullptr;
    stagingSlot = 0xFF;
    activePartition = nullptr;
    activeLookupDone = true;

    Preferences prefs;
    if (!prefs.begin("face", false)) {
        sendBLENotification("IMG_ERR:NVS");
        return;
    }
    const size_t written = prefs.putUChar("imageSlot", 0xFF);
    const uint8_t verify = prefs.getUChar("imageSlot", 0);
    prefs.end();
    if (written != sizeof(uint8_t) || verify != 0xFF) {
        sendBLENotification("IMG_ERR:CLEAR");
        return;
    }
    sendBLENotification("IMG_CLEARED");
}
} // namespace

void handleFaceImagePacket(const uint8_t* packet, size_t length) {
    if (packet == nullptr || length == 0) return;
    switch (packet[0]) {
        case PACKET_BEGIN:
            beginTransfer(packet, length);
            break;
        case PACKET_DATA:
            writeChunk(packet, length);
            break;
        case PACKET_END:
            if (length != 1) sendBLENotification("IMG_ERR:FORMAT");
            else finishTransfer();
            break;
        case PACKET_CLEAR:
            if (length != 1) sendBLENotification("IMG_ERR:FORMAT");
            else clearImage();
            break;
        default:
            sendBLENotification("IMG_ERR:COMMAND");
            break;
    }
}

bool drawFaceImageToSprite(TFT_eSprite& sprite) {
    loadActivePartition();
    if (activePartition == nullptr) return false;

    // Four scanlines per read keep stack/RAM use small on ESP32-C3.
    static uint16_t tile[IMAGE_WIDTH * 4];
    for (uint16_t y = 0; y < IMAGE_HEIGHT; y += 4) {
        if (esp_partition_read(activePartition,
                               HEADER_BYTES + static_cast<uint32_t>(y) * IMAGE_WIDTH * 2U,
                               tile, sizeof(tile)) != ESP_OK) {
            activePartition = nullptr;
            return false;
        }
        sprite.pushImage(0, y, IMAGE_WIDTH, 4, tile);
    }
    return true;
}
