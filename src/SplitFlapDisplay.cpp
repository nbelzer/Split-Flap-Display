#include "SplitFlapDisplay.h"

#include "JsonSettings.h"
#include "SplitFlapModule.h"
#include "SplitFlapMqtt.h"

namespace {
constexpr unsigned long CALIBRATION_SENSOR_CHECK_INTERVAL_US = 5 * 1000;
constexpr unsigned long MOVEMENT_SENSOR_CHECK_INTERVAL_US = 20 * 1000;
constexpr int CALIBRATION_ACCURACY_STEPS = 4;
constexpr int CALIBRATION_CONFIRMATION_SAMPLES = 2;
constexpr float CALIBRATION_MAX_RPM = 10.0f;
constexpr unsigned long BOOT_RAINBOW_HOLD_MS = 1000;

// The blank symbol maps to the physical black flap in the 64-flap set.
const char *const RAINBOW_CHARS[MAX_MODULES] = {" ", "⬜", "🟥", "🟧", "🟨", "🟩", "🟦", "🟪"};
}

SplitFlapDisplay::SplitFlapDisplay(JsonSettings &settings) : settings(settings) {}

void SplitFlapDisplay::init() {
    numModules = settings.getInt("moduleCount");
    stepsPerRot = settings.getInt("stepsPerRot");
    displayOffset = settings.getInt("displayOffset");
    magnetPosition = settings.getInt("magnetPosition");
    maxVel = settings.getFloat("maxVel");
    charSetSize = settings.getInt("charset");

    std::vector<int> settingAddresses = settings.getIntVector("moduleAddresses");
    for (int i = 0; i < numModules; i++) {
        moduleAddresses[i] = (uint8_t) settingAddresses[i];
    }

    std::vector<int> settingOffsets = settings.getIntVector("moduleOffsets");
    for (int i = 0; i < numModules; i++) {
        moduleOffsets[i] = settingOffsets[i];
    }

    Serial.print("Module Offsets: ");
    for (int i = 0; i < numModules; i++) {
        Serial.print(moduleOffsets[i]);
        Serial.print(" ");
    }
    Serial.println();

    for (uint8_t i = 0; i < numModules; i++) {
        modules[i] = SplitFlapModule(
            moduleAddresses[i], stepsPerRot, moduleOffsets[i] + displayOffset, magnetPosition, charSetSize
        );
    }

    SDAPin = settings.getInt("sdaPin");
    SCLPin = settings.getInt("sclPin");

    Wire.begin(SDAPin, SCLPin);
    Wire.setClock(400000);

    int connectedModules = 0;
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
        moduleConnected[i] = false;
    }
    for (uint8_t i = 0; i < numModules; i++) {
        moduleConnected[i] = modules[i].probe();
        if (moduleConnected[i]) {
            connectedModules++;
        } else {
            Serial.print("Module not found at I2C address 0x");
            Serial.println(moduleAddresses[i], HEX);
        }
    }
    Serial.print("Connected modules: ");
    Serial.print(connectedModules);
    Serial.print("/");
    Serial.println(numModules);

    // Put every motor driver into its off state before running the slower
    // per-module initialization sequence. PCF8575 ports power up HIGH, so a
    // quick shutdown sweep minimizes simultaneous motor current at startup.
    for (uint8_t i = 0; i < numModules; i++) {
        if (moduleConnected[i]) {
            modules[i].stop();
        }
    }

    for (uint8_t i = 0; i < numModules; i++) {
        if (moduleConnected[i]) {
            modules[i].init();
        }
    }
}

void SplitFlapDisplay::testAll() {
    int numChars = modules[0].getCharsetSize();
    int targetPositions[numModules];

    for (int i = 0; i < numChars; i++) {
        // Serial.print("Target Positions: [");
        // fill array with same char

        for (int j = 0; j < numModules; j++) {
            targetPositions[j] = modules[j].getCharPosition(modules[j].getChar(i));
            // Serial.print(targetPositions[j]);
            // Serial.print(" , ");
        }
        // Serial.println("]");

        moveTo(targetPositions);
        delay(500);
    }
}

