#include "wifi_setup_screen.h"
#include "wifi_manager.h"
#include <M5Cardputer.h>

namespace WifiSetupScreen {

namespace {
    constexpr uint8_t HID_ENTER = 0x28;
    constexpr uint8_t HID_BACKSPACE = 0x2A;
    // As in settings_menu.cpp: this board's keyboard driver never actually
    // reports Esc via status.hid_keys (0x29) — it only surfaces as '`' in
    // status.word. Relying on HID_ESC alone (as this file previously did)
    // meant Esc/cancel silently did nothing here. '`' is checked below too.
    constexpr uint8_t HID_ESC = 0x29;

    enum class Stage { Scanning, PickSsid, EnterPassword, Connecting, Done };
    Stage stage = Stage::Scanning;

    constexpr uint8_t MAX_LIST = 10;
    String ssidList[MAX_LIST];
    int8_t ssidCount = 0;
    int8_t selectedIndex = 0;

    char passwordBuf[64] = {0};
    uint8_t passwordLen = 0;

    bool done = false;
    bool connectSucceeded = false;

    bool hasHid(const uint8_t* keys, uint8_t count, uint8_t code) {
        for (uint8_t i = 0; i < count; i++) if (keys[i] == code) return true;
        return false;
    }
}

void init() {
    // nothing persistent to load
}

void onEnter() {
    stage = Stage::Scanning;
    done = false;
    connectSucceeded = false;
    ssidCount = 0;
    selectedIndex = 0;
    passwordLen = 0;
    passwordBuf[0] = 0;
    WifiMgr::beginScan();
}

bool isDone() { return done; }

bool didConnectSucceed() { return connectSucceeded; }

void handleWord(const char* chars, uint8_t count, bool fnHeld, bool shiftHeld,
                 const uint8_t* hidKeys, uint8_t hidKeyCount) {

    if (hasHid(hidKeys, hidKeyCount, HID_ESC)) {
        done = true; // cancel, back to radar regardless of stage
        return;
    }

    // '`' is the key that actually fires for physical Esc on this board
    // (see the HID_ESC note above) — but only treat it as cancel outside
    // EnterPassword, since '`' is a legal character in a WiFi password and
    // would otherwise make it untypeable.
    if (stage != Stage::EnterPassword) {
        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == '`') {
                done = true;
                return;
            }
        }
    }

    switch (stage) {
        case Stage::Scanning:
            // No input handled while scanning — see render()/main loop tick
            // for the poll-and-transition logic.
            break;

        case Stage::PickSsid: {
            if (ssidCount == 0) break;

            // Fn+; = up, Fn+. = down (per the documented Cardputer Fn layer)
            if (fnHeld) {
                for (uint8_t i = 0; i < count; i++) {
                    if (chars[i] == ';') {
                        selectedIndex = (selectedIndex - 1 + ssidCount) % ssidCount;
                    } else if (chars[i] == '.') {
                        selectedIndex = (selectedIndex + 1) % ssidCount;
                    }
                }
            }

            if (hasHid(hidKeys, hidKeyCount, HID_ENTER)) {
                passwordLen = 0;
                passwordBuf[0] = 0;
                stage = Stage::EnterPassword;
            }
            break;
        }

        case Stage::EnterPassword: {
            // Plain '`' is skipped as a cancel key above (it's a legal
            // password character), and this board's driver never reports
            // physical Esc via hidKeys either — so on its own there was no
            // way to back out of a wrong SSID once you'd hit Enter on it.
            // Fn+Esc / Fn+` is the escape hatch: fnHeld already means the
            // key isn't appended to the password below, so this is safe to
            // check unconditionally.
            if (fnHeld) {
                bool cancelCombo = hasHid(hidKeys, hidKeyCount, HID_ESC);
                for (uint8_t i = 0; i < count && !cancelCombo; i++) {
                    if (chars[i] == '`') cancelCombo = true;
                }
                if (cancelCombo) {
                    done = true;
                    break;
                }
            }
            if (hasHid(hidKeys, hidKeyCount, HID_BACKSPACE)) {
                if (passwordLen > 0) {
                    passwordLen--;
                    passwordBuf[passwordLen] = 0;
                }
                break;
            }
            if (hasHid(hidKeys, hidKeyCount, HID_ENTER)) {
                WifiMgr::saveCredentials(ssidList[selectedIndex].c_str(), passwordBuf);
                WifiMgr::beginConnect();
                stage = Stage::Connecting;
                break;
            }
            // Append printable characters (skip Fn-layer symbol combos —
            // password entry assumes plain typing).
            if (!fnHeld) {
                for (uint8_t i = 0; i < count && passwordLen < sizeof(passwordBuf) - 1; i++) {
                    char c = chars[i];
                    if (c >= 32 && c < 127) { // printable ASCII only
                        passwordBuf[passwordLen++] = c;
                        passwordBuf[passwordLen] = 0;
                    }
                }
            }
            break;
        }

        case Stage::Connecting:
        case Stage::Done:
            // input ignored — see render() for the state-poll transition
            break;
    }
}

void render() {
    auto& d = M5Cardputer.Display;

    // Poll transitions that don't come from keyboard input.
    if (stage == Stage::Scanning && WifiMgr::isScanComplete()) {
        ssidCount = min((int)WifiMgr::getScanResultCount(), (int)MAX_LIST);
        for (int8_t i = 0; i < ssidCount; i++) ssidList[i] = WifiMgr::getScanResultSSID(i);
        stage = ssidCount > 0 ? Stage::PickSsid : Stage::PickSsid; // empty list still shows a message
    }
    if (stage == Stage::Connecting) {
        WifiMgr::update();
        if (WifiMgr::getState() == WifiMgr::State::Connected) {
            connectSucceeded = true;
            stage = Stage::Done;
            done = true;
        } else if (WifiMgr::getState() == WifiMgr::State::Failed) {
            stage = Stage::Done;
            done = true; // back to radar; main.cpp can decide to retry later
        }
    }

    d.fillScreen(TFT_BLACK);
    d.setTextDatum(top_left);
    d.setTextColor(TFT_GREEN);
    d.setCursor(4, 4);
    d.println("WiFi Setup  (Esc/` to cancel)");
    d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

    d.setCursor(4, 26);
    d.setTextColor(TFT_WHITE);

    switch (stage) {
        case Stage::Scanning:
            d.println("Scanning...");
            break;

        case Stage::PickSsid:
            if (ssidCount == 0) {
                d.println("No networks found.");
                d.println("Esc/` to go back.");
            } else {
                for (int8_t i = 0; i < ssidCount; i++) {
                    if (i == selectedIndex) {
                        d.setTextColor(TFT_BLACK, TFT_GREEN);
                    } else {
                        d.setTextColor(TFT_WHITE, TFT_BLACK);
                    }
                    d.println(ssidList[i]);
                }
                d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
                d.println("");
                d.println("Fn+; / Fn+. to move, Enter to select");
            }
            break;

        case Stage::EnterPassword:
            d.printf("SSID: %s\n\n", ssidList[selectedIndex].c_str());
            d.printf("Password: %s\n", passwordBuf);
            d.setTextColor(TFT_DARKGREEN);
            d.println("\nEnter to connect, Backspace to edit");
            d.println("Fn+Esc/` to cancel");
            break;

        case Stage::Connecting:
            d.println("Connecting...");
            break;

        case Stage::Done:
            d.println(connectSucceeded ? "Connected!" : "Connection failed.");
            d.println("Returning...");
            break;
    }
}

}