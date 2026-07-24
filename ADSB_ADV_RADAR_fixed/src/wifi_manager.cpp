#include "wifi_manager.h"
#include "config.h"
#include "sd_storage.h"
#include <WiFi.h>
#include <Preferences.h>
#include <SD.h>

namespace WifiMgr {

namespace {
    Preferences prefs;
    State state = State::Idle;
    uint32_t connectStartMs = 0;
    constexpr uint32_t CONNECT_TIMEOUT_MS = 15000; // bounded, same fix as the Kinect sketch
    char ipStr[16] = {0};
}

void init() {
    prefs.begin("adsb_radar", /*readOnly=*/false);
    state = hasStoredCredentials() ? State::Idle : State::NoCredentials;
}

bool hasStoredCredentials() {
    String ssid = prefs.getString("ssid", "");
    return ssid.length() > 0;
}

void saveCredentials(const char* ssid, const char* password) {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
}

void beginConnect() {
    if (!hasStoredCredentials()) {
        state = State::NoCredentials;
        return;
    }
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    connectStartMs = millis();
    state = State::Connecting;
}

void update() {
    if (state != State::Connecting) return;

    if (WiFi.status() == WL_CONNECTED) {
        strncpy(ipStr, WiFi.localIP().toString().c_str(), sizeof(ipStr) - 1);
        state = State::Connected;
        return;
    }

    if (millis() - connectStartMs > CONNECT_TIMEOUT_MS) {
        WiFi.disconnect(true);
        state = State::Failed;
    }
}

State getState() { return state; }

void beginScan() {
    WiFi.scanNetworks(/*async=*/true);
}

bool isScanComplete() {
    return WiFi.scanComplete() >= 0;
}

int getScanResultCount() {
    int n = WiFi.scanComplete();
    return n > 0 ? n : 0;
}

String getScanResultSSID(int index) {
    return WiFi.SSID(index);
}

int32_t getScanResultRSSI(int index) {
    return WiFi.RSSI(index);
}

const char* getIP() { return ipStr; }

bool loadCredentialsFromSd() {
    if (!SdStorage::isMounted()) return false;
    if (!SD.exists(Config::SD_WIFI_CREDENTIALS_FILE)) return false;

    File f = SD.open(Config::SD_WIFI_CREDENTIALS_FILE, FILE_READ);
    if (!f) return false;

    String ssid = f.readStringUntil('\n');
    String pass = f.readStringUntil('\n');
    f.close();

    ssid.trim();
    pass.trim();
    if (ssid.length() == 0) return false;

    saveCredentials(ssid.c_str(), pass.c_str());
    return true;
}

void saveCredentialsToSdIfMounted() {
    if (!SdStorage::isMounted()) return;

    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    if (ssid.length() == 0) return;

    File f = SD.open(Config::SD_WIFI_CREDENTIALS_FILE, FILE_WRITE);
    if (!f) return;
    f.println(ssid);
    f.println(pass);
    f.close();
}

}
