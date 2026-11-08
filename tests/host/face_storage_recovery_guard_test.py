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

# A transfer must not infer slot 0 as inactive when NVS metadata could not load.
assert "FaceStorage::isReady()" in transfer_manager
assert "bool isReady();" in face_storage_header
assert "if (!storageInitialized" in face_storage

print("WFS committed-slot recovery guard tests passed.")
