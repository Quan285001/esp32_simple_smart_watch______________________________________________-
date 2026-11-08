#pragma once

class BLEServer;

bool beginWfsGatt(BLEServer* server);
void serviceWfsBle();
void requestWfsSessionReset();
void resetWfsSession();
void shutdownWfsGatt();
