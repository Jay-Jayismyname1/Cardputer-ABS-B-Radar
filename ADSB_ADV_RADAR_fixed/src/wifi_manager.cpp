#include "wifi_manager.h"
#include "config.h"
#include "sd_storage.h"
#include <WiFi.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_wifi.h>

namespace WifiMgr {

namespace {
    Preferences prefs;
    State state = State::Idle;
    uint32_t connectStartMs = 0;
    constexpr uint32_t CONNECT_TIMEOUT_MS = 15000; // bounded, same fix as the Kinect sketch
    char ipStr[16] = {0};

    uint8_t connectAttemptIndex = 0;
    bool cascadeOnFailure = false; // true only during beginAutoConnect()

    String slotKeySsid(uint8_t i) { return "ssid" + String(i); }
    String slotKeyPass(uint8_t i) { return "pass" + String(i); }

    // Reads every currently-saved network into the given arrays (index 0 =
    // most recently used) and returns how many were found. The arrays must
    // have at least MAX_NETWORKS elements.
    uint8_t readAllSlots(String outSsid[], String outPass[]) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MAX_NETWORKS; i++) {
            String ssid = prefs.getString(slotKeySsid(i).c_str(), "");
            if (ssid.length() == 0) break; // list is always kept compact, no gaps
            outSsid[n] = ssid;
            outPass[n] = prefs.getString(slotKeyPass(i).c_str(), "");
            n++;
        }
        return n;
    }

    // Overwrites the stored list with exactly these networks, in this
    // order, clearing any leftover slots beyond `count`.
    void writeAllSlots(const String ssid[], const String pass[], uint8_t count) {
        for (uint8_t i = 0; i < MAX_NETWORKS; i++) {
            if (i < count) {
                prefs.putString(slotKeySsid(i).c_str(), ssid[i]);
                prefs.putString(slotKeyPass(i).c_str(), pass[i]);
            } else {
                prefs.remove(slotKeySsid(i).c_str());
                prefs.remove(slotKeyPass(i).c_str());
            }
        }
    }

    void tryConnectToSlot(uint8_t index) {
        String ssid = prefs.getString(slotKeySsid(index).c_str(), "");
        if (ssid.length() == 0) {
            state = State::Failed;
            return;
        }
        String pass = prefs.getString(slotKeyPass(index).c_str(), "");

        WiFi.scanDelete();           // release any leftover scan state before connecting
        WiFi.disconnect(true, true); // clean reset before (re)connecting
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        connectStartMs = millis();
        state = State::Connecting;
    }
}

void init() {
    prefs.begin("adsb_radar", /*readOnly=*/false);

    // One-time migration from the old single-network storage (before
    // multi-network support existed) to the new indexed slots, so nobody
    // updating this firmware loses their already-saved network.
    if (!hasStoredCredentials()) {
        String oldSsid = prefs.getString("ssid", "");
        if (oldSsid.length() > 0) {
            String oldPass = prefs.getString("pass", "");
            saveCredentials(oldSsid.c_str(), oldPass.c_str());
            prefs.remove("ssid");
            prefs.remove("pass");
        }
    }

    state = hasStoredCredentials() ? State::Idle : State::NoCredentials;

    // Some routers put certain networks (guest networks especially) on
    // channel 12 or 13. ESP32's default regulatory/country setting can
    // scan those channels too conservatively and miss the beacon - other
    // ESP32 tools (Bruce, etc.) explicitly widen this, which is likely why
    // they can see networks this app couldn't. Explicitly allow the full
    // EU channel range (1-13) before any scanning happens.
    WiFi.mode(WIFI_STA); // must init the WiFi driver before touching country config
    wifi_country_t country = {};
    strncpy(country.cc, "DE", sizeof(country.cc));
    country.schan = 1;
    country.nchan = 13;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);
}

bool hasStoredCredentials() {
    String ssid = prefs.getString(slotKeySsid(0).c_str(), "");
    return ssid.length() > 0;
}

uint8_t savedNetworkCount() {
    String s[MAX_NETWORKS], p[MAX_NETWORKS];
    return readAllSlots(s, p);
}

