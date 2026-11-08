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
}

bool begin() {
    // Clear RAM state before every initialization attempt. If SPIFFS or NVS
    // cannot be opened, callers must never observe a stale active slot.
    currentActiveSlot = INVALID_SLOT;
    if (!StorageManager::isReady()) return false;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    const uint8_t stored = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
    prefs.end();

    currentActiveSlot = FaceStoragePolicy::selectBootSlot(
        stored,
        StorageManager::exists(SLOT_A_PATH),
        StorageManager::exists(SLOT_B_PATH));

    if (currentActiveSlot <= 1) return true;

    // A missing/invalid commit record is not permission to promote the other
    // slot: it may be an incomplete package left by an interrupted transfer.
    Serial.printf("[FACE_STORAGE] No valid committed face slot (pointer=%u); refusing recovery promotion.\\n",
                  stored);
}

uint8_t activeSlot() {
    return currentActiveSlot;
}

uint8_t inactiveSlot() {
    return FaceStoragePolicy::inactiveSlot(currentActiveSlot);
}

const char* slotPath(uint8_t slot) {
    if (slot == 0) return SLOT_A_PATH;
    if (slot == 1) return SLOT_B_PATH;
    return nullptr;
}

size_t slotSize(uint8_t slot) {
    const char* path = slotPath(slot);
    if (path == nullptr || !StorageManager::exists(path)) return 0;
    fs::File file = StorageManager::open(path, "r");
    if (!file) return 0;
    const size_t size = file.size();
    file.close();
    return size;
}

bool removeSlot(uint8_t slot) {
    if (!FaceStoragePolicy::mayRemoveSlot(slot, currentActiveSlot)) return false;
    const char* path = slotPath(slot);
    return path != nullptr && (!StorageManager::exists(path) || StorageManager::remove(path));
}

fs::File openSlot(uint8_t slot, const char* mode) {
    const char* path = slotPath(slot);
    if (path == nullptr || mode == nullptr) return fs::File();
    return StorageManager::open(path, mode);
}

bool activateSlot(uint8_t slot) {
    if (!FaceStoragePolicy::mayActivateSlot(slot, StorageManager::exists(slotPath(slot)))) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    const size_t written = prefs.putUChar(ACTIVE_SLOT_KEY, slot);
    const uint8_t verified = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
    prefs.end();

    if (written != sizeof(uint8_t) || verified != slot) return false;
    currentActiveSlot = slot;
    return true;
}

} // namespace FaceStorage
