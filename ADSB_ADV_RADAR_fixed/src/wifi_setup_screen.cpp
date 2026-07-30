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

    // How long the "Connected!"/"Connection failed." message stays on
    // screen before automatically returning to the settings menu. Without
    // this, the screen only advanced past Stage::Done when a keypress
    // happened to trigger the isDone() check in settings_menu.cpp - so it
    // looked "stuck" on Returning... until you mashed a key.
    constexpr uint32_t DONE_DISPLAY_MS = 1500;
    uint32_t doneEnteredMs = 0;

    enum class Stage { Scanning, PickSsid, EnterPassword, Connecting, Done };
    Stage stage = Stage::Scanning;

    // Raised from 10: in a crowded area (very common) the old 10-network
    // cap, combined with no sorting, could silently drop your own network
    // out of the list purely because of scan order - not signal strength.
    constexpr uint8_t MAX_LIST = 24;
    // Only this many rows fit on screen at once - the list scrolls around
    // selectedIndex when there are more results than this.
    constexpr uint8_t VISIBLE_ROWS = 7;
    String ssidList[MAX_LIST];
    int32_t ssidRssi[MAX_LIST];
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

    // Builds ssidList/ssidRssi from the raw scan results: skips blank
    // (hidden) SSIDs, merges duplicate SSIDs (mesh/repeater setups that
    // broadcast the same name from multiple access points) keeping the
    // strongest signal, and - if there are more distinct networks than
    // MAX_LIST - keeps the strongest ones rather than whatever happened to
    // be found first. Finally sorts strongest-first so the networks you're
    // most likely to want are at the top of the (scrollable) list.
    void collectScanResults() {
        ssidCount = 0;
        int rawCount = WifiMgr::getScanResultCount();

        for (int i = 0; i < rawCount; i++) {
            String ssid = WifiMgr::getScanResultSSID(i);
            if (ssid.length() == 0) continue; // hidden/blank SSID
            int32_t rssi = WifiMgr::getScanResultRSSI(i);

            bool merged = false;
            for (int8_t j = 0; j < ssidCount; j++) {
                if (ssidList[j] == ssid) {
                    if (rssi > ssidRssi[j]) ssidRssi[j] = rssi;
                    merged = true;
                    break;
                }
            }
            if (merged) continue;

            if (ssidCount < MAX_LIST) {
                ssidList[ssidCount] = ssid;
                ssidRssi[ssidCount] = rssi;
                ssidCount++;
            } else {
                // List is full - only bump the weakest entry if this new
                // network is stronger, so a crowded area doesn't silently
                // drop a strong nearby network just because it was found
                // late in the scan.
                int8_t weakestIdx = 0;
                for (int8_t j = 1; j < ssidCount; j++) {
                    if (ssidRssi[j] < ssidRssi[weakestIdx]) weakestIdx = j;
                }
                if (rssi > ssidRssi[weakestIdx]) {
                    ssidList[weakestIdx] = ssid;
                    ssidRssi[weakestIdx] = rssi;
                }
            }
        }

        // Simple insertion sort, strongest signal first - ssidCount is
        // small (<= MAX_LIST) so this is plenty fast enough.
        for (int8_t i = 1; i < ssidCount; i++) {
            String keySsid = ssidList[i];
            int32_t keyRssi = ssidRssi[i];
            int8_t j = i - 1;
            while (j >= 0 && ssidRssi[j] < keyRssi) {
                ssidList[j + 1] = ssidList[j];
                ssidRssi[j + 1] = ssidRssi[j];
                j--;
            }
            ssidList[j + 1] = keySsid;
            ssidRssi[j + 1] = keyRssi;
        }
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
        collectScanResults();
        selectedIndex = 0;
        stage = Stage::PickSsid; // empty list still shows a message
    }
    if (stage == Stage::Connecting) {
        WifiMgr::update();
        if (WifiMgr::getState() == WifiMgr::State::Connected) {
            connectSucceeded = true;
            stage = Stage::Done;
            doneEnteredMs = millis();
        } else if (WifiMgr::getState() == WifiMgr::State::Failed) {
            stage = Stage::Done;
            doneEnteredMs = millis();
        }
    }
    if (stage == Stage::Done && millis() - doneEnteredMs >= DONE_DISPLAY_MS) {
        // Auto-advance after the message has had time to be read - no
        // keypress required (previously this only ever flipped `done` the
        // instant we entered Stage::Done, but nothing polled isDone() until
        // the next keystroke, so the screen looked stuck on "Returning...").
        done = true;
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
                int8_t windowStart = 0;
                if (ssidCount > VISIBLE_ROWS) {
                    windowStart = selectedIndex - VISIBLE_ROWS / 2;
                    if (windowStart < 0) windowStart = 0;
                    if (windowStart > ssidCount - VISIBLE_ROWS) windowStart = ssidCount - VISIBLE_ROWS;
                }
                int8_t windowEnd = min((int)ssidCount, (int)(windowStart + VISIBLE_ROWS));

                for (int8_t i = windowStart; i < windowEnd; i++) {
                    if (i == selectedIndex) {
                        d.setTextColor(TFT_BLACK, TFT_GREEN);
                    } else {
                        d.setTextColor(TFT_WHITE, TFT_BLACK);
                    }
                    d.println(ssidList[i]);
                }
                d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
                d.println("");
                if (ssidCount > VISIBLE_ROWS) {
                    d.printf("%d/%d  Fn+;/Fn+. move, Enter select\n",
                             selectedIndex + 1, ssidCount);
                } else {
                    d.println("Fn+; / Fn+. to move, Enter to select");
                }
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