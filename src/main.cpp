/*
  shotStopper

  Use an acaia or other compatible scale to brew by weight with an espresso machine

  Immediately Connects to a nearby acaia scale,
  tares the scale when the "in" gpio is triggered (active low),
  and then triggers the "out" gpio to stop the shot once ( goalWeight - weightOffset ) is achieved.

  Tested on a Acaia Pyxis, Arduino nano ESP32, and La Marzocco GS3.

  To set the Weight over BLE, use a BLE app such as LightBlue to connect
  to the "shotStopper" BLE device and read/write to the weight characteristic,
  otherwise the weight is defaulted to 36g.

  Created by Tate Mazer, 2023.

  Released under the MIT license.

  https://github.com/tatemazer/AcaiaArduinoBLE

*/

#include <AcaiaArduinoBLE.h>
#include <NimBLEDevice.h>
#include "Config.h"
#include "Logger.h"

// Compile-time constants (not configurable)
#define BUTTON_READ_PERIOD_MS     5     // Button debounce sampling period
#define MAX_SHOT_DATAPOINTS       1000  // Maximum number of weight/time measurements per shot
#define N 10                            // Number of datapoints used to calculate trend line

// Runtime configuration variables (loaded from config)
float maxOffset;
int brewPulseDuration;
float dripDelay;
float reedSwitchDelay;
float minWeightForPrediction;
float minShotDuration; // From brew.target_time min value
float maxShotDuration; // From brew.target_time max value
float targetTime;      // Target brew time when scale disconnected or brew.by_time_only is true

// Configuration system
Config config;

// Configuration variables (will be loaded from config system)
bool momentary;
bool reedSwitch;
bool autoTare;
bool brewByTimeOnly;
bool brewByTimeOnlyConfigured; // The configured value from config system

// Board Hardware
#if defined (ARDUINO_ESP32S3_DEV)
    #define LED_RED     46
    #define LED_GREEN   47
    #define LED_BLUE    45
    #define IN          21
    #define OUT         38
    #define REED_IN     18
#elif defined (ARDUINO_ESP32C3_DEV)
    #define LED_RED     21
    #define LED_BLUE    10
    #define LED_GREEN   20
    #define IN          8
    #define OUT         6
    #define REED_IN     7
#endif

#define BUTTON_STATE_ARRAY_LENGTH 31

typedef enum {BUTTON, WEIGHT, TIME, DISCONNECT, UNDEF} ENDTYPE;

// RGB Colors {Red,Green,Blue}
// Using COLOR_ prefix to avoid conflicts with framework-defined macros
int COLOR_RED[3] = {255, 0, 0};
int COLOR_GREEN[3] = {0, 255, 0};
int COLOR_BLUE[3] = {0, 0, 255};
int COLOR_MAGENTA[3] = {255, 0, 255};
int COLOR_CYAN[3] = {0, 255, 255};
int COLOR_YELLOW[3] = {255, 255, 0};
int COLOR_WHITE[3] = {255, 255, 255};
int COLOR_OFF[3] = {0, 0, 0};
int currentColor[3] = {-1, -1, -1};

AcaiaArduinoBLE scale(false);
float currentWeight = 0;
float goalWeight = 0;
float weightOffset = 0;
float error = 0;
int buttonArr[BUTTON_STATE_ARRAY_LENGTH] = {};  // last readings of the button (initialized to 0)

// Button
int in = reedSwitch ? REED_IN : IN;
bool buttonPressed = false; // physical status of button
bool buttonLatched = false; // electrical status of button
unsigned long lastButtonRead_ms = 0;
int newButtonState = 0;

struct Shot {
    float start_timestamp_s; // Relative to runtime
    float shotTimer;         // Reset when the final drip measurement is made
    float end_s;             // Number of seconds after the shot started
    float expected_end_s;    // Estimated duration of the shot
    float weight[1000];      // A scatter plot of the weight measurements, along with time_s[]
    float time_s[1000];      // Number of seconds after the shot starte
    int datapoints;          // Number of datapoitns in the scatter plot
    bool brewing;            // True when actively brewing, otherwise false
    ENDTYPE end;
};

// Initialize shot
Shot shot = {0.0f, 0.0f, 0.0f, 0.0f, {}, {}, 0, false, UNDEF};

float lastReadWeight = 0;

