#include "transfer_manager.h"
#include "transfer_sequence.h"

#include <SPIFFS.h>
#include <string.h>

#include "../storage/storage_manager.h"
#include "../storage/face_storage.h"
#include "../watchface/face_manager.h"
#include "../watchface/package_validator.h"
#include "../system/diagnostics.h"

namespace TransferManager {
namespace {
constexpr size_t MIN_PACKAGE_LENGTH = 64;
fs::File stagingFile;
State currentState = State::Idle;
Error currentError = Error::None;
uint32_t currentTransferId = 0;
uint32_t totalLength = 0;
uint32_t receivedLength = 0;
WfsTransferSequence::Receiver chunkSequence;
uint8_t stagingSlot = FaceStorage::INVALID_SLOT;
uint8_t previousActiveSlot = FaceStorage::INVALID_SLOT;
uint8_t expectedDigest[32] = {};
char expectedDesignId[FACE_ID_CAPACITY] = {};
FaceModel verifiedFace{};
uint32_t transferStartedAtMs = 0;
MemoryMonitor::Snapshot transferStartMemory{};

void fail(Error error) {
    if (stagingFile) {
        stagingFile.flush();
        stagingFile.close();
    }
    currentError = error;
    currentState = State::Failed;
}

void clearTransferFields() {
    currentTransferId = 0;
    totalLength = 0;
    receivedLength = 0;
    chunkSequence.reset();
    stagingSlot = FaceStorage::INVALID_SLOT;
    previousActiveSlot = FaceStorage::INVALID_SLOT;
    memset(expectedDigest, 0, sizeof(expectedDigest));
    memset(expectedDesignId, 0, sizeof(expectedDesignId));
}
}

bool begin(uint32_t transferId, uint32_t packageLength,
           const uint8_t expectedPayloadDigestValue[32], const char* designId) {
    if (isBusyState(currentState)) {
        currentError = Error::Busy;
        return false;
    }
    if (transferId == 0 || packageLength < MIN_PACKAGE_LENGTH ||
        expectedPayloadDigestValue == nullptr || designId == nullptr ||
        designId[0] == '\0' || strlen(designId) >= sizeof(expectedDesignId)) {
        currentState = State::Failed;
        currentError = Error::InvalidRequest;
        return false;
    }
    if (!StorageManager::isReady() || !FaceStorage::isReady()) {
        currentState = State::Failed;
        currentError = Error::StorageUnavailable;
        return false;
    }

    previousActiveSlot = FaceStorage::activeSlot();
    stagingSlot = FaceStorage::inactiveSlot();
    const size_t reclaimable = FaceStorage::slotSize(stagingSlot);
    const size_t availableAfterRemovingInactive = StorageManager::freeBytes() + reclaimable;
    if (packageLength > availableAfterRemovingInactive) {
        currentState = State::Failed;
        currentError = Error::InsufficientStorage;
        return false;
    }

    if (!FaceStorage::removeSlot(stagingSlot)) {
        currentState = State::Failed;
        currentError = Error::FileOpenFailed;
        return false;
    }
    stagingFile = FaceStorage::openSlot(stagingSlot, "w");
    if (!stagingFile) {
        currentState = State::Failed;
        currentError = Error::FileOpenFailed;
        return false;
    }

    clearTransferFields();
    // clearTransferFields resets slot metadata, so restore the reservation.
    stagingSlot = FaceStorage::inactiveSlot();
    previousActiveSlot = FaceStorage::activeSlot();
    currentTransferId = transferId;
    totalLength = packageLength;
    memcpy(expectedDigest, expectedPayloadDigestValue, sizeof(expectedDigest));
    strncpy(expectedDesignId, designId, sizeof(expectedDesignId) - 1);
    currentState = State::Receiving;
    currentError = Error::None;
    transferStartedAtMs = millis();
    transferStartMemory = SystemDiagnostics::captureMemory();
    SystemDiagnostics::logMemory("WFS receive begin");
    return true;
}

bool writeChunk(uint32_t transferId, uint32_t chunkIndex,
                const uint8_t* bytes, size_t length) {
    if (currentState != State::Receiving || transferId != currentTransferId) {
        currentError = Error::WrongTransfer;
        return false;
    }
    const WfsTransferSequence::Decision decision =
        chunkSequence.inspect(chunkIndex, bytes, length);
    if (decision == WfsTransferSequence::Decision::InvalidPayload) {
        currentError = Error::ChunkInvalid;
        return false;
    }
    if (decision == WfsTransferSequence::Decision::Duplicate) {
        // ACK a byte-identical retransmission without writing it twice.
        currentError = Error::None;
        return true;
    }
    if (decision == WfsTransferSequence::Decision::OutOfOrder) {
        currentError = Error::WrongSequence;
        return false;
    }
    if (static_cast<uint64_t>(receivedLength) + length > totalLength) {
        currentError = Error::ChunkInvalid;
        return false;
    }
    const size_t written = stagingFile.write(bytes, length);
    if (written != length) {
        fail(Error::WriteFailed);
        return false;
    }
    stagingFile.flush();
    if (!chunkSequence.commit(chunkIndex, bytes, length)) {
        fail(Error::WrongSequence);
        return false;
    }
    receivedLength += static_cast<uint32_t>(written);
    currentError = Error::None;
    return true;
}

bool finish(uint32_t transferId) {
    if (currentState != State::Receiving || transferId != currentTransferId) {
        currentError = Error::WrongTransfer;
        return false;
    }
    if (receivedLength != totalLength) {
        fail(Error::LengthMismatch);
        return false;
    }

    currentState = State::Verifying;
    const uint32_t verifyStartedAtMs = millis();
    stagingFile.flush();
    stagingFile.close();

    fs::File packageFile = FaceStorage::openSlot(stagingSlot, "r");
    PackageValidationError validationError = PackageValidationError::None;
    const bool valid = packageFile &&
        validateWfsPackage(packageFile, totalLength, expectedDigest,
                           expectedDesignId, verifiedFace, validationError);
    if (packageFile) packageFile.close();

    const uint32_t verifyDurationMs = millis() - verifyStartedAtMs;
    const uint32_t receiveDurationMs = verifyStartedAtMs - transferStartedAtMs;
    const MemoryMonitor::Snapshot memoryAfterVerify = SystemDiagnostics::captureMemory();
    const uint32_t minHeapDrop = transferStartMemory.minimumFreeHeap > memoryAfterVerify.minimumFreeHeap
        ? transferStartMemory.minimumFreeHeap - memoryAfterVerify.minimumFreeHeap
        : 0;
    Serial.printf(
        "[PERF] WFS receive=%lu ms verify=%lu ms bytes=%lu heap_start=%lu heap_after=%lu min_heap_drop=%lu\n",
        static_cast<unsigned long>(receiveDurationMs),
        static_cast<unsigned long>(verifyDurationMs),
        static_cast<unsigned long>(totalLength),
        static_cast<unsigned long>(transferStartMemory.freeHeap),
        static_cast<unsigned long>(memoryAfterVerify.freeHeap),
        static_cast<unsigned long>(minHeapDrop));
    SystemDiagnostics::logMemory(valid ? "WFS verified" : "WFS verify failed");

    if (!valid) {
        Serial.printf("[TRANSFER] Package validation failed: %s.\n",
                      packageValidationErrorName(validationError));
        currentError = Error::PackageInvalid;
        currentState = State::Failed;
        return false;
    }

    currentState = State::Verified;
    currentError = Error::None;
    return true;
}

bool apply(uint32_t transferId) {
    if (currentState != State::Verified || transferId != currentTransferId) {
        currentError = Error::WrongTransfer;
        return false;
    }

    currentState = State::Applying;
    const uint32_t applyStartedAtMs = millis();
    const bool activated = FaceStorage::activateSlot(stagingSlot);
    const bool modelInstalled = activated && FaceManager::setActiveModel(verifiedFace);
    const uint32_t applyDurationMs = millis() - applyStartedAtMs;
    if (!modelInstalled) {
        // Restore the exact previous commit state. On a first install that
        // means removing the active pointer, not leaving the new slot committed.
        FaceStorage::restoreActiveSlot(previousActiveSlot);
        Serial.printf("[PERF] WFS apply failed after %lu ms.\n",
                      static_cast<unsigned long>(applyDurationMs));
        SystemDiagnostics::logMemory("WFS apply failed");
        currentState = State::Failed;
        currentError = Error::ApplyFailed;
        return false;
    }

    currentState = State::Complete;
    currentError = Error::None;
    Serial.printf("[TRANSFER] Active face installed: %s (%lu bytes).\n",
                  verifiedFace.name, static_cast<unsigned long>(totalLength));
    Serial.printf("[PERF] WFS apply=%lu ms total=%lu ms.\n",
                  static_cast<unsigned long>(applyDurationMs),
                  static_cast<unsigned long>(millis() - transferStartedAtMs));
    SystemDiagnostics::logMemory("WFS apply complete");
    return true;
}

bool cancel(uint32_t transferId) {
    if (transferId != currentTransferId || !canCancel(currentState)) {
        // A rejected cancel must not overwrite the active transfer's error.
        return false;
    }
    if (stagingFile) {
        stagingFile.close();
    }
    if (stagingSlot <= 1 && !FaceStorage::removeSlot(stagingSlot)) {
        currentState = State::Failed;
        currentError = Error::FileOpenFailed;
        return false;
    }
    currentState = State::Cancelled;
    currentError = Error::None;
    return true;
}

void abortSession() {
    if (!canCancel(currentState)) return;
    const uint32_t transferId = currentTransferId;
    if (!cancel(transferId)) {
        // A disconnect must release the transfer lock even if cleanup failed.
        // Leave the slot inactive; the next begin() retries its removal.
        currentState = State::Failed;
    }
    currentError = Error::SessionLost;
}

Snapshot snapshot() {
    return {currentState, currentError, currentTransferId,
            totalLength, receivedLength, chunkSequence.nextIndex()};
}

bool isBusy() {
    return isBusyState(currentState);
}

const char* stateName(State state) {
    switch (state) {
        case State::Idle: return "IDLE";
        case State::Receiving: return "RECEIVING";
        case State::Verifying: return "VERIFYING";
        case State::Verified: return "VERIFIED";
        case State::Applying: return "APPLYING";
        case State::Complete: return "COMPLETE";
        case State::Failed: return "FAILED";
        case State::Cancelled: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

const char* errorName(Error error) {
    switch (error) {
        case Error::None: return "NONE";
        case Error::Busy: return "BUSY";
        case Error::InvalidRequest: return "INVALID_REQUEST";
        case Error::StorageUnavailable: return "STORAGE_UNAVAILABLE";
        case Error::InsufficientStorage: return "INSUFFICIENT_STORAGE";
        case Error::FileOpenFailed: return "FILE_OPEN_FAILED";
        case Error::WrongTransfer: return "WRONG_TRANSFER";
        case Error::WrongSequence: return "WRONG_SEQUENCE";
        case Error::ChunkInvalid: return "CHUNK_INVALID";
        case Error::WriteFailed: return "WRITE_FAILED";
        case Error::LengthMismatch: return "LENGTH_MISMATCH";
        case Error::PackageInvalid: return "PACKAGE_INVALID";
        case Error::ApplyFailed: return "APPLY_FAILED";
        case Error::SessionLost: return "SESSION_LOST";
        default: return "UNKNOWN";
    }
}

} // namespace TransferManager
