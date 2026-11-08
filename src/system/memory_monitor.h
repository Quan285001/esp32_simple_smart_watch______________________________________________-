#pragma once

#include <Arduino.h>

namespace MemoryMonitor {

struct Snapshot {
    uint32_t freeHeap;
    uint32_t minimumFreeHeap;
    uint32_t largestFreeBlock;
    uint32_t currentTaskStackHighWaterBytes;
};

Snapshot capture();

} // namespace MemoryMonitor
