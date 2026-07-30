#pragma once
#include <Arduino.h>

namespace Units {

    enum class Distance : uint8_t { Km = 0, NauticalMiles = 1 };

    // 1 nautische Meile = 1.852 km (internationale Definition)
    constexpr float KM_PER_NM = 1.852f;

    void init();
    Distance current();
    void toggle();

    // Formatiert eine in km übergebene Distanz passend zur aktuell
    // gewählten Einheit, z.B. "42km" oder "23nm".
    void formatDistance(float km, char* buf, size_t bufSize);

    // Nur das Einheiten-Kürzel ("km" / "nm"), z.B. für eigene Labels.
    const char* suffix();
}