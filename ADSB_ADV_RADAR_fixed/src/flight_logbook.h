#pragma once
#include "aircraft.h"

namespace FlightLogbook {

    // Loads the on/off preference (see Settings menu -> "Flight Logbook").
    // Call once from setup(), like the other modules' init().
    void init();

    bool isEnabled();
    void setEnabled(bool enabled);

    // Call once per successful fetch, right after
    // AircraftTable::postFetchUpdate() - if enabled, compares the current
    // table against aircraft already logged today and appends a new
    // logbook line to the SD card for each aircraft seen for the first
    // time today. Does nothing at all if disabled in Settings.
    void update(const Aircraft* table, uint8_t count);

}