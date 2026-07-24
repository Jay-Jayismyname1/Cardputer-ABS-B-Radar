#pragma once
#include "aircraft.h"
#include "config.h"

namespace AircraftTable {

    void init();

    Aircraft* raw(); 
    uint8_t capacity();
    uint8_t validCount();
    void postFetchUpdate(double homeLat, double homeLon);

}