#include "diagnostics.h"

#include "memory_monitor.h"

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

MemoryMonitor::Snapshot captureMemory() {
    return MemoryMonitor::capture();
}

void logMemory(const char* reason) {
    const MemoryMonitor::Snapshot m = captureMemory();
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
