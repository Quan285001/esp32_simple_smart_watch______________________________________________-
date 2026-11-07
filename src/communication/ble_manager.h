#pragma once

class BLEServer;

bool beginWfsGatt(BLEServer* server);
void serviceWfsBle();
void resetWfsSession();
void shutdownWfsGatt();
