#include "face_storage.h"

#include <Preferences.h>

#include "storage_manager.h"
#include "face_storage_policy.h"

namespace FaceStorage {
namespace {
constexpr char NVS_NAMESPACE[] = "face";
constexpr char ACTIVE_SLOT_KEY[] = "active";
constexpr char SLOT_A_PATH[] = "/face_a.wfs";
constexpr char SLOT_B_PATH[] = "/face_b.wfs";
uint8_t currentActiveSlot = INVALID_SLOT;
bool storageInitialized = false;
}

bool begin() {
    // Clear RAM state before every initialization attempt. If SPIFFS or NVS
    // cannot be opened, callers must never observe a stale active slot.
    currentActiveSlot = INVALID_SLOT;
    storageInitialized = false;
    if (!StorageManager::isReady()) return false;

    Preferences prefs;
    // Open read-write so the namespace is created on a first boot with no
    // committed face yet. No key is written unless a transfer is applied.
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    const uint8_t stored = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
    prefs.end();
    storageInitialized = true;

    currentActiveSlot = FaceStoragePolicy::selectBootSlot(
        stored,
        StorageManager::exists(SLOT_A_PATH),
        StorageManager::exists(SLOT_B_PATH));

    if (currentActiveSlot <= 1) return true;

    // A missing/invalid commit record is not permission to promote the other
    // slot: it may be an incomplete package left by an interrupted transfer.
    Serial.printf("[FACE_STORAGE] No valid committed face slot (pointer=%u); refusing recovery promotion.\n",
                  stored);
    return false;
}

bool isReady() {
    return storageInitialized;
}

uint8_t activeSlot() {
    return currentActiveSlot;
}

uint8_t inactiveSlot() {
    return storageInitialized
        ? FaceStoragePolicy::inactiveSlot(currentActiveSlot)
        : INVALID_SLOT;
}

const char* slotPath(uint8_t slot) {
    if (slot == 0) return SLOT_A_PATH;
    if (slot == 1) return SLOT_B_PATH;
    return nullptr;
}

size_t slotSize(uint8_t slot) {
    if (!storageInitialized) return 0;
    const char* path = slotPath(slot);
    if (path == nullptr || !StorageManager::exists(path)) return 0;
    fs::File file = StorageManager::open(path, "r");
    if (!file) return 0;
    const size_t size = file.size();
    file.close();
    return size;
}

bool removeSlot(uint8_t slot) {
    if (!storageInitialized ||
        !FaceStoragePolicy::mayRemoveSlot(slot, currentActiveSlot)) return false;
    const char* path = slotPath(slot);
    return path != nullptr && (!StorageManager::exists(path) || StorageManager::remove(path));
}

fs::File openSlot(uint8_t slot, const char* mode) {
    if (!storageInitialized) return fs::File();
    const char* path = slotPath(slot);
    if (path == nullptr || mode == nullptr) return fs::File();
    return StorageManager::open(path, mode);
}

bool activateSlot(uint8_t slot) {
    if (!storageInitialized ||
        !FaceStoragePolicy::mayActivateSlot(slot, StorageManager::exists(slotPath(slot)))) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;

    const uint8_t previousSlot = currentActiveSlot;
    const size_t written = prefs.putUChar(ACTIVE_SLOT_KEY, slot);
    const uint8_t verified = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
    if (written != sizeof(uint8_t) || verified != slot) {
        // putUChar may have reached NVS even if the read-back check failed.
        // Restore the previous commit record before reporting activation failure.
        if (previousSlot <= 1) {
            prefs.putUChar(ACTIVE_SLOT_KEY, previousSlot);
        } else {
            prefs.remove(ACTIVE_SLOT_KEY);
        }
        const uint8_t restored = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
        prefs.end();

        if (restored != previousSlot) {
            Serial.println("[FACE_STORAGE] WARNING: failed to restore active-slot commit record.");
        }
        return false;
    }

    prefs.end();
    currentActiveSlot = slot;
    return true;
}

} // namespace FaceStorage
