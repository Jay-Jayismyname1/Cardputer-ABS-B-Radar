#include "proximity_alert.h"
#include "config.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace ProximityAlert {

namespace {
    Preferences prefs;
    float thresholdKm = Config::DEFAULT_PROXIMITY_ALERT_KM;
    bool beepEnabled = true;
}

void init() {
    prefs.begin("adsb_radar", false);
    thresholdKm = prefs.getFloat("alertKm", Config::DEFAULT_PROXIMITY_ALERT_KM);
    beepEnabled = prefs.getBool("beepOn", true);
}

void setThresholdKm(float km) {
    thresholdKm = km;
    prefs.putFloat("alertKm", km);
}

float getThresholdKm() { return thresholdKm; }

void setBeepEnabled(bool enabled) {
    beepEnabled = enabled;
    prefs.putBool("beepOn", enabled);
}

bool isBeepEnabled() { return beepEnabled; }

void checkAndAlert(Aircraft* table, uint8_t count) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < count; i++) {
        Aircraft& a = table[i];
        if (!a.valid) continue;

        bool inRange = a.distanceKm <= thresholdKm;
        bool cooldownExpired = (now - a.alertedAtMs) > Config::ALERT_RETRIGGER_COOLDOWN_MS;

        if (inRange && (!a.alerted || cooldownExpired)) {
            if (beepEnabled) M5Cardputer.Speaker.tone(Config::ALERT_TONE_HZ, Config::ALERT_TONE_MS);
            a.alerted = true;
            a.alertedAtMs = now;
        } else if (!inRange) {
            // Reset so it can re-alert if it comes back in range later.
            a.alerted = false;
        }
    }
}

void beepOnce() {
    if (!beepEnabled) return;
    M5Cardputer.Speaker.tone(Config::ALERT_TONE_HZ, Config::ALERT_TONE_MS);
}

}
