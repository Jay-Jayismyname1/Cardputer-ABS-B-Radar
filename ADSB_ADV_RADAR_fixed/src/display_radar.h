#pragma once
#include <Arduino.h>
#include "aircraft.h"

namespace DisplayRadar {

    void init();
    void render(const Aircraft* aircraftList, uint8_t count,
                float rangeKm, uint8_t selectedIndex,
                bool wifiConnected, int batteryPct,
                const char* locationLabel, int lastHttpCode);

    void tickSweep(uint32_t deltaMs);
    void cycleRange();
    float currentRangeKm();
    float currentSweepAngle();

}