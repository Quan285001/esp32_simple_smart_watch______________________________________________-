#pragma once

#include <stdint.h>

namespace FaceStoragePolicy {

constexpr uint8_t INVALID_SLOT = 0xFF;

// Never infer that an uncommitted slot is active. This is essential when the
// device loses power while receiving the very first package.
inline uint8_t selectBootSlot(uint8_t committedSlot, bool slotAExists, bool slotBExists) {
    if (committedSlot == 0) {
        if (slotAExists) return 0;
        return slotBExists ? 1 : INVALID_SLOT;
    }
    if (committedSlot == 1) {
        if (slotBExists) return 1;
        return slotAExists ? 0 : INVALID_SLOT;
    }
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