// BLE peripheral device (NimBLE server)
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pWeightCharacteristic = nullptr;
bool deviceConnected = false;
uint8_t lastWrittenWeight = 0;

// Callback class for BLE server connection events
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServerCallback, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        LOG(INFO, "BLE client connected to shotStopper");

        // Allow multiple connections
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer* pServerCallback, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        LOG(INFO, "BLE client disconnected from shotStopper");
        NimBLEDevice::startAdvertising();
    }
};

// Callback class for characteristic write events
class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        if (const std::string value = pCharacteristic->getValue(); !value.empty()) {
            lastWrittenWeight = static_cast<uint8_t>(value[0]);
        }
    }
};

// Forward declarations
void setColor(int rgb[3]);
void updateLEDState();
void setBrewingState(bool brewing);
float seconds_f();
void calculateEndTime(Shot* s);

void setup() {
    Serial.begin(115200);
    Logger::init(23);

    delay(500);

    // Initialize configuration system
    if (!config.begin()) {
        LOG(ERROR, "Failed to initialize config system!");
        // Continue with defaults that are already in Config
    }

    // Load configuration values
    goalWeight = static_cast<float>(config.get<double>("brew.goal_weight"));
    weightOffset = static_cast<float>(config.get<double>("brew.weight_offset"));
    maxOffset = static_cast<float>(config.get<double>("brew.max_offset"));
    brewPulseDuration = config.get<int>("brew.pulse_duration_ms");
    dripDelay = static_cast<float>(config.get<double>("brew.drip_delay"));
    reedSwitchDelay = static_cast<float>(config.get<double>("brew.reed_switch_delay"));
    minWeightForPrediction = static_cast<float>(config.get<double>("scale.min_weight_for_prediction"));
    momentary = config.get<bool>("switch.momentary");
    reedSwitch = config.get<bool>("switch.reedcontact");
    autoTare = config.get<bool>("scale.auto_tare");
    brewByTimeOnlyConfigured = config.get<bool>("brew.by_time_only");
    brewByTimeOnly = brewByTimeOnlyConfigured; // Initial value, will be updated based on scale connection

    // Load target time and shot duration limits from brew.target_time
    targetTime = static_cast<float>(config.get<int>("brew.target_time"));
    
    if (const ConfigDef* targetTimeDef = config.getConfigDef("brew.target_time")) {
        minShotDuration = static_cast<float>(targetTimeDef->minValue);
        maxShotDuration = static_cast<float>(targetTimeDef->maxValue);
    }
    else {
        // Fallback to defaults if not found
        minShotDuration = 3.0f;
        maxShotDuration = 60.0f;
        LOG(WARNING, "brew.target_time ConfigDef not found, using defaults");
    }

    // Set log level from config
    int logLevelValue = config.get<int>("system.log_level");
    Logger::setLevel(static_cast<Logger::Level>(logLevelValue));

    LOG(INFO, "Configuration loaded:");
    LOGF(INFO, "  Goal Weight: %.1fg", goalWeight);
    LOGF(INFO, "  Weight Offset: %.1fg", weightOffset);
    LOGF(INFO, "  Max Offset: %.1fg", maxOffset);
    LOGF(INFO, "  Shot Duration: %.1fs - %.1fs", minShotDuration, maxShotDuration);
    LOGF(INFO, "  Target Time: %.1fs", targetTime);
    LOGF(INFO, "  Pulse Duration: %dms", brewPulseDuration);
    LOGF(INFO, "  Drip Delay: %.1fs", dripDelay);
    LOGF(INFO, "  Reed Switch Delay: %.1fs", reedSwitchDelay);
    LOGF(INFO, "  Min Weight for Prediction: %.1fg", minWeightForPrediction);
    LOGF(INFO, "  Momentary: %s", momentary ? "true" : "false");
    LOGF(INFO, "  Reed Switch: %s", reedSwitch ? "true" : "false");
    LOGF(INFO, "  Auto Tare: %s", autoTare ? "true" : "false");
    LOGF(INFO, "  Brew By Time Only: %s", brewByTimeOnly ? "true" : "false");
    LOGF(INFO, "  Log Level: %d", logLevelValue);

    // Initialize the GPIO hardware
    pinMode(in, INPUT_PULLUP);
    pinMode(OUT, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setColor(COLOR_OFF);

    // Initialize the BLE hardware using NimBLE
    NimBLEDevice::init("shotStopper");

    // Create BLE Server
    pServer = NimBLEDevice::createServer();
    static ServerCallbacks serverCallbacks;
    pServer->setCallbacks(&serverCallbacks);

    // Create BLE Service
    NimBLEService* pService = pServer->createService("0FFE");

    // Create BLE Characteristic
    pWeightCharacteristic = pService->createCharacteristic("FF11", READ | WRITE);
    static CharacteristicCallbacks characteristicCallbacks;
    pWeightCharacteristic->setCallbacks(&characteristicCallbacks);
    const auto initWeight = static_cast<uint8_t>(goalWeight);
    pWeightCharacteristic->setValue(&initWeight, 1);

    // Start the service
    pService->start();

    // Start advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("0FFE");
    pAdvertising->setName("shotStopper");
    pAdvertising->start();

    LOG(INFO, "Bluetooth® device active, waiting for connections...");
}

void loop() {
    // Update brewByTimeOnly based on scale connection status
    // If configured as false, use time-only mode when scale is disconnected
    if (!brewByTimeOnlyConfigured) {
        brewByTimeOnly = !scale.isConnected();
    }
    // If configured as true, always use time-only mode
    else {
        brewByTimeOnly = true;
    }

    // Connect to scale using non-blocking approach
    if (!scale.isConnected()) {
        // Start connection process if not already connecting
        if (!scale.isConnecting()) {
            scale.init();
            currentWeight = 0;

            // Only stop brewing if not brewing by time
            if (shot.brewing && !brewByTimeOnly) {
                shot.brewing = false;
                shot.end = DISCONNECT;
                setBrewingState(false);
            }
        }

        // Update connection state machine
        scale.updateConnection();
    }

    // Check for setpoint updates (NimBLE handles this via callbacks)
    if (lastWrittenWeight != 0 && lastWrittenWeight != static_cast<uint8_t>(goalWeight)) {
        LOGF(INFO, "Goal weight updated from %.1f to %d", goalWeight, lastWrittenWeight);
        goalWeight = lastWrittenWeight;

        // Save to config system
        config.set<double>("brew.goal_weight", goalWeight);

        if (!config.save()) {
            LOG(ERROR, "Failed to save config after goal weight update");
        }

        // Update the characteristic value
        const auto newWeight = static_cast<uint8_t>(goalWeight);
        pWeightCharacteristic->setValue(&newWeight, 1);
    }

    // Send a heartbeat message to the scale periodically to maintain connection
    if (scale.isConnected() && scale.heartbeatRequired()) {
        scale.heartbeat();
    }

    // always call newWeightAvailable to actually receive the datapoint from the scale,
    // otherwise getWeight() will return stale data
    if (scale.isConnected() && scale.newWeightAvailable()) {
        currentWeight = scale.getWeight();

        if (currentWeight != lastReadWeight) {
            LOGF(DEBUG, "Weight: %.1fg", currentWeight);
            lastReadWeight = currentWeight;
        }

        // update shot trajectory
        if (shot.brewing && shot.datapoints < MAX_SHOT_DATAPOINTS) {
            shot.time_s[shot.datapoints] = seconds_f() - shot.start_timestamp_s;
            shot.weight[shot.datapoints] = currentWeight;
            shot.shotTimer = shot.time_s[shot.datapoints];
            shot.datapoints++;

            // get the likely end time of the shot
            calculateEndTime(&shot);
            LOGF(TRACE, "Shot: %.1fs | Expected end: %.1fs", shot.shotTimer, shot.expected_end_s);
        }
    }
    // Update timer if brewing without scale (Time Mode)
    else if (shot.brewing && !scale.isConnected()) {
        shot.shotTimer = seconds_f() - shot.start_timestamp_s;

        static unsigned long lastTimeModePrint = 0;

        if (millis() - lastTimeModePrint > 500) {
            lastTimeModePrint = millis();
            LOGF(DEBUG, "Time mode: %.1fs", shot.shotTimer);
        }
    }

    // Read button every period
    if (millis() - lastButtonRead_ms > BUTTON_READ_PERIOD_MS) {
        lastButtonRead_ms = millis();

        // Push back for new entry
        for (int i = BUTTON_STATE_ARRAY_LENGTH - 2; i >= 0; i--) {
            buttonArr[i + 1] = buttonArr[i];
        }

        buttonArr[0] = !digitalRead(in); // Active Low

        // Only return 1 if contains 1
        // Also assume the button is off for a few milliseconds
        // after the shot is done, there can be residual noise
        // from the reed switch
        newButtonState = 0;

        for (const int i : buttonArr) {
            if (i) {
                newButtonState = 1;
            }

            // Serial.print(buttonArr[i]);
            // Serial.println();
        }

        // The reed switch measurements require a small amount of delay for accuracy.
        // if the shot just stopped, assume that the reed switch should read "open" for the delay period
        if (reedSwitch && !shot.brewing && seconds_f() < shot.start_timestamp_s + shot.end_s + reedSwitchDelay) {
            // Serial.println("force reedSwitch Off");
            newButtonState = 0;
        }
    }

    // SHOT INITIATION EVENTS

    // button just pressed (and released)
    if (newButtonState && buttonPressed == false) {
        LOG(INFO, "Button pressed");
        buttonPressed = true;

        if (reedSwitch) {
            shot.brewing = true;
            setBrewingState(shot.brewing);
        }
    }

    // button held. Take over for the rest of the shot.
    else if (!momentary
                       && shot.brewing
                       && !buttonLatched
                       && (shot.shotTimer > minShotDuration)) {
        buttonLatched = true;
        LOG(INFO, "Button latched");
        digitalWrite(OUT, HIGH);
        LOG(DEBUG, "Output HIGH");

        // Get the scale to beep to inform user.
        if (autoTare) {
            scale.tare();
        }
    }

    // SHOT COMPLETION EVENTS

    // button released
    else if (!buttonLatched && !newButtonState && buttonPressed == true) {
        LOG(INFO, "Button released");
        buttonPressed = false;
        shot.brewing = !shot.brewing;

        if (!shot.brewing) {
            shot.end = BUTTON;
        }

        setBrewingState(shot.brewing);
    }

    // Max duration reached
    else if (shot.brewing
        && shot.shotTimer > maxShotDuration)
    {
        shot.brewing = false;
        LOG(WARNING, "Max brew duration reached");
        shot.end = TIME;
        setBrewingState(shot.brewing);
    }

    // Brew by time (Scale disconnected or brew by time only mode)
    else if (shot.brewing
        && (!scale.isConnected() || brewByTimeOnly)
        && shot.shotTimer >= targetTime)
    {
        LOGF(INFO, "Target brew time reached: %.1fs", targetTime);
        shot.brewing = false;
        shot.end = TIME;
        setBrewingState(shot.brewing);
    }

    // End shot by weight (only if not in time-only mode)
    if (scale.isConnected()
        && !brewByTimeOnly
        && shot.brewing
        && shot.shotTimer >= shot.expected_end_s
        && shot.shotTimer > minShotDuration)
    {
        LOGF(INFO, "Weight achieved. Timer: %.1fs | Expected: %.1fs", shot.shotTimer, shot.expected_end_s);
        shot.brewing = false;
        shot.end = WEIGHT;
        setBrewingState(shot.brewing);
    }

    // Update LED state continuously (needed for blinking during brewing)
    updateLEDState();

    // SHOT ANALYSIS  --------------------------------

    // Detect error of shot
    if (scale.isConnected()
        && static_cast<bool>(shot.start_timestamp_s)
        && static_cast<bool>(shot.end_s)
        && currentWeight >= goalWeight - weightOffset
        && seconds_f() > shot.start_timestamp_s + shot.end_s + dripDelay
    ) {
        shot.start_timestamp_s = 0;
        shot.end_s = 0;

        const float newOffset = weightOffset + (currentWeight - goalWeight);

        if (abs(currentWeight - goalWeight + weightOffset) > maxOffset) {
            LOGF(WARNING, "Final weight: %.1fg | Goal: %.1fg | Offset: %.1fg | Error assumed, offset unchanged",
                currentWeight, goalWeight, weightOffset);
        }
        else if (newOffset < 0) {
            LOGF(WARNING, "Final weight: %.1fg | Goal: %.1fg | Offset: %.1fg | Negative offset would result, offset unchanged",
                currentWeight, goalWeight, weightOffset);
        }
        else {
            weightOffset = newOffset;
            LOGF(INFO, "Final weight: %.1fg | Goal: %.1fg | New offset: %.1fg",
                currentWeight, goalWeight, weightOffset);

            // Save to config system
            config.set<double>("brew.weight_offset", weightOffset);
            if (!config.save()) {
                LOG(ERROR, "Failed to save config after offset update");
            }
        }
    }
}

void setBrewingState(const bool brewing) {
    if (brewing) {
        LOG(INFO, "Shot started");
        shot.start_timestamp_s = seconds_f();
        shot.shotTimer = 0.0f;
        shot.datapoints = 0;
        shot.expected_end_s = maxShotDuration; // Initialize to max duration

        if (scale.isConnected()) {
            scale.resetTimer();

            if (autoTare) {
                scale.tare();
            }

            scale.startTimer();
            LOG(DEBUG, "Waiting for weight data...");
        }
        else {
            LOG(INFO, "Shot started (Time Mode)");
        }
    }
    else {
        const char* endReason = "unknown";

        switch (shot.end) {
            case TIME:
                endReason = "time";
                break;
            case WEIGHT:
                endReason = "weight";
                break;
            case BUTTON:
                endReason = "button";
                break;
            case DISCONNECT:
                endReason = "disconnect";
                break;
            case UNDEF:
                endReason = "undefined";
                break;
        }

        LOGF(INFO, "Shot ended by %s", endReason);

        shot.end_s = seconds_f() - shot.start_timestamp_s;
        scale.stopTimer();

        if (momentary
            && (WEIGHT == shot.end || TIME == shot.end))
        {
            // Pulse button to stop brewing
            digitalWrite(OUT, HIGH);
            LOG(DEBUG, "Output HIGH");
            delay(brewPulseDuration);
            digitalWrite(OUT, LOW);
            LOG(DEBUG, "Output LOW");
            buttonPressed = false;
        }
        else if (!momentary) {
            buttonLatched = false;
            buttonPressed = false;
            LOG(DEBUG, "Button unlatched");
            digitalWrite(OUT, LOW);
            LOG(DEBUG, "Output LOW");
        }
    }

    // Reset
    shot.end = UNDEF;
}

void calculateEndTime(Shot* s) {
    // Do not predict end time if there aren't enough espresso measurements yet
    if ( s->datapoints < N || s->weight[s->datapoints - 1] < minWeightForPrediction ) {
        s->expected_end_s = maxShotDuration;
    }
    else {
        // Get line of best fit (y=mx+b) from the last 10 measurements
        float sumXY = 0, sumX = 0, sumY = 0, sumSquaredX = 0, m = 0, b = 0, meanX = 0, meanY = 0;

        for (int i = s->datapoints - N; i < s->datapoints; i++) {
            sumXY += s->time_s[i] * s->weight[i];
            sumX += s->time_s[i];
            sumY += s->weight[i];
            sumSquaredX += s->time_s[i] * s->time_s[i];
        }

        m = (N * sumXY - sumX * sumY) / (N * sumSquaredX - sumX * sumX);
        meanX = sumX / N;
        meanY = sumY / N;
        b = meanY - m * meanX;

        // Calculate time at which goal weight will be reached (x = (y-b)/m)
        // if M is negative (which can happen during a blooming shot when the flow stops) assume max duration (issue #29)
        s->expected_end_s = m < 0 ? maxShotDuration : (goalWeight - weightOffset - b) / m;
    }
}

float seconds_f() {
    return static_cast<float>(millis()) / 1000.0f;
}

void setColor(int rgb[3]) {
    // Prevent flickering by only updating if color changed
    if (currentColor[0] == rgb[0] && 
        currentColor[1] == rgb[1] && 
        currentColor[2] == rgb[2]) {
        return;
    }

    analogWrite(LED_RED, 255 - rgb[0]);
    analogWrite(LED_GREEN, 255 - rgb[1]);
    analogWrite(LED_BLUE, 255 - rgb[2]);

    currentColor[0] = rgb[0];
    currentColor[1] = rgb[1];
    currentColor[2] = rgb[2];
}

void updateLEDState() {
    if (shot.brewing) {
        if (scale.isConnected()) {
            setColor(millis() / 1000 % 2 ? COLOR_GREEN : COLOR_BLUE);
        }
        else {
            setColor(millis() / 1000 % 2 ? COLOR_RED : COLOR_BLUE);
        }
    }
    else if (!scale.isConnected()) {
        if (scale.isConnecting() && !NimBLEDevice::getScan()->isScanning()) {
            setColor(COLOR_YELLOW);
        }
        else {
            setColor(COLOR_RED);
        }
    }
    else {
        setColor(COLOR_GREEN);
    }
}