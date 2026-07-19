#include "SplitFlapUrlClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr uint16_t HTTP_TIMEOUT_MS = 5000;
constexpr time_t MIN_VALID_EPOCH = 1577836800; // 2020-01-01

void removeTrailingLineEndings(String &content) {
    while (content.endsWith("\r") || content.endsWith("\n")) {
        content.remove(content.length() - 1);
    }
}
} // namespace

SplitFlapUrlClient::SplitFlapUrlClient(JsonSettings &settings) : settings(settings) {}

bool SplitFlapUrlClient::fetchIfDue(String &content) {
    unsigned long nowMillis = millis();
    time_t now = time(nullptr);
    bool clockIsSet = now >= MIN_VALID_EPOCH;
    time_t currentMinute = clockIsSet ? now / 60 : -1;

    if (! refreshRequested) {
        if (clockIsSet) {
            // If SNTP synchronized after an immediate fallback fetch, arm the
            // next minute boundary without fetching again mid-minute.
            if (lastFetchMinute < 0) {
                lastFetchMinute = currentMinute;
                return false;
            }
            if (currentMinute == lastFetchMinute) {
                return false;
            }
        } else if (nowMillis - lastFetchTime < FETCH_INTERVAL_MS) {
            return false;
        }
    }

    refreshRequested = false;
    lastFetchTime = nowMillis;
    lastFetchMinute = currentMinute;

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String url = settings.getString("contentUrl");
    if (url.isEmpty()) {
        return false;
    }

    bool secure = url.startsWith("https://");
    if (! secure && ! url.startsWith("http://")) {
        Serial.println("Content URL must start with http:// or https://");
        return false;
    }

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    if (secure) {
        // This endpoint exposes public display data. Avoid coupling firmware
        // updates to the certificate authority used by a configured endpoint.
        secureClient.setInsecure();
        if (! http.begin(secureClient, url)) {
            Serial.println("Failed to initialize HTTPS content request");
            return false;
        }
    } else if (! http.begin(plainClient, url)) {
        Serial.println("Failed to initialize HTTP content request");
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept", "text/plain");

    int status = http.GET();
    if (status < 200 || status >= 300) {
        if (status < 0) {
            Serial.println("Content request failed: " + http.errorToString(status));
        } else {
            Serial.println("Content request returned HTTP " + String(status));
        }
        http.end();
        return false;
    }

    content = http.getString();
    http.end();
    removeTrailingLineEndings(content);

    Serial.println("Fetched display content: " + content);
    return true;
}
