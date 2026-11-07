#pragma once

#include <Arduino.h>

namespace CommandHandler {

struct Result {
    bool ok;
    uint16_t errorCode;
    int32_t value;
    bool requestSleep;
    char requestId[33];
    char message[64];
};

bool dispatch(const uint8_t* payload, size_t length, Result& result);
void executeDeferred(const Result& result);

} // namespace CommandHandler
