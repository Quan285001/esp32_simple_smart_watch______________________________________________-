from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
boot = (ROOT / "src/app/boot_coordinator.cpp").read_text()
weather = (ROOT / "src/services/weather_coordinator.cpp").read_text()
memory = (ROOT / "src/system/memory_monitor.cpp").read_text()

for source, task_name in (
    (boot, "bootNetwork task exit"),
    (weather, "weatherFetch task exit"),
):
    marker = f'SystemDiagnostics::logMemory("{task_name}");'
    assert marker in source, marker
    task_end = source.index(marker)
    delete = source.index("vTaskDelete(nullptr);", task_end)
    assert task_end < delete
    assert '#include "../system/diagnostics.h"' in source

assert "uxTaskGetStackHighWaterMark(nullptr)" in memory
assert "* sizeof(StackType_t)" not in memory
assert "static_cast<uint32_t>(stackHighWaterBytes)" in memory

print("Worker-task stack and heap diagnostic guard tests passed.")
