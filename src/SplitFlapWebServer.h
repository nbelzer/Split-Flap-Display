#pragma once

#include "JsonSettings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

class SplitFlapWebServer {
  public:
    SplitFlapWebServer(JsonSettings &settings);
    void init();
    void checkRebootRequired();

    // Wifi Connectivity
    bool loadWiFiCredentials();
    bool connectToWifi();
    bool getAttemptReconnect() const { return attemptReconnect; }
    void setAttemptReconnect(bool input) { attemptReconnect = input; }
    void startWebServer();
    void stopConfigurationServices();
    void endMDNS();
    void startMDNS();
    void enableOta();
    void handleOta();
    void startAccessPoint();
    void checkWiFi();
    unsigned long getLastCheckWifiTime() { return lastCheckWifiTime; }
    void setLastCheckWifiTime(unsigned long input) { lastCheckWifiTime = input; }
    int getWifiCheckInterval() { return wifiCheckInterval; }

    bool consumeHomingRequest() {
        bool requested = homingRequested;
        homingRequested = false;
        return requested;
    }

  private:
    JsonSettings &settings;

    int connectionMode; // 0 is AP mode, 1 is Internet Mode

    bool rebootRequired;
    bool attemptReconnect;
    volatile bool homingRequested = false;
    unsigned long lastCheckWifiTime;
    int wifiCheckInterval;
    AsyncWebServer server; // Declare server as a class member
};
