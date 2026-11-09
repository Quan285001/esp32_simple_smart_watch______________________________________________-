from html.parser import HTMLParser
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / "index.html"

class AssetParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.assets = []
        self.ids = set()
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if tag == "script" and attrs.get("src"):
            self.assets.append(attrs["src"])
        if tag == "link" and attrs.get("rel") == "stylesheet" and attrs.get("href"):
            self.assets.append(attrs["href"])

parser = AssetParser()
parser.feed(INDEX.read_text(encoding="utf-8"))
errors = []
for asset in parser.assets:
    if asset.startswith(("http://", "https://")):
        continue
    if not (ROOT / asset).is_file():
        errors.append(f"Missing frontend asset: {asset}")
required_ids = {"watchCanvas", "quickAddSelect", "quickAddBtn", "objectList", "inspector", "templateGrid", "libraryGrid", "connectBtn", "saveBtn", "exportBtn", "importBtn", "imageInput", "jsonInput", "designName", "bgColor", "toast", "legacyStatus", "syncTimeBtn", "syncWeatherBtn", "sleepWatchBtn", "sendLocationBtn", "scanWifiBtn", "saveWifiBtn", "saveAutoSleepBtn", "settingsTopBtn", "undoBtn", "redoBtn", "themeLight", "themeDark", "themeSystem", "languageSelect", "autoSaveToggle", "deviceProfileDialog", "profilePresetSelect", "profileName", "profileWidth", "profileHeight", "profileShape", "profileRotation", "profileColorFormat", "profileScaleStrategy", "profileCompatibilityStatus", "profileExportBtn", "profileImportBtn", "profileImportInput"}
missing = sorted(required_ids - parser.ids)
if missing:
    errors.append("Missing required UI ids: " + ", ".join(missing))
if not (ROOT / "shared/schemas/watch-face.schema.json").is_file():
    errors.append("Shared watch-face schema is missing")

app_js = (ROOT / "web/app.js").read_text(encoding="utf-8")
font_match = re.search(r"const TFT_FONT1_GLYPHS='([0-9a-f]+)'", app_js)
if not font_match or len(font_match.group(1)) != 2560:
    errors.append("Firmware GLCD font table must contain all 256 five-byte glyphs")
if "new TextEncoder().encode(value)" not in app_js or "TFT_FONT1_GLYPHS.slice(offset+column*2" not in app_js:
    errors.append("Web text preview must use the firmware's byte-based GLCD glyph lookup")
if "ctx.imageSmoothingEnabled=false" not in app_js:
    errors.append("Web text preview must keep bitmap glyphs pixel-aligned")
if "roundFirmwarePixel(boxSize?.width??240)" not in app_js or "bitmap.width=width;bitmap.height=height" not in app_js:
    errors.append("Web text preview must rasterize on the same integer pixel grid as the device")
if "setPreviewText(el,objectText(o),s,t)" not in app_js:
    errors.append("Text preview must use the firmware-rounded object dimensions")
if "renderWeatherPreview(el,o,s,t)" not in app_js:
    errors.append("Weather preview must use the firmware object geometry")

if "function renderCanvas()" not in app_js or "state.design.canvas.width" not in app_js or "state.design.canvas.height" not in app_js:
    errors.append("Canvas rendering must use the active Device Profile dimensions")
if "roundFirmwarePixel(t.x)/width*100" not in app_js or "roundFirmwarePixel(t.y)/height*100" not in app_js:
    errors.append("Canvas object placement must be normalized against profile width and height")
if "logicalWidth/Math.max(1,canvas.clientWidth)" not in app_js or "logicalHeight/Math.max(1,canvas.clientHeight)" not in app_js:
    errors.append("Pointer dragging must convert screen coordinates using the active canvas dimensions")
if "$('#deviceSettings').onclick=openDeviceProfileDialog" not in app_js:
    errors.append("The Device Profile control must open the profile editor, not show a device identity toast")
if "function applyDeviceProfile()" not in app_js or "WFSDeviceProfile.migrateObjects" not in app_js:
    errors.append("Applying a profile must run the selected coordinate migration strategy")
if "device-profile.js" not in parser.assets:
    errors.append("The shared Device Profile editor module must load before the application")

if errors:
    print("\n".join("ERROR: " + e for e in errors))
    sys.exit(1)
print(f"Web smoke check passed: {len(parser.ids)} UI ids and {len(parser.assets)} referenced assets.")
