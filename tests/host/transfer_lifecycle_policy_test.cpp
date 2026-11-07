#include "../../src/communication/transfer_manager.h"

#include <cassert>

int main() {
    using TransferManager::State;
    using TransferManager::canCancel;

    assert(canCancel(State::Receiving));
    assert(canCancel(State::Verifying));
    assert(canCancel(State::Verified));
    assert(canCancel(State::Failed));

    assert(!canCancel(State::Idle));
    assert(!canCancel(State::Applying));
    assert(!canCancel(State::Complete));
    assert(!canCancel(State::Cancelled));
    return 0;
}
