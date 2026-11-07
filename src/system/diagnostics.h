#pragma once

#include <Arduino.h>

namespace SystemDiagnostics {

struct MemorySnapshot {
    uint32_t freeHeap;
    uint32_t minimumFreeHeap;
    uint32_t largestFreeBlock;
};

void begin();
MemorySnapshot captureMemory();
void logMemory(const char* reason);
void service();

} // namespace SystemDiagnostics
