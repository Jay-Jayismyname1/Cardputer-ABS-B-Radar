#pragma once
#include <Arduino.h>
#include <cstring>
struct Aircraft {
    char     hex[7]      = {0};
    char     callsign[9] = {0};
    char     reg[9]      = {0};
    char     typeCode[5] = {0};
    char     squawk[5]   = {0};   // 4-digit transponder code, e.g. "7700"

    float    lat            = 0;
    float    lon            = 0;
    int32_t  altBaroFt      = 0;    // barometric altitude, ft
    int16_t  vertRateFtMin  = 0;    // vertical speed, ft/min (+climb / -descend)
    float    groundSpeedKt  = 0;
    float    headingDeg     = 0;    // track/heading

    float    distanceKm     = 0;    // computed relative to home, via Haversine
    float    bearingDeg     = 0;    // computed relative to home

    uint32_t lastSeenMs     = 0;    // millis() at last update, for stale-entry eviction
    bool     alerted        = false;// whether proximity beep already fired this pass
    uint32_t alertedAtMs    = 0;

    bool     valid          = false;

    char     airlineName[24] = {0}; // resolved from callsign ICAO prefix
    uint16_t estSeats         = 0;  // rough capacity estimate from typeCode, 0 = unknown

    // True for the three internationally recognized emergency squawk
    // codes: 7500 (hijack), 7600 (radio failure), 7700 (general
    // emergency). Compared as a string rather than parsed as a number,
    // since leading zeros matter (e.g. "0500" must not match "500") and
    // the ADS-B feed already hands it to us as a 4-character code.
    bool isEmergencySquawk() const {
        return valid && squawk[0] != '\0' &&
               (strcmp(squawk, "7500") == 0 ||
                strcmp(squawk, "7600") == 0 ||
                strcmp(squawk, "7700") == 0);
    }
};