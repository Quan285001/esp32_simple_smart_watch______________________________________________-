#pragma once

#include <Arduino.h>

namespace MemoryMonitor {

struct Snapshot {
    uint32_t freeHeap;
    uint32_t minimumFreeHeap;
    uint32_t largestFreeBlock;
};

Snapshot capture();

} // namespace MemoryMonitor
