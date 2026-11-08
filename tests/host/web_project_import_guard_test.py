#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "web" / "app.js").read_text(encoding="utf-8")
start = APP.index("function importJSON(file)")
end = APP.index("\nfunction openAssetDB()", start)
importer = APP[start:end]

checks = {
    "reject oversized project before FileReader": "file.size>12*1024*1024" in importer and importer.index("file.size>12*1024*1024") < importer.index("new FileReader()"),
    "surface file read errors": "reader.onerror=" in importer,
    "surface user cancellation": "reader.onabort=" in importer,
    "reset file input after import attempt": "finally{resetInput()}" in importer,
    "validate project envelope before writing assets": importer.index("validateJsonSchema(projectSchema,parsed)") < importer.index("await saveAssets(assets)"),
    "validate design and image references before writing assets": importer.index("validateDesign(d)") < importer.index("await saveAssets(assets)"),
    "bound bundled asset payload": "total+a.data.length" in importer and ">8000000" in importer,
}
for label, passed in checks.items():
    print(("PASS" if passed else "FAIL") + ": " + label)
if not all(checks.values()):
    raise SystemExit(1)
