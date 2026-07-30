#pragma once
#include <Arduino.h>
#include "aircraft.h"
#include "config.h"

namespace AdsbClient {

    struct FetchResult {
        bool     ok = false;
        uint16_t aircraftCount = 0;
        int      httpCode = 0;
    };

    // Original blocking fetch - now only called internally by the
    // background task below. Calling this directly from loop() is what
    // caused the ~1-3s UI stutter every FETCH_INTERVAL_MS (TLS handshake +
    // HTTP GET + JSON parse all block whichever core calls it).
    FetchResult fetch(double homeLat, double homeLon, float radiusKm,
                       Aircraft* table, uint8_t tableCapacity);

    void primeTime();

    // --- Background fetch API --------------------------------------------
    // Runs the network fetch on a FreeRTOS task pinned to core 0, so the
    // main loop (core 1: sweep animation, keypad polling, display) never
    // blocks on it. Call startBackgroundTask() once from setup().
    void startBackgroundTask();

    // Kicks off a new fetch if one isn't already running. Returns false
    // (and does nothing) if the previous fetch hasn't finished yet - the
    // caller should just try again on the next interval tick.
    bool requestFetch(double homeLat, double homeLon, float radiusKm);

    // True once a background fetch has finished and its data is ready to
    // be picked up with consumeResult().
    bool resultReady();

    // Copies the finished fetch's aircraft data into outTable (capacity
    // outCapacity entries) and returns its FetchResult. Only call this
    // after resultReady() returned true; clears the ready flag as a side
    // effect, so call it exactly once per completed fetch.
    FetchResult consumeResult(Aircraft* outTable, uint8_t outCapacity);

}