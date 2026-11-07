#pragma once

#include <Arduino.h>
#include "memory_monitor.h"

namespace SystemDiagnostics {

void begin();
MemoryMonitor::Snapshot captureMemory();
void logMemory(const char* reason);
void service();

} // namespace SystemDiagnostics
