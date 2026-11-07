#include "diagnostics.h"

#include <esp_heap_caps.h>

namespace SystemDiagnostics {
namespace {
constexpr uint32_t REPORT_INTERVAL_MS = 15000UL;
uint32_t lastReportMs = 0;
bool initialized = false;
}

void begin() {
    lastReportMs = millis();
    initialized = true;
    logMemory("boot");
}

MemorySnapshot captureMemory() {
    MemorySnapshot snapshot{};
    snapshot.freeHeap = ESP.getFreeHeap();
    snapshot.minimumFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snapshot.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return snapshot;
}

void logMemory(const char* reason) {
    const MemorySnapshot m = captureMemory();
    Serial.printf(
        "[MEM] %s free=%lu min_free=%lu largest_block=%lu bytes\n",
        reason != nullptr ? reason : "snapshot",
        static_cast<unsigned long>(m.freeHeap),
        static_cast<unsigned long>(m.minimumFreeHeap),
        static_cast<unsigned long>(m.largestFreeBlock)
    );
}

void service() {
    if (!initialized) return;
    const uint32_t now = millis();
    if (now - lastReportMs < REPORT_INTERVAL_MS) return;
    lastReportMs = now;
    logMemory("periodic");
}

} // namespace SystemDiagnostics
