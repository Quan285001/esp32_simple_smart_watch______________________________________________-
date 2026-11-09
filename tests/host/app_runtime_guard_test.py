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
    "FaceManager::needsFullRender()",
    "cachedTime.tm_sec != lastCustomFaceSecond ||",
):
    assert required in runtime, required

# A failed custom renderer must clear the complete display before the
# centered built-in analog sprite is drawn, otherwise stale custom pixels
# remain visible around the sprite bounds.
fallback_start = runtime.index("FaceManager::disableActive();", runtime.index("Custom renderer rejected"))
fallback_end = runtime.index("lastCustomFaceSecond = cachedTime.tm_sec;", fallback_start)
fallback = runtime[fallback_start:fallback_end]
assert "getDisplayCanvas().clearScreen(0x0000);" in fallback
assert fallback.index("getDisplayCanvas().clearScreen(0x0000);") < fallback.index("drawAnalogClockFace(")
assert '#include "../display/display_canvas.h"' in runtime

print("Application runtime loop ownership and custom-face fallback guard tests passed.")
