#include "units.h"
#include <Preferences.h>

namespace Units {

namespace {
    Preferences prefs;
    Distance unit = Distance::Km;
}

void init() {
    prefs.begin("adsb_radar", false);
    unit = static_cast<Distance>(prefs.getUChar("distUnit", static_cast<uint8_t>(Distance::Km)));
}

Distance current() {
    return unit;
}

void toggle() {
    unit = (unit == Distance::Km) ? Distance::NauticalMiles : Distance::Km;
    prefs.putUChar("distUnit", static_cast<uint8_t>(unit));
}

void formatDistance(float km, char* buf, size_t bufSize) {
    if (unit == Distance::NauticalMiles) {
        snprintf(buf, bufSize, "%.0fnm", km / KM_PER_NM);
    } else {
        snprintf(buf, bufSize, "%.0fkm", km);
    }
}

const char* suffix() {
    return unit == Distance::NauticalMiles ? "nm" : "km";
}

}