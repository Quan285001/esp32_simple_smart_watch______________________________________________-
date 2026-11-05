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
static constexpr UBaseType_t BLE_COMMAND_MAX_LEN = 128;
static volatile uint32_t commandQueueDropCount = 0;
static constexpr UBaseType_t BLE_COMMAND_QUEUE_LENGTH = 5;

static BLEServer *bleServer = nullptr;
static BLEAdvertising *bleAdvertising = nullptr;
static BLECharacteristic *commandCharacteristic = nullptr;
static QueueHandle_t commandQueue = nullptr;

static volatile bool clientConnected = false;
static volatile bool bleEnabled = false;
static volatile bool advertisingRestartPending = false;
static bool bleStackInitialized = false;

class ServerCallbacks final : public BLEServerCallbacks {
    /**
     * @brief Implements the on connect operation.
     * @param nullptr Input or state used by this operation.
     */
    void onConnect(BLEServer *) override {
        clientConnected = true;
    }

    void onDisconnect(BLEServer *) override {
        clientConnected = false;

        // BLE callbacks run in the host task. Defer advertising API calls
        // to serviceBLE(), which executes in the application loop.
        if (bleEnabled) {
            advertisingRestartPending = true;
            Serial.println("[BLE][TRACE] onDisconnect: advertising restart queued");
        }
    }
};

class CharacteristicCallbacks final : public BLECharacteristicCallbacks {
    /**
     * @brief Implements the on write operation.
     * @param memcpy(buffer Input or state used by this operation.
     * @param value.data() Input or state used by this operation.
     * @param xQueueSend(commandQueue Input or state used by this operation.
     * @param buffer Input or state used by this operation.
     * @param beginBLE( Input or state used by this operation.
     */
    void onWrite(BLECharacteristic *characteristic) override {
        if (commandQueue == nullptr) return;

        std::string value = characteristic->getValue();
        if (value.empty() || value.size() >= BLE_COMMAND_MAX_LEN) return;

        char buffer[BLE_COMMAND_MAX_LEN] = {};
        memcpy(buffer, value.data(), value.size());
        if (xQueueSend(commandQueue, buffer, 0) != pdTRUE) {
            // Count drops without logging or blocking inside the host callback.
            __atomic_fetch_add(&commandQueueDropCount, 1, __ATOMIC_RELAXED);
        }
    }
};

/**
 * @brief Initializes the BLE server, GATT service, characteristic, and advertising.

 */
void beginBLE() {
    if (bleStackInitialized) {
        Serial.println("[BLE] Stack already initialized.");
        return;
    }

    if (commandQueue == nullptr) {
        commandQueue = xQueueCreate(BLE_COMMAND_QUEUE_LENGTH, BLE_COMMAND_MAX_LEN);
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

    advertisingRestartPending = false;
    bleEnabled = true;
    Serial.println("[BLE][TRACE] initial startAdvertising ENTER");
    bleAdvertising->start();
    Serial.println("[BLE][TRACE] initial startAdvertising EXIT");

    Serial.println("[BLE] Advertising as ESP32_Watch_C3.");
}

/**
 * @brief Enables or disables BLE advertising while retaining the initialized BLE stack.

 */
void toggleBLE() {
    if (!bleStackInitialized) {
        Serial.println("[BLE] Stack not initialized; initializing now.");
        beginBLE();
        return;
    }

    if (bleEnabled) {
        // Disable logical state first so a disconnect callback cannot queue
        // a restart while advertising is being stopped.
        bleEnabled = false;
        advertisingRestartPending = false;

        // Keep the BLE host/server alive; only stop discoverability.
        if (bleAdvertising != nullptr) {
            Serial.println("[BLE][TRACE] stopAdvertising ENTER");
            bleAdvertising->stop();
            Serial.println("[BLE][TRACE] stopAdvertising EXIT");
        } else {
            Serial.println("[BLE][TRACE] stopAdvertising skipped: null instance");
        }

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

    // A connected peripheral normally stops advertising. Defer restart until
    // the active client disconnects instead of starting over that connection.
    if (clientConnected) {
        advertisingRestartPending = true;
        Serial.println("[BLE] Bluetooth ON; advertising deferred until client disconnects.");
        return;
    }

    advertisingRestartPending = false;
    Serial.println("[BLE][TRACE] startAdvertising ENTER");
    bleAdvertising->start();
    Serial.println("[BLE][TRACE] startAdvertising EXIT");
    Serial.println("[BLE] Bluetooth ON (advertising restarted; stack retained).");
}

/**
 * @brief Processes deferred advertising restarts from the application loop.
 */
void serviceBLE() {
    const uint32_t dropped =
        __atomic_exchange_n(&commandQueueDropCount, 0, __ATOMIC_RELAXED);
    if (dropped > 0) {
        Serial.printf("[BLE] Command queue full; dropped %lu command(s).\n",
                      static_cast<unsigned long>(dropped));
    }

    if (!bleStackInitialized || !bleEnabled ||
        !advertisingRestartPending || clientConnected) {
        return;
    }

    if (bleAdvertising == nullptr) {
        bleAdvertising = BLEDevice::getAdvertising();
    }

    if (bleAdvertising == nullptr) {
        Serial.println("[BLE] Deferred advertising restart failed: no instance.");
        advertisingRestartPending = false;
        return;
    }

    advertisingRestartPending = false;
    Serial.println("[BLE][TRACE] deferred startAdvertising ENTER");
    bleAdvertising->start();
    Serial.println("[BLE][TRACE] deferred startAdvertising EXIT");
}

// Fully release the Bluetooth controller/host before light sleep.
// beginBLE() can initialize a fresh stack after the device wakes.
/**
 * @brief Stops advertising and deinitializes the BLE stack and command queue before Light Sleep.

 */
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
    advertisingRestartPending = false;
    bleStackInitialized = false;

    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }

    Serial.println("[BLE] Stack deinitialized for light sleep.");
}

/**
 * @brief Reports whether BLE advertising is logically enabled.

 */
bool isBLEEnabled() {
    return bleEnabled;
}

/**
 * @brief Reads one pending command from the FreeRTOS queue without blocking.
 * @param command Input or state used by this operation.
 */
bool readBLECommand(String &command) {
    if (commandQueue == nullptr) return false;

    char buffer[BLE_COMMAND_MAX_LEN] = {};
    if (xQueueReceive(commandQueue, buffer, 0) != pdTRUE) {
        return false;
    }

    command = String(buffer);
    return true;
}

/**
 * @brief Sends a BLE notification, splitting long messages into ATT-sized chunks.
 * @param message Input or state used by this operation.
 */
void sendBLENotification(const String &message) {
    if (!clientConnected || commandCharacteristic == nullptr) return;

    // Default ATT MTU carries 20 bytes of notification payload. Reuse one
    // stack buffer instead of allocating a new Arduino String for each chunk.
    const char *payload = message.c_str();
    const size_t payloadLength = message.length();
    char chunk[21];
    for (size_t offset = 0; offset < payloadLength; offset += 20) {
        const size_t chunkLength =
            (payloadLength - offset) < 20 ? (payloadLength - offset) : 20;
        memcpy(chunk, payload + offset, chunkLength);
        chunk[chunkLength] = '\0';
        commandCharacteristic->setValue(chunk);
        commandCharacteristic->notify();
        delay(12);
    }
}

/**
 * @brief Reports whether a BLE client is currently connected.

 */
bool isBLEConnected() {
    return clientConnected;
}
