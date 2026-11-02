#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "ble_service.h"

static constexpr char SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static constexpr char CHARACTERISTIC_UUID[] = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

static BLECharacteristic *commandCharacteristic = nullptr;
static QueueHandle_t commandQueue = nullptr;
static volatile bool clientConnected = false;
static bool bleInitialized = false;

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        clientConnected = true;
    }

    void onDisconnect(BLEServer *server) override {
        clientConnected = false;
        server->getAdvertising()->start();
    }
};

class CharacteristicCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        if (commandQueue == nullptr) return;

        std::string value = characteristic->getValue();
        if (value.empty() || value.size() >= 256) return;

        char buffer[256] = {};
        memcpy(buffer, value.data(), value.size());
        xQueueSend(commandQueue, buffer, 0);
    }
};

void beginBLE() {
    if (bleInitialized) {
        Serial.println("[BLE] Already enabled.");
        return;
    }
    if (commandQueue == nullptr) {
        commandQueue = xQueueCreate(5, 256);
    }
    if (commandQueue == nullptr) {
        Serial.println("[BLE] Command queue allocation failed.");
        return;
    }

    BLEDevice::init("ESP32_Watch_C3");
    BLEDevice::setMTU(185);

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);

    commandCharacteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    commandCharacteristic->addDescriptor(new BLE2902());
    commandCharacteristic->setCallbacks(new CharacteristicCallbacks());
    commandCharacteristic->setValue("READY");

    service->start();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->start();

    bleInitialized = true;
    Serial.println("[BLE] Advertising as ESP32_Watch_C3.");
}

void toggleBLE() {
    if (bleInitialized) {
        BLEAdvertising *advertising = BLEDevice::getAdvertising();
        if (advertising != nullptr) advertising->stop();

        clientConnected = false;
        commandCharacteristic = nullptr;
        BLEDevice::deinit(true);
        bleInitialized = false;
        Serial.println("[BLE] Bluetooth disabled.");
        return;
    }

    beginBLE();
}

bool isBLEEnabled() {
    return bleInitialized;
}

bool readBLECommand(String &command) {
    if (commandQueue == nullptr) return false;

    char buffer[256] = {};
    if (xQueueReceive(commandQueue, buffer, 0) != pdTRUE) {
        return false;
    }

    command = String(buffer);
    return true;
}

void sendBLENotification(const String &message) {
    if (!clientConnected || commandCharacteristic == nullptr) return;

    // Keep each notification within the default ATT payload size. The web
    // client concatenates notifications until it receives the closing ']'.
    for (size_t offset = 0; offset < message.length(); offset += 20) {
        String chunk = message.substring(offset, offset + 20);
        commandCharacteristic->setValue(chunk.c_str());
        commandCharacteristic->notify();
        delay(12);
    }
}

bool isBLEConnected() {
    return clientConnected;
}
