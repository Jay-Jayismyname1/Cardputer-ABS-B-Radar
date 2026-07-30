#include "adsb_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <string.h>

namespace AdsbClient {

namespace {
    const char* kNoValidate = nullptr;

    // Reused across fetches instead of being constructed fresh each call.
    // A fresh WiFiClientSecure/HTTPClient pair means a brand-new TLS
    // handshake on every single fetch - that handshake is typically the
    // single biggest contributor to the multi-second UI freeze during a
    // fetch cycle. Keeping the client alive lets the underlying TLS
    // session/connection be reused where the server supports it.
    WiFiClientSecure persistentClient;
    bool clientConfigured = false;

    // --- Background task state --------------------------------------------
    // The fetch task runs on core 0 and writes into workTable/workResult.
    // The main loop (core 1) only ever reads them, and only after
    // readyFlag is true - the task sets that flag last (with release
    // ordering), so by the time the main loop sees it true (via an
    // acquire load) the task is guaranteed done writing. That's a simple,
    // race-free single-producer/single-consumer hand-off with no extra
    // mutex needed, as long as only one fetch is ever in flight at a time
    // (enforced by busyFlag).
    TaskHandle_t fetchTaskHandle = nullptr;
    SemaphoreHandle_t startSem = nullptr;

    std::atomic<bool> busyFlag{false};
    std::atomic<bool> readyFlag{false};

    struct Request { double lat; double lon; float radiusKm; };
    Request pendingRequest;

    Aircraft workTable[Config::MAX_TRACKED_AIRCRAFT];
    FetchResult workResult;

    void fetchTaskFn(void*) {
        for (;;) {
            xSemaphoreTake(startSem, portMAX_DELAY);
            // pendingRequest was fully written by requestFetch() strictly
            // before it gave this semaphore, so it's safe to read here.
            workResult = fetch(pendingRequest.lat, pendingRequest.lon,
                                pendingRequest.radiusKm,
                                workTable, Config::MAX_TRACKED_AIRCRAFT);
            readyFlag.store(true, std::memory_order_release);
        }
    }
}

void primeTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    uint32_t start = millis();
    while (now < 8 * 3600 * 2 && millis() - start < 5000) {
        delay(100);
        now = time(nullptr);
    }
}

FetchResult fetch(double homeLat, double homeLon, float radiusKm,
                   Aircraft* table, uint8_t tableCapacity) {
    FetchResult result;

    if (WiFi.status() != WL_CONNECTED) {
        return result;
    }

    if (!clientConfigured) {
        persistentClient.setInsecure();
        persistentClient.setTimeout(Config::HTTP_TIMEOUT_MS);
        clientConfigured = true;
    }

    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url),
             "https://%s/api/v3/lat/%.5f/lon/%.5f/dist/%.0f",
             Config::ADSB_API_HOST, homeLat, homeLon, radiusKm);

    http.setTimeout(Config::HTTP_TIMEOUT_MS);
    if (!http.begin(persistentClient, url)) {
        return result;
    }
    http.setReuse(true);

    int code = http.GET();
    result.httpCode = code;

    if (code != HTTP_CODE_OK) {
        http.end();
        return result;
    }
    JsonDocument filter;
    JsonObject filterAc = filter["ac"].add<JsonObject>();
    filterAc["hex"]      = true;
    filterAc["flight"]   = true;
    filterAc["r"]        = true;   // registration
    filterAc["t"]        = true;   // type code
    filterAc["lat"]      = true;
    filterAc["lon"]      = true;
    filterAc["alt_baro"] = true;
    filterAc["baro_rate"]= true;
    filterAc["gs"]       = true;
    filterAc["track"]    = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));

    http.end();

    if (err) {
        result.ok = false;
        return result;
    }

    JsonArray acArray = doc["ac"].as<JsonArray>();
    uint8_t idx = 0;
    for (JsonObject ac : acArray) {
        if (idx >= tableCapacity) break;

        Aircraft& a = table[idx];
        a = Aircraft{}; // reset

        const char* hex = ac["hex"] | "";
        strncpy(a.hex, hex, sizeof(a.hex) - 1);

        const char* flight = ac["flight"] | "";
        strncpy(a.callsign, flight, sizeof(a.callsign) - 1);

        const char* reg = ac["r"] | "";
        strncpy(a.reg, reg, sizeof(a.reg) - 1);

        const char* type = ac["t"] | "";
        strncpy(a.typeCode, type, sizeof(a.typeCode) - 1);

        a.lat = ac["lat"] | 0.0f;
        a.lon = ac["lon"] | 0.0f;

        if (ac["alt_baro"].is<const char*>()) {
            a.altBaroFt = 0;
        } else {
            a.altBaroFt = ac["alt_baro"] | 0;
        }

        a.vertRateFtMin = ac["baro_rate"] | 0;
        a.groundSpeedKt = ac["gs"] | 0.0f;
        a.headingDeg    = ac["track"] | 0.0f;

        a.lastSeenMs = millis();
        a.valid = (a.lat != 0.0f || a.lon != 0.0f);

        if (a.valid) idx++;
    }

    result.ok = true;
    result.aircraftCount = idx;
    return result;
}

void startBackgroundTask() {
    if (fetchTaskHandle) return; // already started
    startSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(fetchTaskFn, "adsbFetch",
                             12288, nullptr, 1, &fetchTaskHandle,
                             0 /* core 0 - opposite the Arduino loop task */);
}

bool requestFetch(double homeLat, double homeLon, float radiusKm) {
    if (busyFlag.load(std::memory_order_acquire)) return false;
    pendingRequest = { homeLat, homeLon, radiusKm };
    busyFlag.store(true, std::memory_order_release);
    xSemaphoreGive(startSem);
    return true;
}

bool resultReady() {
    return readyFlag.load(std::memory_order_acquire);
}

FetchResult consumeResult(Aircraft* outTable, uint8_t outCapacity) {
    FetchResult r = workResult;
    uint8_t n = outCapacity < Config::MAX_TRACKED_AIRCRAFT
                    ? outCapacity : Config::MAX_TRACKED_AIRCRAFT;
    memcpy(outTable, workTable, n * sizeof(Aircraft));
    readyFlag.store(false, std::memory_order_release);
    busyFlag.store(false, std::memory_order_release);
    return r;
}

}