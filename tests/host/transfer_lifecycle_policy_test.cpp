#include "../../src/communication/transfer_manager.h"

#include <cassert>

int main() {
    using TransferManager::State;
    using TransferManager::canCancel;
    using TransferManager::isBusyState;

    assert(canCancel(State::Receiving));
    assert(canCancel(State::Verifying));
    assert(canCancel(State::Verified));
    assert(canCancel(State::Failed));

    assert(!canCancel(State::Idle));
    assert(!canCancel(State::Applying));
    assert(!canCancel(State::Complete));
    assert(!canCancel(State::Cancelled));

    assert(isBusyState(State::Receiving));
    assert(isBusyState(State::Verifying));
    assert(isBusyState(State::Verified));
    assert(isBusyState(State::Applying));
    assert(!isBusyState(State::Idle));
    assert(!isBusyState(State::Complete));
    assert(!isBusyState(State::Failed));
    assert(!isBusyState(State::Cancelled));
    return 0;
}
