#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "web" / "app.js").read_text(encoding="utf-8")
start = APP.index("function addImage(file)")
end = APP.index("\nasync function sendLegacyCommand", start)
importer = APP[start:end]

checks = {
    "reject non-image files": "file.type.startsWith('image/')" in importer,
    "bound source image size before reading": "file.size>5*1024*1024" in importer and importer.index("file.size>5*1024*1024") < importer.index("new FileReader()"),
    "handle image read errors": "reader.onerror=" in importer,
    "handle image read cancellation": "reader.onabort=" in importer,
    "validate encoded image data URL": "reader.result.startsWith('data:image/')" in importer,
    "bound encoded asset size": "reader.result.length>8000000" in importer,
    "bound portable project image count": "usedIds.size>=16" in importer,
    "bound aggregate portable asset data": "existingBytes+reader.result.length>8000000" in importer,
    "reset file input after completion": "finally{resetInput()}" in importer,
    "reset file input when read cannot start": "catch(e){toast('Could not start image import: '+e.message);resetInput()}" in importer,
}
for label, passed in checks.items():
    print(("PASS" if passed else "FAIL") + ": " + label)
if not all(checks.values()):
    raise SystemExit(1)
