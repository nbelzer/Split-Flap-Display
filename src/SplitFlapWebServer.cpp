#include "SplitFlapWebServer.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <time.h>

#define AP_SSID "Split Flap Display"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

SplitFlapWebServer::SplitFlapWebServer(JsonSettings &settings)
    : settings(settings), connectionMode(0), rebootRequired(false), attemptReconnect(false), wifiCheckInterval(1000),
      server(80) {}

void SplitFlapWebServer::init() {
    if (! LittleFS.begin()) {
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }
}

void SplitFlapWebServer::checkWiFi() {
    if (connectionMode == 1) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi lost! Forcing reconnect...");
            WiFi.disconnect();
            WiFi.reconnect();
        }
    }
}

bool SplitFlapWebServer::loadWiFiCredentials() {
    // Allow WIFI_SSID and WIFI_PASS to be overridden by compile-time definitions
    String ssid = String(WIFI_SSID).isEmpty() ? settings.getString("ssid") : String(WIFI_SSID);
    String password = String(WIFI_PASS).isEmpty() ? settings.getString("password") : String(WIFI_PASS);

    if (ssid != "" && password != "") {
        Serial.println("Wi-Fi credentials loaded successfully.");
        Serial.print("Connecting to Network: ");
        Serial.println(ssid);
        WiFi.mode(WIFI_STA);
#ifdef WIFI_TX_POWER
        delay(100);
        WiFi.setTxPower((wifi_power_t) WIFI_TX_POWER);
#endif
        WiFi.begin(ssid.c_str(), password.c_str());
        return true; // Return true if credentials exist
    }
    return false;    // Return false if no credentials were found
}

void SplitFlapWebServer::checkRebootRequired() {
    if (rebootRequired) {
        Serial.println("Reboot required. Restarting...");
        delay(1000);
        ESP.restart();
    }
}

void SplitFlapWebServer::handleOta() {
    ArduinoOTA.handle();
}
void SplitFlapWebServer::enableOta() {
    // Skip OTA initialisation if no password is set
    if (settings.getString("otaPass") == "") {
        return;
    }

    ArduinoOTA.setHostname(settings.getString("mdns").c_str()); // otherwise mdns name gets overwritten with default
    ArduinoOTA.setPassword(settings.getString("otaPass").c_str());

    ArduinoOTA
        .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {            // U_LITTLEFS
            type = "filesystem";
            LittleFS.end(); // Unmount the filesystem before update
        }
        Serial.println("Start updating " + type);
    })
        .onEnd([]() {
        Serial.println("\nEnd");
        LittleFS.begin(); // Remount filesystem
    })
        .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    }).onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        LittleFS.begin(); // Remount filesystem
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });

    ArduinoOTA.begin();
    Serial.println("OTA Initialized");
}

bool SplitFlapWebServer::connectToWifi() {
    if (loadWiFiCredentials()) {
        unsigned long startAttemptTime = millis();
        const unsigned long timeout = 20000; // 20 seconds
        unsigned long lastPrintTime = startAttemptTime;

        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startAttemptTime >= timeout) {
                Serial.println("_");
                Serial.println("Wi-Fi connection failed! Timeout reached.");
                return false; // Return false if unable to connect in 30 seconds
            }
            if ((millis() - lastPrintTime) > 1000) {
                Serial.print(".");
                lastPrintTime = millis();
            }
            yield();
        }

        // connected succesfully
        connectionMode = 1;
        WiFi.softAPdisconnect(); // Turns off SoftAP mode only after connected to
        // actual network
        WiFi.setAutoReconnect(true);
        WiFi.persistent(true); // Saves Wi-Fi settings to flash memory
        WiFi.setSleep(false);
        configTime(0, 0, "pool.ntp.org");
        Serial.println("Connected to Wi-Fi!");
        Serial.println("IP Address: http://" + WiFi.localIP().toString());
        return true;
    }
    return false;
}

void SplitFlapWebServer::startAccessPoint() {
    connectionMode = 0;
    const char *apSSID = AP_SSID;
    WiFi.softAP(apSSID);
#ifdef WIFI_TX_POWER
    delay(100);
    WiFi.setTxPower((wifi_power_t) WIFI_TX_POWER);
#endif
    Serial.println("AP Mode Started!");
    Serial.println("Connect to: " + String(apSSID));
    Serial.println("AP IP Address: http://" + WiFi.softAPIP().toString());
}

