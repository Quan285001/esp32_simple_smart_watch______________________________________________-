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

exit_log = boot.index('SystemDiagnostics::logMemory("bootNetwork task exit");')
ble_pending = boot.index("setState(State::BlePending);")
task_delete = boot.index("vTaskDelete(nullptr);", ble_pending)
assert exit_log < ble_pending < task_delete

take_start = boot.index("bool takeBleInitRequest()")
take = boot[take_start:]
critical_exit = take.index("portEXIT_CRITICAL(&stateMux);")
scheduler_yield = take.index("vTaskDelay(1);")
return_pending = take.index("return pending;")
assert critical_exit < scheduler_yield < return_pending

print("BootCoordinator persistent-state restoration and worker handoff guard tests passed.")
