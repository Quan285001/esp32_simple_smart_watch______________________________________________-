#include "memory_monitor.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace MemoryMonitor {

Snapshot capture() {
    Snapshot snapshot{};
    snapshot.freeHeap = ESP.getFreeHeap();
    snapshot.minimumFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snapshot.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    // ESP-IDF overrides the vanilla FreeRTOS convention: this API returns bytes,
    // not StackType_t words. Do not multiply by sizeof(StackType_t).
    const UBaseType_t stackHighWaterBytes = uxTaskGetStackHighWaterMark(nullptr);
    snapshot.currentTaskStackHighWaterBytes =
        static_cast<uint32_t>(stackHighWaterBytes);
    return snapshot;
}

} // namespace MemoryMonitor
