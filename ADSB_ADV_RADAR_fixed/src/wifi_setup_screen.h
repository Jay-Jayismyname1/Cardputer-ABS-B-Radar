#pragma once
#include <Arduino.h>

namespace WifiSetupScreen {
    void init();
    void onEnter();
    bool isDone();
    bool didConnectSucceed();

    void handleWord(const char* chars, uint8_t count, bool fnHeld, bool shiftHeld,
                     const uint8_t* hidKeys, uint8_t hidKeyCount);
    void render();
}
