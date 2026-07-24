#pragma once
#include <Arduino.h>
#include "aircraft.h"

namespace ProximityAlert {
    void init();
    void setThresholdKm(float km);
    float getThresholdKm();
    void checkAndAlert(Aircraft* table, uint8_t count);
    void beepOnce();
    void setBeepEnabled(bool enabled);
    bool isBeepEnabled();
}