void SplitFlapDisplay::testRandom(float speed) {
    int targetPositions[numModules];
    int numChars = modules[0].getCharsetSize();

    Serial.print("Target: ");
    for (int i = 0; i < numModules; i++) {
        const char *randChar = modules[i].getChar(random(0, numChars));
        targetPositions[i] = modules[i].getCharPosition(randChar);
        Serial.print(randChar);
    }
    Serial.println(" ");
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::testCount() {
    int maxCount = pow(10, numModules);
    char targetChar;
    int targetInteger;

    int targetPositions[numModules];

    for (int i = 0; i < maxCount; i++) {
        // get each character in the count integer
        for (int j = 0; j < numModules; j++) {
            targetInteger = (i % (int) pow(10, j + 1)) / (int) pow(10, j);
            targetChar = targetInteger + '0'; // convert to char
            targetPositions[numModules - j - 1] = modules[j].getCharPosition(String(targetChar));
        }

        moveTo(targetPositions);
        delay(250);
    }
}

void SplitFlapDisplay::home(float speed) {
    Serial.println("Homing");
    calibrateModules();

    int targetPositions[numModules];
    String homeChar = " ";
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::homeWithRainbow() {
    if (charSetSize != 64) {
        home();
        return;
    }

    Serial.println("Homing with rainbow");
    int targetPositions[MAX_MODULES] = {};
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(RAINBOW_CHARS[i]);
    }

    calibrateModules(targetPositions);
    delay(BOOT_RAINBOW_HOLD_MS);
}

void SplitFlapDisplay::homeToString(String homeString, float speed, bool centering) {
    Serial.println("Homing");
    calibrateModules();
    writeString(homeString, speed, centering);
}

void SplitFlapDisplay::homeToChar(const String &homeChar, float speed) {
    Serial.println("Homing");
    calibrateModules();

    int targetPositions[numModules];
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::writeChar(const String &inputChar, float speed) {
    int targetPositions[numModules];
    // Iterate through the input string and process each character
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(inputChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::writeString(String inputString, float speed, bool centering) {
    String displaySymbols[MAX_MODULES];
    int symbolCount = 0;

    // Arduino String indexes bytes. Split on UTF-8 code points so symbols such
    // as hearts and colored squares consume one module instead of 3-4 modules.
    for (unsigned int byteIndex = 0; byteIndex < inputString.length() && symbolCount < numModules;) {
        uint8_t leadByte = static_cast<uint8_t>(inputString[byteIndex]);
        unsigned int symbolBytes = 1;
        if ((leadByte & 0xE0) == 0xC0) {
            symbolBytes = 2;
        } else if ((leadByte & 0xF0) == 0xE0) {
            symbolBytes = 3;
        } else if ((leadByte & 0xF8) == 0xF0) {
            symbolBytes = 4;
        }
        if (byteIndex + symbolBytes > inputString.length()) {
            symbolBytes = 1;
        }
        displaySymbols[symbolCount++] = inputString.substring(byteIndex, byteIndex + symbolBytes);
        byteIndex += symbolBytes;
    }

    if (centering) {
        int totalPadding = numModules - symbolCount;
        int paddingLeft = totalPadding / 2;
        for (int i = symbolCount - 1; i >= 0; i--) {
            displaySymbols[i + paddingLeft] = displaySymbols[i];
        }
        for (int i = 0; i < paddingLeft; i++) {
            displaySymbols[i] = " ";
        }
        symbolCount += paddingLeft;
    }
    while (symbolCount < numModules) {
        displaySymbols[symbolCount++] = " ";
    }

    int targetPositions[numModules];
    String displayString;
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(displaySymbols[i]);
        displayString += displaySymbols[i];
    }
    moveTo(targetPositions, speed);

    if (mqtt && mqtt->isConnected()) {
        mqtt->publishState(displayString);
    }
}

void SplitFlapDisplay::calibrateModules(int targetPositions[]) {
    // Split the four-step accuracy budget across the samples required to
    // confirm a sensor transition. At 2048 steps this derives 11.72 RPM, then
    // caps at 10 RPM to leave timing and I2C overhead margin.
    float stepsPerPoll =
        (float) CALIBRATION_ACCURACY_STEPS / CALIBRATION_CONFIRMATION_SAMPLES;
    float speed = (stepsPerPoll * 60.0f * 1000000.0f) /
        (stepsPerRot * CALIBRATION_SENSOR_CHECK_INTERVAL_US);
    speed = min(speed, min(maxVel, CALIBRATION_MAX_RPM));
    float stepsPerSecond = (speed / 60.0f) * stepsPerRot;
    float timePerStep = 1000000 / stepsPerSecond;

    calibrateAllModules(timePerStep, targetPositions);
}

void SplitFlapDisplay::calibrateAllModules(float timePerStep, int targetPositions[]) {
    const int startStopDelayMs = 200;

    bool calibrated[MAX_MODULES] = {};
    bool targetReached[MAX_MODULES] = {};
    bool sensorArmed[MAX_MODULES] = {};
    uint8_t consecutiveLowSamples[MAX_MODULES] = {};
    uint8_t consecutiveHighSamples[MAX_MODULES] = {};
    unsigned long lastStepTimes[MAX_MODULES] = {};
    unsigned long currentTime = micros();
    unsigned long lastSensorCheckTime = currentTime;
    int connectedModules = 0;
    int modulesInProgress = 0;

    for (int i = 0; i < numModules; i++) {
        if (! moduleConnected[i]) {
            calibrated[i] = true;
            continue;
        }
        lastStepTimes[i] = currentTime;
        modules[i].start();
        connectedModules++;
        modulesInProgress++;
    }

    if (connectedModules == 0) {
        Serial.println("No connected modules to calibrate");
        return;
    }

    delay(startStopDelayMs);

    // There is intentionally no rotation or time limit here. A module with a
    // missed or failed sensor keeps revolving, making the failure visible
    // instead of silently continuing with an uncalibrated position.
    while (modulesInProgress > 0) {
        currentTime = micros();

        for (int i = 0; i < numModules; i++) {
            if (! moduleConnected[i] || targetReached[i] || (currentTime - lastStepTimes[i]) <= timePerStep) {
                continue;
            }

            if (! calibrated[i]) {
                modules[i].step();
                lastStepTimes[i] = micros();
            } else if (targetPositions != nullptr) {
                modules[i].step();
                lastStepTimes[i] = micros();
                if (modules[i].getPosition() == targetPositions[i]) {
                    targetReached[i] = true;
                    modulesInProgress--;
                    modules[i].stop();
                }
            }
        }

        if ((currentTime - lastSensorCheckTime) >= CALIBRATION_SENSOR_CHECK_INTERVAL_US) {
            for (int i = 0; i < numModules; i++) {
                if (calibrated[i]) {
                    continue;
                }

                bool sensorHigh;
                if (! modules[i].readHallEffectSensor(sensorHigh)) {
                    continue;
                }

                if (! sensorArmed[i]) {
                    if (! sensorHigh) {
                        consecutiveLowSamples[i]++;
                        if (consecutiveLowSamples[i] >= CALIBRATION_CONFIRMATION_SAMPLES) {
                            sensorArmed[i] = true;
                            consecutiveHighSamples[i] = 0;
                        }
                    } else {
                        consecutiveLowSamples[i] = 0;
                    }
                } else if (sensorHigh) {
                    consecutiveHighSamples[i]++;
                    if (consecutiveHighSamples[i] >= CALIBRATION_CONFIRMATION_SAMPLES) {
                        modules[i].magnetDetected();
                        calibrated[i] = true;
                        if (targetPositions == nullptr) {
                            targetReached[i] = true;
                            modulesInProgress--;
                        } else if (modules[i].getPosition() == targetPositions[i]) {
                            targetReached[i] = true;
                            modulesInProgress--;
                            modules[i].stop();
                        }
                    }
                } else {
                    consecutiveHighSamples[i] = 0;
                }
            }
            lastSensorCheckTime = currentTime;
            yield();
        }
    }

    delay(startStopDelayMs);
    for (int i = 0; i < numModules; i++) {
        if (moduleConnected[i]) {
            modules[i].stop();
        }
    }
}

void SplitFlapDisplay::moveTo(int targetPositions[], float speed, bool releaseMotors) {
    speed = constrain(speed, 2, maxVel);
    float stepsPerSecond = (speed / 60) * stepsPerRot;
    float timePerStep = 1000000 / stepsPerSecond;

    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = constrain(
            targetPositions[i],
            0,
            stepsPerRot - 1
        );
    }

    moveAllModulesTo(targetPositions, timePerStep, releaseMotors);
}

void SplitFlapDisplay::moveAllModulesTo(int targetPositions[], float timePerStep, bool releaseMotors) {
    const int startStopDelayMs = 200;

    bool needsStepping[MAX_MODULES] = {};
    bool resetLatches[MAX_MODULES] = {};
    bool startedMotors[MAX_MODULES] = {};
    unsigned long lastStepTimes[MAX_MODULES] = {};
    unsigned long currentTime = micros();
    unsigned long lastSensorCheckTime = currentTime;
    int movingModules = 0;

    for (int i = 0; i < numModules; i++) {
        if (! moduleConnected[i]) {
            continue;
        }
        if (modules[i].getPosition() == targetPositions[i]) {
            continue;
        }

        needsStepping[i] = true;
        resetLatches[i] = true; // Ignore a magnet already over the sensor at startup.
        startedMotors[i] = true;
        lastStepTimes[i] = currentTime;
        movingModules++;
        modules[i].start();
    }

    if (movingModules == 0) {
        return;
    }

    delay(startStopDelayMs);

    while (movingModules > 0) {
        currentTime = micros();

        for (int i = 0; i < numModules; i++) {
            if (needsStepping[i] && (currentTime - lastStepTimes[i]) > timePerStep) {
                modules[i].step();
                lastStepTimes[i] = micros();
                if (modules[i].getPosition() == targetPositions[i]) {
                    needsStepping[i] = false;
                    movingModules--;
                }
            }
        }

        if ((currentTime - lastSensorCheckTime) >= MOVEMENT_SENSOR_CHECK_INTERVAL_US) {
            for (int i = 0; i < numModules; i++) {
                if (! needsStepping[i]) {
                    continue;
                }

                bool sensorHigh;
                if (! modules[i].readHallEffectSensor(sensorHigh)) {
                    continue;
                }

                if (sensorHigh) {
                    if (! resetLatches[i]) {
                        modules[i].magnetDetected();
                        resetLatches[i] = true;
                    }
                } else if (resetLatches[i]) {
                    resetLatches[i] = false;
                }
            }
            lastSensorCheckTime = currentTime;
            yield();
        }
    }

    if (releaseMotors) {
        delay(startStopDelayMs);
        for (int i = 0; i < numModules; i++) {
            if (startedMotors[i]) {
                modules[i].stop();
            }
        }
    }
}

void SplitFlapDisplay::setMqtt(SplitFlapMqtt *mqttHandler) {
    mqtt = mqttHandler;
}
