#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace WfsTransferSequence {

constexpr size_t MAX_CHUNK_BYTES = 216;

enum class Decision : uint8_t {
    Accept,
    Duplicate,
    InvalidPayload,
    OutOfOrder
};

// Pure stop-and-wait sequencing policy. The caller commits only after the
// corresponding bytes have been written successfully to the staging file.
class InactivityTimer {
public:
    void start(uint32_t now) {
        lastActivity_ = now;
        armed_ = true;
    }

    void touch(uint32_t now) {
        if (armed_) lastActivity_ = now;
        else start(now);
    }

    void clear() {
        lastActivity_ = 0;
        armed_ = false;
    }

    bool expired(uint32_t now, uint32_t timeoutMs) const {
        return armed_ && timeoutMs > 0 &&
               static_cast<uint32_t>(now - lastActivity_) >= timeoutMs;
    }

private:
    uint32_t lastActivity_ = 0;
    bool armed_ = false;
};

class Receiver {
public:
    void reset() {
        nextIndex_ = 0;
        lastIndex_ = 0;
        lastLength_ = 0;
        hasLast_ = false;
    }

    Decision inspect(uint32_t index, const uint8_t* bytes, size_t length) const {
        if (bytes == nullptr || length == 0 || length > MAX_CHUNK_BYTES) {
            return Decision::InvalidPayload;
        }
        if (index == nextIndex_) return Decision::Accept;
        if (hasLast_ && index == lastIndex_ && length == lastLength_ &&
            memcmp(bytes, lastBytes_, length) == 0) {
            return Decision::Duplicate;
        }
        return Decision::OutOfOrder;
    }

    bool commit(uint32_t index, const uint8_t* bytes, size_t length) {
        if (inspect(index, bytes, length) != Decision::Accept) return false;
        memcpy(lastBytes_, bytes, length);
        lastLength_ = static_cast<uint16_t>(length);
        lastIndex_ = index;
        hasLast_ = true;
        ++nextIndex_;
        return true;
    }

    uint32_t nextIndex() const { return nextIndex_; }

private:
    uint32_t nextIndex_ = 0;
    uint32_t lastIndex_ = 0;
    uint16_t lastLength_ = 0;
    bool hasLast_ = false;
    uint8_t lastBytes_[MAX_CHUNK_BYTES] = {};
};

} // namespace WfsTransferSequence
