from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "src/main.cpp").read_text()
boot = (ROOT / "src/app/boot_coordinator.cpp").read_text()
header = (ROOT / "src/app/boot_coordinator.h").read_text()

setup_start = main.index("void setup()")
setup = main[setup_start:]
assert "BootCoordinator::restorePersistentState();" in setup
assert "Preferences" not in setup
assert "StorageManager::begin()" not in setup
assert "FaceStorage::begin()" not in setup
assert "restorePersistentState();" in header
for required in (
    "StorageManager::begin()",
    "FaceStorage::begin()",
    "FaceManager::begin()",
    "restoreTimeFromNvs()",
    "loadWeatherCache()",
    "WeatherCoordinator::begin()",
    "setWeatherCoordinates(savedLat, savedLon)",
):
    assert required in boot, required

print("BootCoordinator persistent-state restoration guard tests passed.")
