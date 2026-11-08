#include "../../src/storage/face_storage_policy.h"

#include <cassert>
#include <cstdint>

int main() {
    using FaceStoragePolicy::INVALID_SLOT;
    using FaceStoragePolicy::inactiveSlot;
    using FaceStoragePolicy::mayActivateSlot;
    using FaceStoragePolicy::mayRemoveSlot;
    using FaceStoragePolicy::selectBootSlot;

    // Only the persisted NVS commit pointer determines the active package.
    assert(selectBootSlot(0, true, true) == 0);
    assert(selectBootSlot(1, true, true) == 1);
    assert(selectBootSlot(0, true, false) == 0);
    assert(selectBootSlot(1, false, true) == 1);

    // Never promote the other slot when the committed target is missing:
    // it may be a partial package left by power loss during transfer.
    assert(selectBootSlot(0, false, true) == INVALID_SLOT);
    assert(selectBootSlot(1, true, false) == INVALID_SLOT);
    assert(selectBootSlot(INVALID_SLOT, true, false) == INVALID_SLOT);
    assert(selectBootSlot(INVALID_SLOT, false, true) == INVALID_SLOT);
    assert(selectBootSlot(7, true, true) == INVALID_SLOT);

    // The active slot is never the staging/removal target.
    assert(inactiveSlot(0) == 1);
    assert(inactiveSlot(1) == 0);
    assert(inactiveSlot(INVALID_SLOT) == 0);
    assert(!mayRemoveSlot(0, 0));
    assert(mayRemoveSlot(1, 0));
    assert(!mayRemoveSlot(1, 1));
    assert(!mayRemoveSlot(2, INVALID_SLOT));

    // Activation is limited to existing, valid slot indices; package
    // validation must be completed by the caller before this policy is used.
    assert(mayActivateSlot(0, true));
    assert(mayActivateSlot(1, true));
    assert(!mayActivateSlot(0, false));
    assert(!mayActivateSlot(2, true));
    return 0;
}
