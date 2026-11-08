#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TransferManager {

enum class State : uint8_t {
    Idle,
    Receiving,
    Verifying,
    Verified,
    Applying,
    Complete,
    Failed,
    Cancelled
};

constexpr bool isBusyState(State state) {
    return state == State::Receiving || state == State::Verifying ||
           state == State::Verified || state == State::Applying;
}

constexpr bool canCancel(State state) {
    return state == State::Receiving || state == State::Verifying ||
           state == State::Verified || state == State::Failed;
}

enum class Error : uint8_t {
    None,
    Busy,
    InvalidRequest,
    StorageUnavailable,
    InsufficientStorage,
    FileOpenFailed,
    WrongTransfer,
    WrongSequence,
    ChunkInvalid,
    WriteFailed,
    LengthMismatch,
    PackageInvalid,
    ApplyFailed,
    SessionLost
};

struct Snapshot {
    State state;
    Error error;
    uint32_t transferId;
    uint32_t totalBytes;
    uint32_t receivedBytes;
    uint32_t nextChunkIndex;
};

bool begin(uint32_t transferId, uint32_t packageLength,
           const uint8_t expectedPayloadDigest[32], const char* designId);
bool writeChunk(uint32_t transferId, uint32_t chunkIndex,
                const uint8_t* bytes, size_t length);
bool finish(uint32_t transferId);
bool apply(uint32_t transferId);
bool cancel(uint32_t transferId);
void abortSession();
Snapshot snapshot();
bool isBusy();
const char* stateName(State state);
const char* errorName(Error error);

} // namespace TransferManager
