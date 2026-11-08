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
