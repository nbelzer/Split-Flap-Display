#include "SplitFlapModule.h"

// Array of characters, in order, the first item is located on the magnet on the
// character drum
const char *const SplitFlapModule::StandardChars[37] = {" ", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
                                                 "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y",
                                                 "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

// "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$&()-+=;:'\"%,.?♥/1234567890⬛⬜🟥🟧🟨🟩🟦🟪"
const char *const SplitFlapModule::ExtendedChars[64] = {
  "A", "B", "C", "D", "E",  "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U",  "V", "W", "X", "Y", "Z", "!", "@", "#", "$", "&", "(", ")", "-", "+", "=", ";", ":", "'", "\"", "%", ",", ".", "?", "♥", "/", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", " ", "⬜", "🟥", "🟧", "🟨", "🟩", "🟦", "🟪" };

// Default Constructor
SplitFlapModule::SplitFlapModule()
    : address(0), position(0), stepNumber(0), stepsPerRot(0), chars(StandardChars), numChars(37) {
    magnetPosition = 710;
}

// Constructor implementation
SplitFlapModule::SplitFlapModule(
    uint8_t I2Caddress, int stepsPerFullRotation, int stepOffset, int magnetPos, int charsetSize
)
    : address(I2Caddress), position(0), stepNumber(0), stepsPerRot(stepsPerFullRotation) {
    magnetPosition = magnetPos + stepOffset;

    chars = (charsetSize == 64) ? ExtendedChars : StandardChars;
    numChars = (charsetSize == 64) ? 64 : 37;
}

bool SplitFlapModule::writeIO(uint16_t data) {
    Wire.beginTransmission(address);
    Wire.write(data & 0xFF);        // Send lower byte
    Wire.write((data >> 8) & 0xFF); // Send upper byte

    byte error = Wire.endTransmission();

    if (error > 0 && ! hasErrored) {
        Serial.print("Error writing data to module ");
        Serial.print(address);
        Serial.print(", error code: ");
        Serial.println(error); // Error codes:
        // 0 = success
        // 1 = data too long to fit in transmit buffer
        // 2 = received NACK on transmit of address
        // 3 = received NACK on transmit of data
        // 4 = other error
    }

    hasErrored = error > 0;
    return error == 0;
}

bool SplitFlapModule::probe() {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    hasErrored = error != 0;
    return error == 0;
}

// Init Module, Setup IO Board
void SplitFlapModule::init() {
    float stepSize = (float) stepsPerRot / (float) numChars;
    float currentPosition = 0;
    for (int i = 0; i < numChars; i++) {
        charPositions[i] = (int) currentPosition;
        currentPosition += stepSize;
    }

    uint16_t initState = 0b1111111111100001; // Pin 15 (17) as INPUT, Pins 1-4 as OUTPUT
    writeIO(initState);

    stop();                                  // Write all motor coil inputs LOW

    int initDelay = 100;

    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);
    step();
    delay(initDelay);

    stop();
}

int SplitFlapModule::getCharPosition(const String &inputChar) const {
    String normalized = inputChar;
    normalized.toUpperCase();

    for (int i = 0; i < numChars; i++) {
        if (normalized == chars[i]) {
            return charPositions[i];
        }
    }

    // Unsupported input must resolve to the actual blank flap. In the extended
    // set blank is not at index zero.
    for (int i = 0; i < numChars; i++) {
        if (strcmp(chars[i], " ") == 0) {
            return charPositions[i];
        }
    }
    return 0;
}

void SplitFlapModule::stop() {
    uint16_t stepState = 0b1111111111100001;
    writeIO(stepState);
}

void SplitFlapModule::start() {
    // Re-energize the last physical coil phase without changing which phase
    // the next counted step will use. Leaving stepNumber decremented makes the
    // next step update position without actually advancing the motor.
    stepNumber = (stepNumber + 3) % 4;
    step(false);
    stepNumber = (stepNumber + 1) % 4;
}

void SplitFlapModule::step(bool updatePosition) {
    uint16_t stepState;
    switch (stepNumber) {
        case 0:
            stepState = 0b1111111111100111;
            break;
        case 1:
            stepState = 0b1111111111110011;
            break;
        case 2:
            stepState = 0b1111111111111001;
            break;
        case 3:
            stepState = 0b1111111111101101;
            break;
    }

    bool writeSucceeded = writeIO(stepState);
    if (updatePosition && writeSucceeded) {
        position = (position + 1) % stepsPerRot;
        stepNumber = (stepNumber + 1) % 4;
    }
}

bool SplitFlapModule::readHallEffectSensor(bool &sensorHigh) {
    uint8_t requestBytes = 2;
    uint8_t receivedBytes = Wire.requestFrom(address, requestBytes);
    if (receivedBytes == requestBytes && Wire.available() >= requestBytes) {
        uint16_t inputState = 0;

        // Read the two bytes and combine them into a 16-bit value
        inputState = Wire.read();             // Read the lower byte
        inputState |= (Wire.read() << 8);     // Read the upper byte and shift it left

        sensorHigh = (inputState & (1 << 15)) != 0;
        hasErrored = false;
        return true;
    }

    while (Wire.available()) {
        Wire.read();
    }
    hasErrored = true;
    return false;
}
