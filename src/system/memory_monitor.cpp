#include "memory_monitor.h"

#include <esp_heap_caps.h>

namespace MemoryMonitor {

Snapshot capture() {
    Snapshot snapshot{};
    snapshot.freeHeap = ESP.getFreeHeap();
    snapshot.minimumFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    snapshot.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return snapshot;
}

} // namespace MemoryMonitor
