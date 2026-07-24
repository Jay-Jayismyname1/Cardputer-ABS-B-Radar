#include "aircraft_table.h"
#include "radar_math.h"
#include "airline_lookup.h"
#include <algorithm>

namespace AircraftTable {

namespace {
    Aircraft table[Config::MAX_TRACKED_AIRCRAFT];
    constexpr uint32_t STALE_TIMEOUT_MS = Config::FETCH_INTERVAL_MS * 3; // ~24s
}

void init() {
    for (auto& a : table) a = Aircraft{};
}

Aircraft* raw() { return table; }
uint8_t capacity() { return Config::MAX_TRACKED_AIRCRAFT; }

uint8_t validCount() {
    uint8_t n = 0;
    for (auto& a : table) if (a.valid) n++;
    return n;
}

void postFetchUpdate(double homeLat, double homeLon) {
    uint32_t now = millis();

    for (auto& a : table) {
        if (!a.valid) continue;

        if (now - a.lastSeenMs > STALE_TIMEOUT_MS) {
            a = Aircraft{}; // evict
            continue;
        }

        auto polar = RadarMath::toPolar(homeLat, homeLon, a.lat, a.lon);
        a.distanceKm = polar.distanceKm;
        a.bearingDeg = polar.bearingDeg;

        AirlineLookup::resolve(a);
    }
    std::sort(table, table + Config::MAX_TRACKED_AIRCRAFT,
              [](const Aircraft& a, const Aircraft& b) {
                  if (a.valid != b.valid) return a.valid > b.valid;
                  if (!a.valid) return false;
                  return a.distanceKm < b.distanceKm;
              });
}

}