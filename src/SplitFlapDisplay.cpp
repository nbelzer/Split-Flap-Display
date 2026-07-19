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

    for (uint8_t i = 0; i < numModules; i++) {
        modules[i].init();
    }
}

void SplitFlapDisplay::testAll() {
    char testChars[37] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
                          'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    int numChars = sizeof(testChars) / sizeof(testChars[0]);
    int targetPositions[numModules];

    int charPos;
    for (int i = 0; i < numChars; i++) {
        // Serial.print("Target Positions: [");
        // fill array with same char

        for (int j = 0; j < numModules; j++) {
            targetPositions[j] = modules[j].getCharPosition(testChars[i]);
            // Serial.print(targetPositions[j]);
            // Serial.print(" , ");
        }
        // Serial.println("]");

        moveTo(targetPositions);
        delay(500);
    }
}

void SplitFlapDisplay::testRandom(float speed) {
    char testChars[37] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
                          'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    int targetPositions[numModules];
    char randChar;

    Serial.print("Target: ");
    for (int i = 0; i < numModules; i++) {
        randChar = testChars[random(0, 37)];
        targetPositions[i] = modules[i].getCharPosition(randChar);
        Serial.print(randChar);
    }
    Serial.println(" ");
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::testCount() {
    int count = 0;
    int maxCount = pow(10, numModules);
    char targetChar;
    int targetInteger;

    int targetPositions[numModules];

    for (int i = 0; i < maxCount; i++) {
        // get each character in the count integer
        for (int j = 0; j < numModules; j++) {
            targetInteger = (i % (int) pow(10, j + 1)) / (int) pow(10, j);
            targetChar = targetInteger + '0'; // convert to char
            targetPositions[numModules - j - 1] = modules[j].getCharPosition(targetChar);
        }

        moveTo(targetPositions);
        delay(250);
    }
}

void SplitFlapDisplay::home(float speed) {
    Serial.println("Homing");
    calibrateModules();

    int targetPositions[numModules];
    char homeChar = ' ';
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::homeToString(String homeString, float speed, bool centering) {
    Serial.println("Homing");
    calibrateModules();
    writeString(homeString, speed, centering);
}

void SplitFlapDisplay::homeToChar(char homeChar, float speed) {
    Serial.println("Homing");
    calibrateModules();

    int targetPositions[numModules];
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(homeChar);
    }
    moveTo(targetPositions, speed);
}

void SplitFlapDisplay::writeChar(char inputChar, float speed) {
    int targetPositions[numModules];
    // Iterate through the input string and process each character
    for (int i = 0; i < numModules; i++) {
        targetPositions[i] = modules[i].getCharPosition(inputChar);
    }
    moveTo(targetPositions, speed);
}

String sanitizeInput(const String &input) {
    String sanitized = input;

    // Replace problematic characters
    sanitized.replace("'", "'\\'");
    sanitized.replace("%", "%%");

    return sanitized;
}

void SplitFlapDisplay::writeString(String inputString, float speed, bool centering) {
    inputString = sanitizeInput(inputString);
    String displayString = inputString.substring(0, numModules);

    if (centering) {
        int totalPadding = numModules - displayString.length();
        int paddingLeft = totalPadding / 2;
        int paddingRight = totalPadding - paddingLeft;

        // Add padding to the left
        String result = "";
        for (int i = 0; i < paddingLeft; i++) {
            result += " ";
        }

        // Add the original string
        result += displayString;

        // Add padding to the right
        for (int i = 0; i < paddingRight; i++) {
            result += " ";
        }
        displayString = result;
    } else {                                          // pad blanks to end, if no centering
        while (displayString.length() < numModules) { // Pad with spaces
            displayString += " ";                     // Padding with space
        }
    }

    int targetPositions[numModules];
    // Iterate through the input string and process each character
    for (int i = 0; i < displayString.length(); i++) {
        char currentChar = displayString[i];
        // Serial.println(currentChar);
        targetPositions[i] = modules[i].getCharPosition(currentChar);
    }
    moveTo(targetPositions, speed);

    if (mqtt && mqtt->isConnected()) {
        mqtt->publishState(displayString);
    }
}

void SplitFlapDisplay::calibrateModules() {
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

    calibrateAllModules(timePerStep);
}

void SplitFlapDisplay::calibrateAllModules(float timePerStep) {
    const int startStopDelayMs = 200;

    bool calibrated[MAX_MODULES] = {};
    bool sensorArmed[MAX_MODULES] = {};
    uint8_t consecutiveLowSamples[MAX_MODULES] = {};
    uint8_t consecutiveHighSamples[MAX_MODULES] = {};
    unsigned long lastStepTimes[MAX_MODULES] = {};
    unsigned long currentTime = micros();
    unsigned long lastSensorCheckTime = currentTime;
    int modulesAwaitingMagnet = numModules;

    for (int i = 0; i < numModules; i++) {
        lastStepTimes[i] = currentTime;
        modules[i].start();
    }

    delay(startStopDelayMs);

    // There is intentionally no rotation or time limit here. A module with a
    // missed or failed sensor keeps revolving, making the failure visible
    // instead of silently continuing with an uncalibrated position.
    while (modulesAwaitingMagnet > 0) {
        currentTime = micros();

        for (int i = 0; i < numModules; i++) {
            if (! calibrated[i] && (currentTime - lastStepTimes[i]) > timePerStep) {
                modules[i].step();
                lastStepTimes[i] = micros();
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
                        modulesAwaitingMagnet--;
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
        modules[i].stop();
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