String savedNetworkSsid(uint8_t index) {
    if (index >= MAX_NETWORKS) return "";
    return prefs.getString(slotKeySsid(index).c_str(), "");
}

void saveCredentials(const char* ssid, const char* password) {
    // Insert-or-move-to-front (most-recently-used first): the network you
    // just picked/entered is the one you want to connect to right now, so
    // it should be tried first - both immediately (beginConnect()) and on
    // every future beginAutoConnect() cycle. If it's already saved
    // elsewhere in the list, the old copy is dropped so it isn't
    // duplicated; if the list is already full, the least-recently-used
    // entry (the last one) is dropped to make room.
    String existingSsid[MAX_NETWORKS], existingPass[MAX_NETWORKS];
    uint8_t existingN = readAllSlots(existingSsid, existingPass);

    String newSsid(ssid);
    String outSsid[MAX_NETWORKS], outPass[MAX_NETWORKS];
    uint8_t outN = 0;
    outSsid[outN] = newSsid;
    outPass[outN] = String(password);
    outN++;

    for (uint8_t i = 0; i < existingN && outN < MAX_NETWORKS; i++) {
        if (existingSsid[i] == newSsid) continue; // drop the old copy of this network
        outSsid[outN] = existingSsid[i];
        outPass[outN] = existingPass[i];
        outN++;
    }

    writeAllSlots(outSsid, outPass, outN);
}

void forgetNetwork(uint8_t index) {
    String s[MAX_NETWORKS], p[MAX_NETWORKS];
    uint8_t n = readAllSlots(s, p);
    if (index >= n) return;

    // Shift everything after `index` down by one, then write the
    // shortened list - keeps the stored list compact (no gaps).
    for (uint8_t i = index; i < n - 1; i++) {
        s[i] = s[i + 1];
        p[i] = p[i + 1];
    }
    writeAllSlots(s, p, n - 1);
}

void forgetAllNetworks() {
    String empty[MAX_NETWORKS];
    writeAllSlots(empty, empty, 0);
}

void beginConnect() {
    if (!hasStoredCredentials()) {
        state = State::NoCredentials;
        return;
    }
    cascadeOnFailure = false;
    connectAttemptIndex = 0;
    tryConnectToSlot(0); // just the most-recently-saved network - fail fast, no fallback
}

void beginAutoConnect() {
    if (!hasStoredCredentials()) {
        state = State::NoCredentials;
        return;
    }
    cascadeOnFailure = true;
    connectAttemptIndex = 0;
    tryConnectToSlot(0);
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

        if (cascadeOnFailure) {
            connectAttemptIndex++;
            if (connectAttemptIndex < savedNetworkCount()) {
                tryConnectToSlot(connectAttemptIndex); // try the next saved network
                return;
            }
        }
        state = State::Failed; // out of networks to try (or not cascading at all)
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

    // Format: one network per two lines (SSID, then password), up to
    // MAX_NETWORKS networks - the first pair is the highest priority
    // (tried first). Fully backward compatible with the old single-network
    // wifi.txt, which was just SSID+password on the first two lines.
    String ssids[MAX_NETWORKS], passes[MAX_NETWORKS];
    uint8_t n = 0;
    while (n < MAX_NETWORKS && f.available()) {
        String ssid = f.readStringUntil('\n');
        String pass = f.readStringUntil('\n');
        ssid.trim();
        pass.trim();
        if (ssid.length() == 0) break;
        ssids[n] = ssid;
        passes[n] = pass;
        n++;
    }
    f.close();

    if (n == 0) return false;
    writeAllSlots(ssids, passes, n);
    return true;
}

void saveCredentialsToSdIfMounted() {
    if (!SdStorage::isMounted()) return;

    String s[MAX_NETWORKS], p[MAX_NETWORKS];
    uint8_t n = readAllSlots(s, p);
    if (n == 0) return;

    File f = SD.open(Config::SD_WIFI_CREDENTIALS_FILE, FILE_WRITE);
    if (!f) return;
    for (uint8_t i = 0; i < n; i++) {
        f.println(s[i]);
        f.println(p[i]);
    }
    f.close();
}

}