#include "volume_control.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace VolumeControl {

namespace {
    Preferences prefs;
    uint8_t step = 6; // default ~60%, out of 10 steps
    constexpr uint8_t MAX_STEP = 10;

    void apply() {
        // Speaker_Class::setVolume() takes 0-255. Scale our 0-10 step up.
        uint8_t raw = (step * 255) / MAX_STEP;
        M5Cardputer.Speaker.setVolume(raw);
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    step = prefs.getUChar("volStep", 6);
    if (step > MAX_STEP) step = MAX_STEP;
    apply();
}

void increase() {
    if (step < MAX_STEP) step++;
    prefs.putUChar("volStep", step);
    apply();
}

void decrease() {
    if (step > 0) step--;
    prefs.putUChar("volStep", step);
    apply();
}

uint8_t currentStep() { return step; }

}
