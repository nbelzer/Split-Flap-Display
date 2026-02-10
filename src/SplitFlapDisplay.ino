// Split Flap Display
// Morgan Manly 02/16/2025
// Jordan Hoff 03/25/2025
// Thom Koopman 03/30/2025

// Enjoy :)
#include "JsonSettings.h"
#include "SplitFlapDisplay.h"
#include "SplitFlapMqtt.h"
#include "SplitFlapWebServer.h"

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// clang-format off
JsonSettings settings = JsonSettings("config", {
    // General Settings
    {"name", JsonSetting("My Display")},
    {"mdns", JsonSetting("splitflap")},
    {"otaPass", JsonSetting("")},
    {"timezone", JsonSetting("Etc/UTC")},
    // Wifi Settings
    {"ssid", JsonSetting("")},
    {"password", JsonSetting("")},
    // MQTT Settings
    {"mqtt_server", JsonSetting("")},
    {"mqtt_port", JsonSetting(1883)},
    {"mqtt_user", JsonSetting("")},
    {"mqtt_pass", JsonSetting("")},
    {"fetchEndpoint", JsonSetting("")},
    // Hardware Settings
    {"moduleCount", JsonSetting(8)},
    {"moduleAddresses", JsonSetting({0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27})},
    {"magnetPosition", JsonSetting(730)},
    {"moduleOffsets", JsonSetting({0, 0, 0, 0, 0, 0, 0, 0})},
    {"displayOffset", JsonSetting(0)},
    {"sdaPin", JsonSetting(8)},
    {"sclPin", JsonSetting(9)},
    {"stepsPerRot", JsonSetting(2048)},
    {"maxVel", JsonSetting(15.0f)},
    {"charset", JsonSetting(37)},
    // Operational States
    {"mode", JsonSetting(0)}
});
// clang-format on

WiFiClient wifiClient;
SplitFlapDisplay display(settings);
SplitFlapWebServer webServer(settings);
SplitFlapMqtt splitflapMqtt(settings, wifiClient);

unsigned long lastFetchRequestTime = 0;
int lastActiveMode = -1;
const unsigned long FETCH_MODE_REQUEST_INTERVAL_MS = 5000;

void setup() {
    // put your setup code here, to run once:
    Serial.begin(SERIAL_SPEED);

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

        display.init();
        display.homeToString("");

        if (display.getNumModules() == 8) {
            display.writeString("Wifi Err");
        } else {
            display.writeChar('X');
        }
    } else {
        webServer.enableOta();
        webServer.startMDNS();
        webServer.startWebServer();

        display.init();
        splitflapMqtt.setup();
        splitflapMqtt.setDisplay(&display);
        display.setMqtt(&splitflapMqtt);
        display.homeToString("");

        display.writeString("OK");
        delay(250);
        display.writeString("");
    }
}

void loop() {
    splitflapMqtt.loop();

    // check what mode the display is in, this value is updated by the web server
    int activeMode = webServer.getMode();
    if (activeMode != lastActiveMode) {
        if (activeMode == 6) {
            lastFetchRequestTime = 0;
        }
        lastActiveMode = activeMode;
    }

    switch (activeMode) {
        case 0: singleInputMode(); break;
        case 1: multiInputMode(); break;
        case 2: dateMode(); break;
        case 3: timeMode(); break;
        case 4: break;
        case 5: randomTest(); break;
        case 6: endpointFetchMode(); break;
        default: break;
    }

    webServer.handleOta();
    checkConnection();

    reconnectIfNeeded();

    webServer.checkRebootRequired();
    yield();
}

void singleInputMode() {
    String userInput = webServer.getInputString();
    if (userInput != webServer.getWrittenString()) {
        display.writeString(userInput, MAX_RPM, webServer.getCentering());
        webServer.setWrittenString(userInput);
    }
}

void multiInputMode() {
    if (millis() - webServer.getLastSwitchMultiTime() > webServer.getMultiWordDelay()) {
        // get user input, extract correct word from index using webserver counter, and display
        String userInput = webServer.getMultiInputString();
        String currWord = extractFromCSV(userInput, webServer.getMultiWordCurrentIndex());
        if (currWord != webServer.getWrittenString()) {
            display.writeString(currWord, MAX_RPM, webServer.getCentering());
            webServer.setWrittenString(currWord);
        }
        webServer.setLastSwitchMultiTime(millis());
        webServer.setMultiWordCurrentIndex((webServer.getMultiWordCurrentIndex() + 1) % (webServer.getNumMultiWords()));
    }
}

