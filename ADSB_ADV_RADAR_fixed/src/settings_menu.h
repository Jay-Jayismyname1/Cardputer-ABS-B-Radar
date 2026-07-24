#pragma once
#include <Arduino.h>

namespace SettingsMenu {

    void init();
    void onEnter(); // call when opening from the radar screen

    bool isDone(); // true once the menu wants control back on the radar screen

    void handleWord(const char* chars, uint8_t count, bool fnHeld, bool shiftHeld,
                     const uint8_t* hidKeys, uint8_t hidKeyCount);

    void render();

}