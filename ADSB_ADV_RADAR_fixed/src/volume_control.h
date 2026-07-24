#pragma once
#include <Arduino.h>

namespace VolumeControl {

    void init();

    void increase(); // one step up, clamped at max, applies immediately
    void decrease(); // one step down, clamped at 0 (mute), applies immediately

    uint8_t currentStep(); // 0-10, for display purposes

} // namespace VolumeControl