void dateMode() {
    if (millis() - webServer.getLastCheckDateTime() > webServer.getDateCheckInterval()) {
        webServer.setLastCheckDateTime(millis());
        String currentDay = webServer.getCurrentDay();
        String dayPrefix = webServer.getDayPrefix(3);

        String outputString = " ";
        switch (display.getNumModules()) {
            case 2: outputString = currentDay; break;
            case 3: outputString = dayPrefix; break;
            case 4: outputString = " " + currentDay + " "; break;
            case 5: outputString = dayPrefix + currentDay; break;
            case 6: outputString = dayPrefix + " " + currentDay; break;
            case 7: outputString = dayPrefix + "  " + currentDay; break;
            case 8: outputString = dayPrefix + currentDay + webServer.getMonthPrefix(3); break;
            default: break;
        }
        if (outputString != webServer.getWrittenString()) {
            display.writeString(outputString, MAX_RPM, webServer.getCentering());
            webServer.setWrittenString(outputString);
        }
    }
}

void timeMode() {
    if (millis() - webServer.getLastCheckDateTime() > webServer.getDateCheckInterval()) {
        webServer.setLastCheckDateTime(millis());
        String currentHour = webServer.getCurrentHour();
        String currentMinute = webServer.getCurrentMinute();
        String outputString = " ";

        switch (display.getNumModules()) {
            case 2: outputString = currentMinute; break;
            case 3: outputString = " " + currentMinute; break;
            case 4: outputString = currentHour + "" + currentMinute; break;
            case 5: outputString = currentHour + " " + currentMinute; break;
            case 6: outputString = " " + currentHour + " " + currentMinute; break;
            case 7: outputString = " " + currentHour + " " + currentMinute + " "; break;
            case 8: outputString = " " + currentHour + currentMinute + " "; break;
            default: break;
        }

        if (outputString != webServer.getWrittenString()) {
            display.writeString(outputString, MAX_RPM, webServer.getCentering());
            webServer.setWrittenString(outputString);
        }
    }
}

void randomTest() {
    display.testRandom();
    delay(2500);
}

String trimTrailingNewlines(String input) {
    while (input.endsWith("\n") || input.endsWith("\r")) {
        input.remove(input.length() - 1);
    }
    return input;
}

String getDisplayTail(String input, int maxChars) {
    if (maxChars <= 0 || input.length() <= maxChars) {
        return input;
    }

    return input.substring(input.length() - maxChars);
}

bool isFetchDisplayCharAllowed(char c, int charsetSize) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
        return true;
    }

    if (charsetSize == 48) {
        switch (c) {
            case '\'':
            case ':':
            case '?':
            case '!':
            case '.':
            case '-':
            case '/':
            case '$':
            case '@':
            case '#':
            case '%': return true;
            default: break;
        }
    }

    return false;
}

String normalizeFetchDisplayText(String input, int charsetSize) {
    String output = "";
    output.reserve(input.length());

    bool previousWasSpace = false;
    for (int i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') {
            c = c - ('a' - 'A');
        }

        if (! isFetchDisplayCharAllowed(c, charsetSize)) {
            c = ' ';
        }

        if (c == ' ') {
            if (previousWasSpace) {
                continue;
            }
            previousWasSpace = true;
        } else {
            previousWasSpace = false;
        }

        output += c;
    }

    output.trim();
    return output;
}

bool parseEndpointUrl(const String &endpoint, bool &useTls, String &host, int &port, String &path) {
    useTls = false;
    int schemeLength = 0;
    int defaultPort = 80;

    if (endpoint.startsWith("http://")) {
        useTls = false;
        schemeLength = 7;
        defaultPort = 80;
    } else if (endpoint.startsWith("https://")) {
        useTls = true;
        schemeLength = 8;
        defaultPort = 443;
    } else {
        return false;
    }

    String endpointWithoutScheme = endpoint.substring(schemeLength);
    int pathIndex = -1;
    int slashIndex = endpointWithoutScheme.indexOf('/');
    int queryIndex = endpointWithoutScheme.indexOf('?');
    int fragmentIndex = endpointWithoutScheme.indexOf('#');

    if (slashIndex >= 0) {
        pathIndex = slashIndex;
    }
    if (queryIndex >= 0 && (pathIndex < 0 || queryIndex < pathIndex)) {
        pathIndex = queryIndex;
    }
    if (fragmentIndex >= 0 && (pathIndex < 0 || fragmentIndex < pathIndex)) {
        pathIndex = fragmentIndex;
    }

    String hostPort = pathIndex >= 0 ? endpointWithoutScheme.substring(0, pathIndex) : endpointWithoutScheme;
    path = pathIndex >= 0 ? endpointWithoutScheme.substring(pathIndex) : "/";
    if (path.startsWith("?")) {
        path = "/" + path;
    } else if (path.startsWith("#")) {
        path = "/";
    }

    int hashInPath = path.indexOf('#');
    if (hashInPath >= 0) {
        path = path.substring(0, hashInPath);
    }
    if (path.isEmpty()) {
        path = "/";
    }

    port = defaultPort;
    host = hostPort;
    if (hostPort.startsWith("[")) {
        int closingBracket = hostPort.indexOf(']');
        if (closingBracket < 0) {
            Serial.println("Fetch mode endpoint format is invalid.");
            return false;
        }
        host = hostPort.substring(1, closingBracket);
        if (closingBracket + 1 < hostPort.length()) {
            if (hostPort[closingBracket + 1] != ':') {
                Serial.println("Fetch mode endpoint format is invalid.");
                return false;
            }
            port = hostPort.substring(closingBracket + 2).toInt();
        }
    } else {
        int colonIndex = hostPort.lastIndexOf(':');
        if (colonIndex >= 0) {
            host = hostPort.substring(0, colonIndex);
            port = hostPort.substring(colonIndex + 1).toInt();
        }
    }

    if (host == "" || port <= 0 || port > 65535 || path == "") {
        return false;
    }

    return true;
}

