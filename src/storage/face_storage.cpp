#include "face_storage.h"

#include <Preferences.h>

#include "storage_manager.h"

namespace FaceStorage {
namespace {
constexpr char NVS_NAMESPACE[] = "face";
constexpr char ACTIVE_SLOT_KEY[] = "active";
constexpr char SLOT_A_PATH[] = "/face_a.wfs";
constexpr char SLOT_B_PATH[] = "/face_b.wfs";
uint8_t currentActiveSlot = INVALID_SLOT;
}

bool begin() {
    if (!StorageManager::isReady()) return false;

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    const uint8_t stored = prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT);
    prefs.end();

    if (stored <= 1 && StorageManager::exists(slotPath(stored))) {
        currentActiveSlot = stored;
        return true;
    }

    // Recover only if a committed pointer existed but its selected file is
    // missing. An unset pointer means no package was committed; never promote
    // an inactive slot that may contain a partial transfer after power loss.
    if (stored <= 1) {
        const uint8_t fallback = stored == 0 ? 1 : 0;
        if (StorageManager::exists(slotPath(fallback))) {
            currentActiveSlot = fallback;
            Serial.printf("[FACE_STORAGE] Active pointer target missing; selected fallback slot %u.\\n", fallback);
            return true;
        }
    }

    currentActiveSlot = INVALID_SLOT;
    return false;
}

uint8_t activeSlot() {
    return currentActiveSlot;
}

uint8_t inactiveSlot() {
    if (currentActiveSlot == INVALID_SLOT) return 0;
    return currentActiveSlot == 0 ? 1 : 0;
}

const char* slotPath(uint8_t slot) {
    if (slot == 0) return SLOT_A_PATH;
    if (slot == 1) return SLOT_B_PATH;
    return nullptr;
}

size_t slotSize(uint8_t slot) {
    const char* path = slotPath(slot);
    if (path == nullptr || !StorageManager::exists(path)) return 0;
    File file = StorageManager::open(path, "r");
    if (!file) return 0;
    const size_t size = file.size();
    file.close();
    return size;
}

bool removeSlot(uint8_t slot) {
    if (slot > 1 || slot == currentActiveSlot) return false;
    const char* path = slotPath(slot);
    return path != nullptr && (!StorageManager::exists(path) || StorageManager::remove(path));
}

File openSlot(uint8_t slot, const char* mode) {
    const char* path = slotPath(slot);
    if (path == nullptr || mode == nullptr) return File();
    return StorageManager::open(path, mode);
}

bool activateSlot(uint8_t slot) {
    if (slot > 1 || !StorageManager::exists(slotPath(slot))) return false;

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
