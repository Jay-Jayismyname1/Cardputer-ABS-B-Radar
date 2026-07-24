#include <M5Cardputer.h>
#include <cstring>
#include "config.h"
#include "screen_mode.h"
#include "wifi_manager.h"
#include "adsb_client.h"
#include "aircraft_table.h"
#include "display_radar.h"
#include "proximity_alert.h"
#include "sd_storage.h"
#include "airline_lookup.h"
#include "neopixel_status.h"
#include "volume_control.h"
#include "wifi_setup_screen.h"
#include "settings_menu.h"
#include "location_manager.h"

namespace {
    uint32_t lastFetchMs = 0;
    uint32_t lastFrameMs = 0;
    uint8_t selectedIndex = 0;
    int lastHttpCode = 0;

    bool sdReady = false;
    bool timePrimed = false;

    // --- Keyboard debounce ---
    // The Cardputer's key matrix chatters on contact, especially on combo
    // presses (Aa+letter for capitals), so isChange() can fire more than
    // once for a single physical keystroke and double-type characters.
    // We debounce by requiring the reported key state to actually differ
    // from the last *accepted* state, and by ignoring repeats that land
    // inside a short window after the last accepted event.
    constexpr uint32_t KEY_DEBOUNCE_MS = 40;
    uint32_t lastKeyAcceptMs = 0;
    uint8_t lastWordSig[16] = {0};
    uint8_t lastWordSigLen = 0;
    uint8_t lastHidSig[16] = {0};
    uint8_t lastHidSigLen = 0;
    bool lastFn = false, lastShift = false;

    bool sameSig(const uint8_t* a, uint8_t aLen, const uint8_t* b, uint8_t bLen) {
        if (aLen != bLen) return false;
        for (uint8_t i = 0; i < aLen; i++) if (a[i] != b[i]) return false;
        return true;
    }

    // Returns true if this key state should be processed (i.e. it's a new,
    // debounced event rather than matrix-scan chatter from the same
    // physical keystroke).
    bool acceptKeyEvent(const Keyboard_Class::KeysState& status) {
        uint8_t wordSig[16]; uint8_t wordLen = 0;
        for (auto c : status.word) {
            if (wordLen < sizeof(wordSig)) wordSig[wordLen++] = (uint8_t)c;
        }
        uint8_t hidSig[16]; uint8_t hidLen = 0;
        for (auto k : status.hid_keys) {
            if (hidLen < sizeof(hidSig)) hidSig[hidLen++] = k;
        }

        bool sameAsLast = sameSig(wordSig, wordLen, lastWordSig, lastWordSigLen) &&
                           sameSig(hidSig, hidLen, lastHidSig, lastHidSigLen) &&
                           status.fn == lastFn && status.shift == lastShift;

        uint32_t now = millis();
        if (sameAsLast && (now - lastKeyAcceptMs) < KEY_DEBOUNCE_MS) {
            return false; // chatter — same state repeated too quickly
        }

        memcpy(lastWordSig, wordSig, wordLen); lastWordSigLen = wordLen;
        memcpy(lastHidSig, hidSig, hidLen); lastHidSigLen = hidLen;
        lastFn = status.fn; lastShift = status.shift;
        lastKeyAcceptMs = now;
        return true;
    }

    ScreenMode screenMode = ScreenMode::Radar;

    // Currently unused by the radar HUD (replaced by the green/dark-green
    // status dot in DisplayRadar), kept for debug builds / future reuse.
    [[maybe_unused]] const char* wifiStatusLabel() {
        switch (WifiMgr::getState()) {
            case WifiMgr::State::Connected:      return "WIFI";
            case WifiMgr::State::Connecting:      return "...";
            case WifiMgr::State::Failed:          return "FAIL";
            case WifiMgr::State::NoCredentials:   return "NOCFG";
            default:                               return "IDLE";
        }
    }

    const char* locationStatusLabel() {
        switch (LocationManager::currentSource()) {
            case LocationManager::Source::GpsFix:        return "GPS";
            case LocationManager::Source::IpGeolocation: return "IP";
            case LocationManager::Source::Manual:        return "MANUAL";
            case LocationManager::Source::Persisted:     return "SAVED";
            default:                                      return "NOLOC";
        }
    }

    constexpr uint8_t HID_BACKSPACE = 0x2A;