String buildEndpointUrl(bool useTls, const String &host, int port, const String &path) {
    String endpointUrl = String(useTls ? "https://" : "http://") + host;
    if ((! useTls && port != 80) || (useTls && port != 443)) {
        endpointUrl += ":" + String(port);
    }
    endpointUrl += path;
    return endpointUrl;
}

String resolveRedirectUrl(
    const String &location,
    bool useTls,
    const String &host,
    int port,
    const String &path
) {
    if (location.startsWith("http://") || location.startsWith("https://")) {
        return location;
    }

    if (location.startsWith("//")) {
        return String(useTls ? "https:" : "http:") + location;
    }

    String origin = String(useTls ? "https://" : "http://") + host;
    if ((! useTls && port != 80) || (useTls && port != 443)) {
        origin += ":" + String(port);
    }

    if (location.startsWith("/")) {
        return origin + location;
    }

    String basePath = path;
    int queryIndex = basePath.indexOf('?');
    if (queryIndex >= 0) {
        basePath = basePath.substring(0, queryIndex);
    }

    int lastSlash = basePath.lastIndexOf('/');
    if (lastSlash < 0) {
        basePath = "/";
    } else {
        basePath = basePath.substring(0, lastSlash + 1);
    }

    return origin + basePath + location;
}

bool fetchEndpointText(const String &endpoint, String &responseText) {
    String currentEndpoint = endpoint;
    const int maxRedirects = 3;

    for (int redirectCount = 0; redirectCount <= maxRedirects; redirectCount++) {
        bool useTls = false;
        String host = "";
        int port = 0;
        String path = "/";
        if (! parseEndpointUrl(currentEndpoint, useTls, host, port, path)) {
            Serial.println("Fetch mode endpoint format is invalid.");
            return false;
        }

        WiFiClient httpClient;
        WiFiClientSecure httpsClient;
        Client *client = nullptr;

        if (useTls) {
            httpsClient.setInsecure();
            client = &httpsClient;
        } else {
            client = &httpClient;
        }

        Serial.println("Fetch mode requesting " + buildEndpointUrl(useTls, host, port, path));
        IPAddress resolvedIp;
        if (! WiFi.hostByName(host.c_str(), resolvedIp)) {
            Serial.println("Fetch mode DNS lookup failed for host: " + host);
            return false;
        }
        Serial.println("Fetch mode DNS resolved " + host + " -> " + resolvedIp.toString());

        const unsigned long responseTimeoutMs = useTls ? 15000UL : 8000UL;
        client->setTimeout(responseTimeoutMs);

        bool connected = false;
        if (useTls) {
            connected = client->connect(host.c_str(), port);
        } else {
            connected = client->connect(resolvedIp, port);
        }

        if (! connected) {
            Serial.println("Fetch mode could not connect to endpoint host.");
            return false;
        }

        String hostHeader = host;
        if (host.indexOf(':') >= 0 && ! host.startsWith("[")) {
            hostHeader = "[" + host + "]";
        }
        if ((! useTls && port != 80) || (useTls && port != 443)) {
            hostHeader += ":" + String(port);
        }

        client->print("GET " + path + " HTTP/1.1\r\n");
        client->print("Host: " + hostHeader + "\r\n");
        client->print("Accept: */*\r\n");
        client->print("Connection: close\r\n");
        client->print("User-Agent: SplitFlapDisplay\r\n");
        client->print("\r\n");

        unsigned long waitStart = millis();
        while (! client->available()) {
            if (! client->connected() || millis() - waitStart > responseTimeoutMs) {
                client->stop();
                Serial.println("Fetch mode timed out waiting for endpoint response (" + String(responseTimeoutMs) + "ms).");
                return false;
            }
            delay(5);
            yield();
        }

        String statusLine = client->readStringUntil('\n');
        statusLine.trim();
        Serial.println("Fetch mode response status: " + statusLine);

        int statusCode = 0;
        int firstSpace = statusLine.indexOf(' ');
        if (firstSpace >= 0 && statusLine.length() >= firstSpace + 4) {
            statusCode = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
        }

        String redirectLocation = "";
        while (client->connected() || client->available()) {
            String line = client->readStringUntil('\n');
            line.trim();
            if (line == "") {
                break;
            }

            int colonPos = line.indexOf(':');
            if (colonPos <= 0) {
                continue;
            }

            String headerName = line.substring(0, colonPos);
            headerName.trim();
            headerName.toLowerCase();

            if (headerName == "location") {
                redirectLocation = line.substring(colonPos + 1);
                redirectLocation.trim();
            }
        }

        if (statusCode == 200) {
            String body = "";
            const size_t maxBodyBytes = 2048;
            bool bodyTruncated = false;
            unsigned long lastBodyDataTime = millis();

            while (client->connected() || client->available()) {
                bool readAny = false;
                while (client->available()) {
                    if (body.length() < maxBodyBytes) {
                        body += static_cast<char>(client->read());
                    } else {
                        client->read();
                        bodyTruncated = true;
                    }
                    readAny = true;
                }

                if (readAny) {
                    lastBodyDataTime = millis();
                } else if (millis() - lastBodyDataTime > responseTimeoutMs) {
                    Serial.println("Fetch mode body read timed out.");
                    break;
                }

                yield();
            }

            client->stop();
            if (bodyTruncated) {
                Serial.println("Fetch mode body truncated to " + String(maxBodyBytes) + " bytes.");
            }
            Serial.println("Fetch mode received body bytes: " + String(body.length()));
            responseText = body;
            return true;
        }

        if ((statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) &&
            ! redirectLocation.isEmpty()) {
            client->stop();

            if (redirectCount >= maxRedirects) {
                Serial.println("Fetch mode redirect limit reached. Last status: " + statusLine);
                return false;
            }

            currentEndpoint = resolveRedirectUrl(redirectLocation, useTls, host, port, path);
            Serial.println("Fetch mode following redirect to: " + currentEndpoint);
            continue;
        }

        client->stop();
        Serial.println("Fetch mode request failed. Status: " + statusLine);
        if (statusCode >= 300 && statusCode < 400 && redirectLocation.isEmpty()) {
            Serial.println("Fetch mode redirect response missing Location header.");
        }
        return false;
    }

    return false;
}

