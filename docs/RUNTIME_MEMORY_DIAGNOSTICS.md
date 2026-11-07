# ESP32-C3 Runtime Memory Diagnostics

This document describes the diagnostic logs added for measuring real runtime heap use. It does not claim that the current firmware has a memory leak or that a particular amount of RAM is available for Face VM.

## Metrics

- `free_heap`: currently free heap reported by the Arduino/ESP-IDF heap API.
- `min_free_heap`: lowest free heap observed since boot. This is a historical low-water mark.
- `largest_block`: largest contiguous allocatable 8-bit-capable heap block. This can be much smaller than total free heap when memory is fragmented.
- `internal_free`: free internal 8-bit-capable heap; useful when evaluating allocations that must reside in internal RAM.

The build-time RAM percentage and runtime free heap are different measurements. Static/global allocations are already reserved before the application begins normal operation. Runtime components such as BLE, Wi-Fi, TLS/HTTP, JSON parsing, display buffers and tasks may allocate more memory later.

## Log checkpoints

The firmware emits snapshots at important lifecycle boundaries:

- `[MEM] BOOT`
- `[MEM] AFTER_BLE_INIT`
- `[MEM] BLE_CONNECTED` and `BLE_DISCONNECTED`
- `[MEM] WIFI_SCAN_START` and `WIFI_SCAN_SERIALIZED`
- `[MEM] WEATHER_TASK_START`, `WEATHER_FETCH_OK/FAILED`, `WEATHER_TASK_END`
- `[MEM] AFTER_LIGHT_SLEEP_WAKE`
- `[LOOP] Alive` heartbeat includes current free heap, minimum free heap and largest block.

## How to collect a useful test

1. Reset the board and capture the complete boot log.
2. Leave the clock idle for several minutes.
3. Connect the Web Dashboard over BLE and capture the connection snapshot.
4. Trigger a weather update and capture the weather task snapshots.
5. If possible, enable Wi-Fi and run a Wi-Fi scan.
6. Disconnect BLE and observe whether current free heap returns near its previous baseline.
7. Repeat the same sequence several times.

Compare both current free heap and largest block. If current free heap drops after every repeated cycle and never recovers, investigate retained allocations or leaks. If total free heap remains healthy but largest block shrinks, investigate fragmentation and large contiguous allocations. A one-time drop after initializing BLE or Wi-Fi may be normal because those subsystems retain buffers.

The minimum-free metric never rises after boot; it records the historical low point. It is not a measure of current free memory.

## Face VM budgeting rule

Do not reserve a fixed percentage from the build summary. First measure the worst-case runtime baseline with the current firmware, including BLE and weather activity. Then reserve headroom for system tasks and transient allocations. Only the remaining measured budget can be considered for VM stack, locals, renderer buffers and face assets.

A full-screen 240x240 RGB565 image is 115,200 bytes uncompressed. Prefer streaming/tile rendering from Flash over allocating multiple full-screen buffers in RAM.

## Notes

These logs are diagnostic instrumentation. They do not prove the absence of leaks, fragmentation, stack overflow or out-of-memory failures. A later profiling pass may add task stack high-water marks and allocation tracing if the current measurements indicate a need.
