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

static BLEServer *bleServer = nullptr;
static BLEAdvertising *bleAdvertising = nullptr;
static BLECharacteristic *commandCharacteristic = nullptr;
static QueueHandle_t commandQueue = nullptr;

static volatile bool clientConnected = false;
static volatile bool bleEnabled = false;
static bool bleStackInitialized = false;

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        clientConnected = true;
    }

    void onDisconnect(BLEServer *server) override {
        clientConnected = false;

        // Resume advertising only when Bluetooth is logically enabled.
        if (bleEnabled && server != nullptr) {
            server->getAdvertising()->start();
        }
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
    if (bleStackInitialized) {
        Serial.println("[BLE] Stack already initialized.");
        return;
    }

    if (commandQueue == nullptr) {
        commandQueue = xQueueCreate(5, 256);
    }

    if (commandQueue == nullptr) {
        Serial.println("[BLE] Command queue allocation failed.");
        return;
    }

    Serial.println("[BLE] Initializing stack once...");
    BLEDevice::init("ESP32_Watch_C3");
    bleStackInitialized = true;

    Serial.println("[BLE] Creating server...");
    bleServer = BLEDevice::createServer();
    if (bleServer == nullptr) {
        Serial.println("[BLE] Server creation failed.");
        return;
    }
    bleServer->setCallbacks(new ServerCallbacks());

    BLEService *service = bleServer->createService(SERVICE_UUID);
    if (service == nullptr) {
        Serial.println("[BLE] Service creation failed.");
        return;
    }

    commandCharacteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR |
            BLECharacteristic::PROPERTY_NOTIFY
    );

    if (commandCharacteristic == nullptr) {
        Serial.println("[BLE] Characteristic creation failed.");
        return;
    }

    commandCharacteristic->addDescriptor(new BLE2902());
    commandCharacteristic->setCallbacks(new CharacteristicCallbacks());
    commandCharacteristic->setValue("READY");

    service->start();

    bleAdvertising = BLEDevice::getAdvertising();
    if (bleAdvertising == nullptr) {
        Serial.println("[BLE] Advertising instance unavailable.");
        return;
    }

    bleAdvertising->addServiceUUID(SERVICE_UUID);
    bleAdvertising->setScanResponse(true);

    bleEnabled = true;
    bleAdvertising->start();

    Serial.println("[BLE] Advertising as ESP32_Watch_C3.");
}

void toggleBLE() {
    if (!bleStackInitialized) {
        Serial.println("[BLE] Stack not initialized; initializing now.");
        beginBLE();
        return;
    }

    if (bleEnabled) {
        // Keep the BLE host/server alive. Only stop discoverability.
        // This avoids deinit(true) followed by an unsafe stack reinitialization.
        if (bleAdvertising != nullptr) {
            bleAdvertising->stop();
        }

        bleEnabled = false;
        Serial.println("[BLE] Bluetooth OFF (advertising stopped; stack retained).");
        return;
    }

    if (bleAdvertising == nullptr) {
        bleAdvertising = BLEDevice::getAdvertising();
    }

    if (bleAdvertising == nullptr) {
        Serial.println("[BLE] Cannot enable: advertising instance unavailable.");
        return;
    }

    bleEnabled = true;
    bleAdvertising->start();
    Serial.println("[BLE] Bluetooth ON (advertising restarted; stack retained).");
}

// Fully release the Bluetooth controller/host before light sleep.
// beginBLE() can initialize a fresh stack after the device wakes.
void shutdownBLEForSleep() {
    if (bleAdvertising != nullptr) {
        bleAdvertising->stop();
    }

    if (bleStackInitialized) {
        BLEDevice::deinit(true);
    }

    bleAdvertising = nullptr;
    commandCharacteristic = nullptr;
    bleServer = nullptr;
    clientConnected = false;
    bleEnabled = false;
    bleStackInitialized = false;

    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }

    Serial.println("[BLE] Stack deinitialized for light sleep.");
}

bool isBLEEnabled() {
    return bleEnabled;
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

    // Default ATT MTU carries 20 bytes of notification payload.
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
