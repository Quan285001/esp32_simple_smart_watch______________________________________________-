#pragma once

namespace BootCoordinator {

// Starts the ordered Wi-Fi, NTP, weather and lunar startup task.
// Returns false if the FreeRTOS task could not be created; BLE initialization
// is still scheduled so startup can complete in offline mode.
// Restores storage, location, clock, power settings and cached weather.
void restorePersistentState();

bool begin();

// True while the network task is running or BLE initialization is pending.
bool isBusy();

// Consumes the one-shot request for BLE initialization from the main loop.
bool takeBleInitRequest();

} // namespace BootCoordinator
