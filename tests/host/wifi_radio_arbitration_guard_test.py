from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "src/main.cpp").read_text()
ble_handler = (ROOT / "src/services/ble_command_handler.cpp").read_text()

toggle_start = main.index("void toggleWiFi()")
toggle_end = main.index("// =====================================================\n// SETUP", toggle_start)
toggle = main[toggle_start:toggle_end]
assert "BootCoordinator::isBusy()" in toggle
assert "WeatherCoordinator::isFetchRunning()" in toggle
assert "TransferManager::isBusy()" in toggle
assert "WifiService::isScanBusy()" in toggle
assert "WifiService::disable()" in toggle
assert "WifiService::enableSavedCredentials()" in toggle
assert "WiFi.mode(" not in toggle
assert "Preferences" not in toggle

wifi_command_start = ble_handler.index('if (cmd.startsWith("WIFI:"))')
wifi_command_end = ble_handler.index('if (cmd == "SCAN_WIFI")', wifi_command_start)
wifi_command = ble_handler[wifi_command_start:wifi_command_end]
assert "BootCoordinator::isBusy()" in wifi_command
assert "WeatherCoordinator::isFetchRunning()" in wifi_command
assert "TransferManager::isBusy()" in wifi_command
assert "isWifiScanBusy()" in wifi_command

print("WFS Wi-Fi radio arbitration guard tests passed.")

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
command_handler = (ROOT / "src/communication/command_handler.cpp").read_text()

location_start = command_handler.index('if (strcmp(name, "LOCATION_SET") == 0)')
location_end = command_handler.index('if (strcmp(name, "WIFI_SCAN") == 0)', location_start)
location_command = command_handler[location_start:location_end]
assert "BootCoordinator::isBusy()" in location_command
assert "WeatherCoordinator::isFetchRunning()" in location_command
assert "WifiService::isScanBusy()" in location_command
assert location_command.index("network_work_busy") < location_command.index('Preferences prefs;')
assert location_command.index("network_work_busy") < location_command.index("setWeatherCoordinates(")

wifi_start = command_handler.index('if (strcmp(name, "WIFI_CONFIG") == 0)')
wifi_end = command_handler.index('if (strcmp(name, "AUTO_SLEEP_GET") == 0)', wifi_start)
wifi_command = command_handler[wifi_start:wifi_end]
assert "BootCoordinator::isBusy()" in wifi_command
assert "WeatherCoordinator::isFetchRunning()" in wifi_command
assert wifi_command.index("network_work_busy") < wifi_command.index("WifiService::configureCredentials(")

print("WFS command network arbitration guard tests passed.")