void endpointFetchMode() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (millis() - lastFetchRequestTime < FETCH_MODE_REQUEST_INTERVAL_MS) {
        return;
    }
    lastFetchRequestTime = millis();

    String endpoint = settings.getString("fetchEndpoint");
    endpoint.trim();
    if (endpoint == "") {
        return;
    }

    String fetchedValue;
    if (! fetchEndpointText(endpoint, fetchedValue)) {
        return;
    }

    fetchedValue = trimTrailingNewlines(fetchedValue);
    String displayValue = normalizeFetchDisplayText(fetchedValue, display.getCharsetSize());
    if (displayValue == "") {
        displayValue = " ";
    }

    String outputString = getDisplayTail(displayValue, display.getNumModules());
    Serial.println("Fetch mode output string: [" + outputString + "]");

    if (outputString != webServer.getWrittenString()) {
        display.writeString(outputString, MAX_RPM, false);
        webServer.setWrittenString(outputString);
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
            display.writeChar('X');
        } else {
            webServer.enableOta();
            webServer.endMDNS();
            webServer.startMDNS();
            display.writeString("OK");
            webServer.setWrittenString("OK");
            delay(500);
            display.writeString("");
            webServer.setWrittenString("");
        }

        splitflapMqtt.setup();
    }
}

String extractFromCSV(String str, int index) {
    int startIndex = 0;
    int endIndex = str.length();

    int commaCount = 0;
    for (int i = 0; i < str.length(); i++) {
        if (str[i] == ',') {
            commaCount++;
            if (commaCount == index) {
                startIndex = i + 1; // skip past the comma
            } else if (commaCount == index + 1) {
                endIndex = i;
            }
        }
    }

    return str.substring(startIndex, endIndex);
}