    void handleKeyboardRadar(const Keyboard_Class::KeysState& status) {
        for (auto k : status.hid_keys) {
            if (k == HID_BACKSPACE) {
                screenMode = ScreenMode::Settings;
                SettingsMenu::onEnter();
                return;
            }
        }
        for (auto c : status.word) {
            if (status.fn) {
                if (c == 'w') {
                    screenMode = ScreenMode::Settings;
                    SettingsMenu::onEnter();
                    return;
                }
                if (c == '=') { VolumeControl::increase(); continue; }
                if (c == '-') { VolumeControl::decrease(); continue; }
            } else if (c == ' ') {
                DisplayRadar::cycleRange();
            }
        }

        if (status.tab) {
            uint8_t count = AircraftTable::validCount();
            if (count > 0) selectedIndex = (selectedIndex + 1) % count;
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);

    DisplayRadar::init();
    AircraftTable::init();
    ProximityAlert::init();
    WifiMgr::init();
    AirlineLookup::init();
    NeopixelStatus::init();
    VolumeControl::init();
    WifiSetupScreen::init();
    SettingsMenu::init();
    LocationManager::init();

    sdReady = SdStorage::init();
    if (sdReady) {
        SdStorage::seedDefaultDataFiles();
    }
    // If SdStorage::init() fails, the radar still runs fine — airline/seat
    // fields just stay blank. Nothing else depends on the SD card being present.

    // "Kinectputer-style" SD credential loading
    if (WifiMgr::loadCredentialsFromSd()) {
        WifiMgr::beginConnect();
    } else if (WifiMgr::hasStoredCredentials()) {
        WifiMgr::beginConnect();
    }

    lastFrameMs = millis();
}

void loop() {
    M5Cardputer.update();

    bool keyChanged = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
    Keyboard_Class::KeysState status;
    if (keyChanged) {
        status = M5Cardputer.Keyboard.keysState();
        keyChanged = acceptKeyEvent(status); // debounce matrix-scan chatter
    }

    if (screenMode == ScreenMode::Radar) {
        if (keyChanged) handleKeyboardRadar(status);
    } else { // Settings
        if (keyChanged) {
            char chars[16];
            uint8_t charCount = 0;
            for (auto c : status.word) {
                if (charCount < sizeof(chars)) chars[charCount++] = c;
            }
            uint8_t hidKeys[16];
            uint8_t hidCount = 0;
            for (auto k : status.hid_keys) {
                if (hidCount < sizeof(hidKeys)) hidKeys[hidCount++] = k;
            }
            SettingsMenu::handleWord(chars, charCount, status.fn, status.shift,
                                       hidKeys, hidCount);
        }
        if (SettingsMenu::isDone()) {
            screenMode = ScreenMode::Radar;
        }
    }

    WifiMgr::update();
    LocationManager::update();

    if (WifiMgr::getState() == WifiMgr::State::Connected && !timePrimed) {
        AdsbClient::primeTime();
        timePrimed = true;
    }
    if (WifiMgr::getState() == WifiMgr::State::Connected) {
        LocationManager::requestIpLookupIfNeeded(); // one-shot, no-ops after first success/attempt
    }

    uint32_t now = millis();

    if (screenMode == ScreenMode::Radar &&
        WifiMgr::getState() == WifiMgr::State::Connected &&
        now - lastFetchMs >= Config::FETCH_INTERVAL_MS) {
        lastFetchMs = now;

        double homeLat = 0.0, homeLon = 0.0;
        // NOTE: previously this did an early `return` here when no location
        // fix was available yet, which skipped the *entire* rest of loop()
        // every frame - including NeopixelStatus::tick()/notifySweepAngle()
        // and DisplayRadar::render(). That's why the LED (and the sweep)
        // could look completely dead before a GPS/IP fix came in. Now we
        // just skip the fetch itself and let the rest of the loop keep running.
        if (LocationManager::currentSource() != LocationManager::Source::None) {
            LocationManager::getHomeLocation(homeLat, homeLon);

            // fetch() is a blocking HTTP call and can take a couple seconds
            // (TLS + request + JSON parse), which otherwise reads as a UI
            // freeze. Flash the LED white for the duration instead of an
            // on-screen "FETCHING..." label, so the radar view stays clean —
            // the sweep/keys are still paused during the call itself.
            NeopixelStatus::flashFetching();

            auto result = AdsbClient::fetch(homeLat, homeLon,
                                             DisplayRadar::currentRangeKm(),
                                             AircraftTable::raw(), AircraftTable::capacity());
            lastHttpCode = result.httpCode;

            if (result.ok) {
                AircraftTable::postFetchUpdate(homeLat, homeLon);
                if (selectedIndex >= AircraftTable::validCount()) selectedIndex = 0;
            }
        }

        // Un-gated from `result.ok`: even with no fix yet, or a failed/empty
        // fetch, this still needs to run so headingBeaconArmed gets set and
        // the idle green heading-beacon flash (LED) has a chance to fire
        // instead of staying permanently unset/dark.
        NeopixelStatus::update(AircraftTable::raw(), AircraftTable::validCount(),
                                AircraftTable::validCount() > 0 ? (int)selectedIndex : -1);
    }

    NeopixelStatus::tick(now);
    if (NeopixelStatus::isFlashRisingEdge()) {
        ProximityAlert::beepOnce();
    }

    if (screenMode == ScreenMode::Radar) {
        uint32_t deltaMs = now - lastFrameMs;
        DisplayRadar::tickSweep(deltaMs);
        NeopixelStatus::notifySweepAngle(DisplayRadar::currentSweepAngle());

        bool wifiConnected = (WifiMgr::getState() == WifiMgr::State::Connected);
        DisplayRadar::render(AircraftTable::raw(), AircraftTable::validCount(),
                              DisplayRadar::currentRangeKm(), selectedIndex,
                              wifiConnected, M5Cardputer.Power.getBatteryLevel(),
                              locationStatusLabel(), lastHttpCode);
    } else {
        SettingsMenu::render();
    }
    lastFrameMs = now;

    delay(16);
}
