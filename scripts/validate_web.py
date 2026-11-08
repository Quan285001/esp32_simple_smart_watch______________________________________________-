from html.parser import HTMLParser
from pathlib import Path
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
required_ids = {"watchCanvas", "quickAddSelect", "quickAddBtn", "objectList", "inspector", "templateGrid", "libraryGrid", "connectBtn", "saveBtn", "exportBtn", "importBtn", "imageInput", "jsonInput", "designName", "bgColor", "toast", "legacyStatus", "syncTimeBtn", "syncWeatherBtn", "sleepWatchBtn", "sendLocationBtn", "scanWifiBtn", "saveWifiBtn", "saveAutoSleepBtn", "settingsTopBtn", "undoBtn", "redoBtn", "themeLight", "themeDark", "themeSystem", "languageSelect", "autoSaveToggle"}
missing = sorted(required_ids - parser.ids)
if missing:
    errors.append("Missing required UI ids: " + ", ".join(missing))
if not (ROOT / "shared/schemas/watch-face.schema.json").is_file():
    errors.append("Shared watch-face schema is missing")
if errors:
    print("\n".join("ERROR: " + e for e in errors))
    sys.exit(1)
print(f"Web smoke check passed: {len(parser.ids)} UI ids and {len(parser.assets)} referenced assets.")
