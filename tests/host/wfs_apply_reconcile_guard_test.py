#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "web" / "app.js").read_text(encoding="utf-8")

assert "async function refreshWfsFaceStatus()" in APP
assert "sendWfsFrameAndWait(0x07,new Uint8Array(),0x08,8000)" in APP
assert "WFS response timeout: 0x27" in APP
assert "faceStatus?.faceId!==pack.designId" in APP
assert "applied={ok:true,designId:faceStatus.faceId}" in APP

print("WFS lost-apply-acknowledgement reconciliation guards passed.")
