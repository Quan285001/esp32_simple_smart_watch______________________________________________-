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
    const uint32_t fragmentationPermille = m.freeHeap > 0 && m.largestFreeBlock <= m.freeHeap
        ? static_cast<uint32_t>((static_cast<uint64_t>(m.freeHeap - m.largestFreeBlock) * 1000ULL) / m.freeHeap)
        : 0;
    Serial.printf(
        "[MEM] %s free=%lu min_free=%lu largest_block=%lu fragmentation=%lu.%01lu%% task_stack_hwm=%lu bytes\n",
        reason != nullptr ? reason : "snapshot",
        static_cast<unsigned long>(m.freeHeap),
        static_cast<unsigned long>(m.minimumFreeHeap),
        static_cast<unsigned long>(m.largestFreeBlock),
        static_cast<unsigned long>(fragmentationPermille / 10),
        static_cast<unsigned long>(fragmentationPermille % 10),
        static_cast<unsigned long>(m.currentTaskStackHighWaterBytes)
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
