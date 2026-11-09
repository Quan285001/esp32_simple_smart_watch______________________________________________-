#include "../../src/communication/transfer_sequence.h"

#include <cassert>
#include <cstdint>

int main() {
    using WfsTransferSequence::Decision;
    using WfsTransferSequence::Receiver;

    Receiver receiver;
    const uint8_t first[] = {0x10, 0x20, 0x30};
    const uint8_t second[] = {0x40, 0x50};
    const uint8_t changed[] = {0x10, 0x20, 0x31};

    assert(receiver.nextIndex() == 0);
    assert(receiver.inspect(0, first, sizeof(first)) == Decision::Accept);
    assert(receiver.commit(0, first, sizeof(first)));
    assert(receiver.nextIndex() == 1);

    // A lost ACK may cause the sender to retransmit the exact prior chunk.
    assert(receiver.inspect(0, first, sizeof(first)) == Decision::Duplicate);
    assert(!receiver.commit(0, first, sizeof(first)));
    assert(receiver.nextIndex() == 1);

    // Same index with different bytes is never treated as a valid retry.
    assert(receiver.inspect(0, changed, sizeof(changed)) == Decision::OutOfOrder);

    // Stop-and-wait rejects a skipped index and accepts only the next index.
    assert(receiver.inspect(2, second, sizeof(second)) == Decision::OutOfOrder);
    assert(receiver.inspect(1, second, sizeof(second)) == Decision::Accept);
    assert(receiver.commit(1, second, sizeof(second)));
    assert(receiver.nextIndex() == 2);

    // Invalid payloads are rejected before any state is advanced.
    assert(receiver.inspect(2, nullptr, 1) == Decision::InvalidPayload);
    assert(receiver.inspect(2, second, 0) == Decision::InvalidPayload);
    uint8_t oversized[WfsTransferSequence::MAX_CHUNK_BYTES + 1] = {};
    assert(receiver.inspect(2, oversized, sizeof(oversized)) == Decision::InvalidPayload);
    assert(receiver.nextIndex() == 2);

    receiver.reset();
    assert(receiver.nextIndex() == 0);
    assert(receiver.inspect(0, first, sizeof(first)) == Decision::Accept);

    // Transfer inactivity uses unsigned elapsed time so millis() wraparound
    // cannot keep an abandoned session alive forever.
    WfsTransferSequence::InactivityTimer timer;
    timer.start(1000);
    assert(!timer.expired(1000, 100));
    assert(!timer.expired(1099, 100));
    assert(timer.expired(1100, 100));
    timer.touch(1100);
    assert(!timer.expired(1199, 100));
    assert(timer.expired(1200, 100));

    timer.start(0xFFFFFFF0U);
    assert(!timer.expired(0x0000000FU, 32));
    assert(timer.expired(0x00000010U, 32));
    timer.clear();
    assert(!timer.expired(0x00000020U, 1));
    return 0;
}
