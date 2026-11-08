from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
face_manager = (ROOT / "src/watchface/face_manager.cpp").read_text()
transfer_manager = (ROOT / "src/communication/transfer_manager.cpp").read_text()
face_storage = (ROOT / "src/storage/face_storage.cpp").read_text()
face_storage_header = (ROOT / "src/storage/face_storage.h").read_text()

# Boot restoration must never promote an inactive slot. Only TransferManager,
# after package verification, may commit a newly staged slot.
assert "FaceStorage::activateSlot(" not in face_manager
assert "fallbackSlot" not in face_manager

# No other firmware module may commit a slot pointer. Keep activation
# centralized in the transfer transaction, including its rollback path.
for source in (ROOT / "src").rglob("*.cpp"):
    if source == ROOT / "src/communication/transfer_manager.cpp":
        continue
    assert "FaceStorage::activateSlot(" not in source.read_text(), source

# A transfer must not infer slot 0 as inactive when NVS metadata could not load.
assert "FaceStorage::isReady()" in transfer_manager
assert "bool isReady();" in face_storage_header
assert "if (!storageInitialized" in face_storage
assert "prefs.begin(NVS_NAMESPACE, false)" in face_storage
assert "return storageInitialized;" in face_storage

# The transfer must close the staging writer before reopening the file for
# validation. The state cannot become Verified until validation has succeeded.
finish_start = transfer_manager.index("bool finish(")
apply_start = transfer_manager.index("bool apply(", finish_start)
finish = transfer_manager[finish_start:apply_start]
assert finish.index("stagingFile.flush()") < finish.index("stagingFile.close()")
assert finish.index('FaceStorage::openSlot(stagingSlot, "r")') > finish.index("stagingFile.close()")
assert finish.index("validateWfsPackage(") > finish.index('FaceStorage::openSlot(stagingSlot, "r")')
assert finish.index("currentState = State::Verified") > finish.index("if (!valid)")

# Apply is gated by the verified state, and a failed RAM activation attempts
# to restore the previous NVS commit pointer.
apply_end = transfer_manager.index("bool cancel(", apply_start)
apply = transfer_manager[apply_start:apply_end]
assert "currentState != State::Verified" in apply
assert "FaceStorage::activateSlot(stagingSlot)" in apply
assert "FaceStorage::restoreActiveSlot(previousActiveSlot)" in apply
assert "bool restoreActiveSlot(uint8_t slot);" in face_storage_header
assert "if (slot == INVALID_SLOT)" in face_storage
assert "prefs.remove(ACTIVE_SLOT_KEY)" in face_storage
assert "currentActiveSlot = slot;" in face_storage

# Rollback is the only other path allowed to change the NVS active pointer.
for source in (ROOT / "src").rglob("*.cpp"):
    if source == ROOT / "src/communication/transfer_manager.cpp":
        continue
    assert "FaceStorage::restoreActiveSlot(" not in source.read_text(), source

restore_start = face_storage.index("bool restoreActiveSlot(")
restore_end = face_storage.index("bool activateSlot(", restore_start)
restore = face_storage[restore_start:restore_end]
assert "prefs.isKey(ACTIVE_SLOT_KEY)" in restore
assert "prefs.remove(ACTIVE_SLOT_KEY)" in restore
assert "prefs.getUChar(ACTIVE_SLOT_KEY, INVALID_SLOT) == slot" in restore
assert "if (restored)" in restore
assert "markStorageUnavailable();" in restore
activate_start = face_storage.index("bool activateSlot(")
activate = face_storage[activate_start:]
assert "markStorageUnavailable();" in activate
unavailable_start = face_storage.index("void markStorageUnavailable()")
unavailable_end = face_storage.index("}", unavailable_start)
unavailable = face_storage[unavailable_start:unavailable_end]
assert "currentActiveSlot = INVALID_SLOT;" in unavailable
assert "storageInitialized = false;" in unavailable
assert "if (!prefs.begin(NVS_NAMESPACE, false))" in restore

print("WFS committed-slot recovery and transfer transaction guard tests passed.")
