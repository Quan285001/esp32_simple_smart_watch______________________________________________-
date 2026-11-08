from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / "src/main.cpp").read_text()
runtime = (ROOT / "src/app/app_runtime.cpp").read_text()
header = (ROOT / "src/app/app_runtime.h").read_text()

assert "void setup()" in main
assert "void loop()" not in main
assert "void loop();" in header
assert "void loop()" in runtime
for required in (
    "BootCoordinator::takeBleInitRequest()",
    "updateButtonManager()",
    "handleBLECommands()",
    "serviceBLE()",
    "WeatherCoordinator::service(",
    "SystemManager::serviceAutoSleep(",
    "drawAnalogClockFace(",
    "drawWeatherFaceData(",
    "micros() - renderStartedUs",
    "renderDurationUs >= 8000UL",
):
    assert required in runtime, required

print("Application runtime loop ownership guard tests passed.")
