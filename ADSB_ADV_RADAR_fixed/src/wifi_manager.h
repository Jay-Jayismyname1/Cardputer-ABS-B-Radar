#pragma once
#include <Arduino.h>

namespace WifiMgr {
    enum class State {
        Idle, Connecting, Connected, Failed, NoCredentials
    };

    // Up to this many WiFi networks can be remembered at once (e.g. home,
    // a friend's place, a phone hotspot). The most recently added/used one
    // is always tried first.
    constexpr uint8_t MAX_NETWORKS = 3;

    void init();

    // Tries only the most-recently-saved network once, and reports Failed
    // if it doesn't connect within the timeout - no fallback to other
    // saved networks. Used right after the user manually picks/enters a
    // network in the WiFi setup screen, so they get direct feedback on
    // that specific attempt.
    void beginConnect();

    // Tries every saved network in priority order (most recent first),
    // automatically moving on to the next one if the current attempt times
    // out, and only reports Failed once all of them have been tried. Used
    // for the silent background/auto-reconnect at boot.
    void beginAutoConnect();

    void update();
    State getState();

    // Adds a network, or - if this SSID is already saved - updates its
    // password and moves it to the front of the list (most-recently-used
    // first). If the list is already full, the least-recently-used entry
    // is dropped to make room.
    void saveCredentials(const char* ssid, const char* password);

    bool hasStoredCredentials(); // true if at least one network is saved
    uint8_t savedNetworkCount();
    String savedNetworkSsid(uint8_t index); // 0 = most recently used
    void forgetNetwork(uint8_t index);
    void forgetAllNetworks();

    bool loadCredentialsFromSd();
    void saveCredentialsToSdIfMounted();

    void beginScan();
    bool isScanComplete();
    int  getScanResultCount();
    String getScanResultSSID(int index);
    int32_t getScanResultRSSI(int index);

    const char* getIP();
}