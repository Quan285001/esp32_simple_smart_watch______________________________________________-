#pragma once

#include <stdint.h>

namespace FaceStoragePolicy {

constexpr uint8_t INVALID_SLOT = 0xFF;

// The NVS pointer is the commit record. Never fall back to the other file:
// it may be an incomplete package left by an interrupted first transfer.
inline uint8_t selectBootSlot(uint8_t committedSlot, bool slotAExists, bool slotBExists) {
    if (committedSlot == 0 && slotAExists) return 0;
    if (committedSlot == 1 && slotBExists) return 1;
    return INVALID_SLOT;
}

inline uint8_t inactiveSlot(uint8_t activeSlot) {
    if (activeSlot == INVALID_SLOT) return 0;
    return activeSlot == 0 ? 1 : 0;
}

inline bool mayRemoveSlot(uint8_t slot, uint8_t activeSlot) {
    return slot <= 1 && slot != activeSlot;
}

inline bool mayActivateSlot(uint8_t slot, bool slotExists) {
    return slot <= 1 && slotExists;
}

} // namespace FaceStoragePolicy
