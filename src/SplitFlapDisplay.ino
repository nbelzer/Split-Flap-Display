// Split Flap Display
// Morgan Manly 02/16/2025
// Jordan Hoff 03/25/2025
// Thom Koopman 03/30/2025

// Enjoy :)
#include "JsonSettings.h"
#include "SplitFlapDisplay.h"
#include "SplitFlapMqtt.h"
#include "SplitFlapUrlClient.h"
#include "SplitFlapWebServer.h"

#include <Arduino.h>
#include <WiFiClient.h>

namespace {
constexpr unsigned long CONFIGURATION_WINDOW_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long CONFIGURATION_LOOP_DELAY_MS = 20;
constexpr unsigned long LOW_POWER_LOOP_DELAY_MS = 250;

unsigned long bootTime = 0;
bool configurationServicesStopped = false;
}

// clang-format off
JsonSettings settings = JsonSettings("config", {
    // General Settings
    {"name", JsonSetting("My Display")},
    {"mdns", JsonSetting("splitflap")},
    {"otaPass", JsonSetting("")},
    {"contentUrl", JsonSetting("")},
    // Wifi Settings
    {"ssid", JsonSetting("")},
    {"password", JsonSetting("")},
    // MQTT Settings
    {"mqtt_server", JsonSetting("")},
    {"mqtt_port", JsonSetting(1883)},
    {"mqtt_user", JsonSetting("")},
    {"mqtt_pass", JsonSetting("")},
    // Hardware Settings
    {"moduleCount", JsonSetting(8)},
    {"moduleAddresses", JsonSetting({0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27})},
    {"magnetPosition", JsonSetting(730)},
    {"moduleOffsets", JsonSetting({0, 0, 0, 0, 0, 0, 0, 0})},
    {"displayOffset", JsonSetting(0)},
    {"sdaPin", JsonSetting(6)},
    {"sclPin", JsonSetting(7)},
    {"stepsPerRot", JsonSetting(2048)},
    {"maxVel", JsonSetting(15.0f)},
    {"charset", JsonSetting(64)}
});
// clang-format on

WiFiClient wifiClient;
SplitFlapDisplay display(settings);
SplitFlapWebServer webServer(settings);
SplitFlapMqtt splitflapMqtt(settings, wifiClient);
SplitFlapUrlClient urlClient(settings);

void setup() {
    // put your setup code here, to run once:
    Serial.begin(SERIAL_SPEED);
    bootTime = millis();

    display.init();

#ifdef STARTUP_DELAY
    delay(STARTUP_DELAY);
#endif

    Serial.println("Init Web Server");
    webServer.init();

    if (! webServer.connectToWifi()) {
        webServer.startAccessPoint();
        webServer.enableOta();
        webServer.startMDNS();
        webServer.startWebServer();

        display.homeWithRainbow();
    } else {
        webServer.enableOta();
        webServer.startMDNS();
        webServer.startWebServer();

        splitflapMqtt.setup();
        splitflapMqtt.setDisplay(&display);
        display.setMqtt(&splitflapMqtt);
        display.homeWithRainbow();
        updateFromUrl();
    }
}

void loop() {
    splitflapMqtt.loop();

    if (webServer.consumeHomingRequest()) {
        // Reload hardware-related settings before homing, then immediately
        // refresh the URL content using any newly saved configuration.
        display.init();
        display.homeToString("");
        urlClient.requestRefresh();
    }

    updateFromUrl();

    webServer.handleOta();
    checkConnection();

    reconnectIfNeeded();

    webServer.checkRebootRequired();

    if (! configurationServicesStopped && millis() - bootTime >= CONFIGURATION_WINDOW_MS) {
        webServer.stopConfigurationServices();
        configurationServicesStopped = true;
    }

    delay(configurationServicesStopped ? LOW_POWER_LOOP_DELAY_MS : CONFIGURATION_LOOP_DELAY_MS);
}

void updateFromUrl() {
    String content;
    if (urlClient.fetchIfDue(content)) {
        // Treat the response as positional display text. writeString handles
        // UTF-8 symbols and truncates it to the configured module count.
        display.writeString(content, MAX_RPM, false);
    }
}

void checkConnection() {
    if (millis() - webServer.getLastCheckWifiTime() >
        webServer.getWifiCheckInterval()) { // check wifi to see if disconnected
        webServer.checkWiFi();
        webServer.setLastCheckWifiTime(millis());
    }
}

void reconnectIfNeeded() {
    if (webServer.getAttemptReconnect()) { // check if the device should attempt reconnection to wifi
        webServer.setAttemptReconnect(false);
        display.writeString("");
        if (! webServer.connectToWifi()) {
            webServer.startAccessPoint();
            webServer.enableOta();
            webServer.endMDNS();
            webServer.startMDNS();
        } else {
            webServer.enableOta();
            webServer.endMDNS();
            webServer.startMDNS();
            display.writeString("OK");
            delay(500);
            display.writeString("");
            urlClient.requestRefresh();
        }

        splitflapMqtt.setup();
    }
}