void fourOhFour(AsyncWebServerRequest *request) {
    Serial.println("Request: " + request->url());
    Serial.println("Method: " + String(request->methodToString()));
    request->send(404);
}

void SplitFlapWebServer::endMDNS() {
    MDNS.end();
    Serial.println("mDNS responder stopped");
}

void SplitFlapWebServer::startMDNS() {
    if (! MDNS.begin(settings.getString("mdns").c_str())) {
        Serial.println("Error setting up MDNS responder!");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("mDNS: http://" + settings.getString("mdns") + ".local");
}

void SplitFlapWebServer::startWebServer() {
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) { request->redirect("/index.html"); });

    File root = LittleFS.open("/");
    if (! root || ! root.isDirectory()) {
        Serial.println("Failed to open directory or not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (String(file.name()).endsWith(".gz")) {
            const char *filename = file.name();
            String tempFilename = (String("/") + String(filename));
            tempFilename.replace(".gz", "");
            filename = tempFilename.c_str();

            server.serveStatic(filename, LittleFS, filename, "max-age=600");
        }
        file = root.openNextFile();
    }

    server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", settings.toJson().as<String>());
    });

    server.on("/settings/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        settings.reset();
        this->homingRequested = true;

        JsonDocument response;
        response["message"] = "Settings reset successfully! Reconnect to the " + String(AP_SSID) + " network";
        response["persistent"] = true;

        request->send(200, "application/json", response.as<String>());

        this->attemptReconnect = true;
    });

    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/settings",
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (request->method() != HTTP_POST) {
            return request->send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
        }

        Serial.println("Received settings update request");
        Serial.println(json.as<String>());

        bool rebootRequired = false;
        bool reconnect = false;
        JsonDocument response;
        response["message"] = "Settings saved successfully!";

        // TODO Refactor this it's gross
        if ((json["ssid"].is<String>() && json["ssid"].as<String>() != settings.getString("ssid")) ||
            (json["password"].is<String>() && json["password"].as<String>() != settings.getString("password"))) {
            reconnect = true;
            response["message"] = "Settings updated successfully, Network " "settings have changed, reconnect to the " +
                json["ssid"].as<String>() + " network";
        }

        if (json["otaPass"].is<String>() && json["otaPass"].as<String>() != settings.getString("otaPass")) {
            rebootRequired = true; // OTA password change can only be applied by rebooting
            response["message"] = "Settings updated successfully, OTA Password has changed. Rebooting...";
        }

        if (json["mdns"].is<String>() && json["mdns"].as<String>() != settings.getString("mdns")) {
            reconnect = true;
            response["message"] =
                "Settings updated successfully, mDNS name has changed, " "automatically redirecting to http://" +
                json["mdns"].as<String>() + ".local...";
            response["redirect"] = "http://" + json["mdns"].as<String>() + ".local/settings.html";
        }

        if ((json["mqtt_server"].is<String>() && json["mqtt_server"].as<String>() != settings.getString("mqtt_server")
            ) ||
            (json["mqtt_port"].is<int>() && json["mqtt_port"].as<int>() != settings.getInt("mqtt_port")) ||
            (json["mqtt_user"].is<String>() && json["mqtt_user"].as<String>() != settings.getString("mqtt_user")) ||
            (json["mqtt_pass"].is<String>() && json["mqtt_pass"].as<String>() != settings.getString("mqtt_pass"))) {
            response["message"] = "Mqtt settings have changed, reconnecting...";
            reconnect = true;
        }

        if (json["contentUrl"].is<String>()) {
            String contentUrl = json["contentUrl"].as<String>();
            if (! contentUrl.isEmpty() && ! contentUrl.startsWith("http://") && ! contentUrl.startsWith("https://")) {
                response["message"] = "Content URL must start with http:// or https://";
                response["type"] = "error";
                response["errors"]["key"] = "contentUrl";
                response["errors"]["message"] = response["message"];
                return request->send(400, "application/json", response.as<String>());
            }
        }

        if (! settings.fromJson(json)) {
            response["message"] = "Failed to save settings";
            response["type"] = "error";
            response["errors"]["key"] = settings.getLastValidationKey();
            response["errors"]["message"] = settings.getLastValidationError();
            return request->send(400, "application/json", response.as<String>());
        }

        response["type"] = "success";
        response["persistent"] = reconnect;

        request->send(200, "application/json", response.as<String>());

        this->homingRequested = true;
        this->rebootRequired = rebootRequired;
        this->attemptReconnect = reconnect;
    }
    ));

    server.onNotFound(fourOhFour);

    server.begin();
}
