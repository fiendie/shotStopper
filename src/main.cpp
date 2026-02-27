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

#include <WiFi.h>
#include <WiFiManager.h>
#include <AcaiaArduinoBLE.h>
#include <NimBLEDevice.h>
#include "Config.h"
#include "Logger.h"

WiFiManager wifiManager;

String hostName;

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

AcaiaArduinoBLE* scale = nullptr;
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
static constexpr uint8_t FIRMWARE_VERSION = 1;

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pWeightCharacteristic = nullptr;
NimBLECharacteristic* pReedSwitchCharacteristic = nullptr;
NimBLECharacteristic* pMomentaryCharacteristic = nullptr;
NimBLECharacteristic* pAutoTareCharacteristic = nullptr;
NimBLECharacteristic* pMinShotDurationCharacteristic = nullptr;
NimBLECharacteristic* pMaxShotDurationCharacteristic = nullptr;
NimBLECharacteristic* pDripDelayCharacteristic = nullptr;
NimBLECharacteristic* pFirmwareVersionCharacteristic = nullptr;
NimBLECharacteristic* pScaleStatusCharacteristic = nullptr;

bool deviceConnected = false;
bool lastScaleConnected = false; // Track scale state for SCALE_STATUS notifications

volatile bool bleClientConnected = false;
volatile bool bleClientDisconnected = false;

// Deferred write handling (BLE callbacks run on a different task)
struct PendingWrite {
    volatile bool weightDirty;
    volatile bool reedSwitchDirty;
    volatile bool momentaryDirty;
    volatile bool autoTareDirty;
    volatile bool minShotDurationDirty;
    volatile bool maxShotDurationDirty;
    volatile bool dripDelayDirty;
    volatile uint8_t weight;
    volatile uint8_t reedSwitchVal;
    volatile uint8_t momentaryVal;
    volatile uint8_t autoTareVal;
    volatile uint8_t minShotDurationVal;
    volatile uint8_t maxShotDurationVal;
    volatile uint8_t dripDelayVal;
};
PendingWrite pendingWrite = {};

// Callback class for BLE server connection events
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServerCallback, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        bleClientConnected = true;
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer* pServerCallback, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        bleClientDisconnected = true;
        NimBLEDevice::startAdvertising();
    }
};

// Callback class for characteristic write events
class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    enum CharID { CHAR_WEIGHT, CHAR_REED, CHAR_MOMENTARY, CHAR_AUTOTARE,
                  CHAR_MIN_DUR, CHAR_MAX_DUR, CHAR_DRIP, CHAR_UNKNOWN };

    static CharID identify(const NimBLECharacteristic* pChar) {
        if (pChar == pWeightCharacteristic)       return CHAR_WEIGHT;
        if (pChar == pReedSwitchCharacteristic)   return CHAR_REED;
        if (pChar == pMomentaryCharacteristic)    return CHAR_MOMENTARY;
        if (pChar == pAutoTareCharacteristic)     return CHAR_AUTOTARE;
        if (pChar == pMinShotDurationCharacteristic) return CHAR_MIN_DUR;
        if (pChar == pMaxShotDurationCharacteristic) return CHAR_MAX_DUR;
        if (pChar == pDripDelayCharacteristic)    return CHAR_DRIP;

        return CHAR_UNKNOWN;
    }

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        const std::string value = pCharacteristic->getValue();
        if (value.empty()) return;
        const auto val = static_cast<uint8_t>(value[0]);

        switch (identify(pCharacteristic)) {
            case CHAR_WEIGHT:
                pendingWrite.weight = val;
                pendingWrite.weightDirty = true;
                break;
            case CHAR_REED:
                pendingWrite.reedSwitchVal = val;
                pendingWrite.reedSwitchDirty = true;
                break;
            case CHAR_MOMENTARY:
                pendingWrite.momentaryVal = val;
                pendingWrite.momentaryDirty = true;
                break;
            case CHAR_AUTOTARE:
                pendingWrite.autoTareVal = val;
                pendingWrite.autoTareDirty = true;
                break;
            case CHAR_MIN_DUR:
                pendingWrite.minShotDurationVal = val;
                pendingWrite.minShotDurationDirty = true;
                break;
            case CHAR_MAX_DUR:
                pendingWrite.maxShotDurationVal = val;
                pendingWrite.maxShotDurationDirty = true;
                break;
            case CHAR_DRIP:
                pendingWrite.dripDelayVal = val;
                pendingWrite.dripDelayDirty = true;
                break;
            case CHAR_UNKNOWN:
                break;
        }
    }
};

// Forward declarations
void setColor(int rgb[3]);
void updateLEDState();
void setBrewingState(bool brewing);
float seconds_f();
void calculateEndTime(Shot* s);
void setupBLEServer();
void processPendingBLEWrites();
void setupWiFi();

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
    hostName = config.get<String>("system.hostname");
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

    // Load target time and shot duration limits
    targetTime = static_cast<float>(config.get<int>("brew.target_time"));
    minShotDuration = static_cast<float>(config.get<int>("brew.min_shot_duration"));
    maxShotDuration = static_cast<float>(config.get<int>("brew.max_shot_duration"));

    // Set log level from config
    int logLevelValue = config.get<int>("system.log_level");
    Logger::setLevel(static_cast<Logger::Level>(logLevelValue));

    // Initialize scale with debug flag based on log level (TRACE=0, DEBUG=1)
    const bool scaleDebug = logLevelValue <= 1;
    scale = new AcaiaArduinoBLE(scaleDebug);

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
    LOGF(INFO, "  Scale Debug: %s", scaleDebug ? "true" : "false");

    // Initialize the GPIO hardware
    pinMode(in, INPUT_PULLUP);
    pinMode(OUT, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setColor(COLOR_OFF);

    // Initialize the BLE hardware using NimBLE
    NimBLEDevice::init(hostName.c_str());

    // Create BLE Server for companion app communication
    setupBLEServer();

    LOG(INFO, "Bluetooth® device active, waiting for connections...");

    setupWiFi();
}

void setupBLEServer() {
    // Full 128-bit UUIDs matching the companion app
    static auto SERVICE_UUID             = "00000000-0000-0000-0000-000000000ffe";
    static auto WEIGHT_CHAR_UUID         = "00000000-0000-0000-0000-00000000ff11";
    static auto REED_SWITCH_CHAR_UUID    = "00000000-0000-0000-0000-00000000ff12";
    static auto MOMENTARY_CHAR_UUID      = "00000000-0000-0000-0000-00000000ff13";
    static auto AUTO_TARE_CHAR_UUID      = "00000000-0000-0000-0000-00000000ff14";
    static auto MIN_SHOT_DUR_CHAR_UUID   = "00000000-0000-0000-0000-00000000ff15";
    static auto MAX_SHOT_DUR_CHAR_UUID   = "00000000-0000-0000-0000-00000000ff16";
    static auto DRIP_DELAY_CHAR_UUID     = "00000000-0000-0000-0000-00000000ff17";
    static auto FW_VERSION_CHAR_UUID     = "00000000-0000-0000-0000-00000000ff18";
    static auto SCALE_STATUS_CHAR_UUID   = "00000000-0000-0000-0000-00000000ff19";

    // Create BLE Server
    pServer = NimBLEDevice::createServer();

    if (!pServer) {
        LOG(ERROR, "Failed to create BLE server!");
        return;
    }
    static ServerCallbacks serverCallbacks;
    pServer->setCallbacks(&serverCallbacks);

    // Create BLE Service
    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    if (!pService) {
        LOG(ERROR, "Failed to create BLE service!");
        return;
    }

    static CharacteristicCallbacks characteristicCallbacks;

    // Helper to create a R/W characteristic, set initial value, and attach callbacks
    auto createRWChar = [&](const char* uuid, const uint8_t initVal) -> NimBLECharacteristic* {
        auto* pChar = pService->createCharacteristic(uuid, READ | WRITE);

        if (pChar) {
            pChar->setCallbacks(&characteristicCallbacks);
            pChar->setValue(&initVal, 1);
        }

        return pChar;
    };

    // FF11: Weight (R/W)
    pWeightCharacteristic = createRWChar(WEIGHT_CHAR_UUID, static_cast<uint8_t>(goalWeight));

    // FF12: Reed Switch (R/W, bool as uint8)
    pReedSwitchCharacteristic = createRWChar(REED_SWITCH_CHAR_UUID, reedSwitch ? 1 : 0);

    // FF13: Momentary (R/W, bool as uint8)
    pMomentaryCharacteristic = createRWChar(MOMENTARY_CHAR_UUID, momentary ? 1 : 0);

    // FF14: Auto Tare (R/W, bool as uint8)
    pAutoTareCharacteristic = createRWChar(AUTO_TARE_CHAR_UUID, autoTare ? 1 : 0);

    // FF15: Min Shot Duration (R/W, uint8 seconds)
    pMinShotDurationCharacteristic = createRWChar(MIN_SHOT_DUR_CHAR_UUID, static_cast<uint8_t>(minShotDuration));

    // FF16: Max Shot Duration (R/W, uint8 seconds)
    pMaxShotDurationCharacteristic = createRWChar(MAX_SHOT_DUR_CHAR_UUID, static_cast<uint8_t>(maxShotDuration));

    // FF17: Drip Delay (R/W, uint8 seconds)
    pDripDelayCharacteristic = createRWChar(DRIP_DELAY_CHAR_UUID, static_cast<uint8_t>(dripDelay));

    // FF18: Firmware Version (R only)
    pFirmwareVersionCharacteristic = pService->createCharacteristic(FW_VERSION_CHAR_UUID, READ);

    if (pFirmwareVersionCharacteristic) {
        constexpr uint8_t fwVer = FIRMWARE_VERSION;
        pFirmwareVersionCharacteristic->setValue(&fwVer, 1);
    }

    // FF19: Scale Status (R + Notify)
    pScaleStatusCharacteristic = pService->createCharacteristic(SCALE_STATUS_CHAR_UUID, READ | NOTIFY);

    if (pScaleStatusCharacteristic) {
        const uint8_t scaleStatus = scale && scale->isConnected() ? 1 : 0;
        pScaleStatusCharacteristic->setValue(&scaleStatus, 1);
    }

    // Start the service
    if (!pService->start()) {
        LOG(ERROR, "Failed to start BLE service!");
        return;
    }

    // Start advertising
    // Enable scan response so the name goes into the scan response packet
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->enableScanResponse(true);
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setName(hostName.c_str());

    if (!pAdvertising->start()) {
        LOG(ERROR, "Failed to start BLE advertising!");
        return;
    }

    LOG(INFO, "BLE server initialized (firmware v1)");
}

void loop() {
    wifiManager.process();

    // Update brewByTimeOnly based on scale connection status
    // If configured as false, use time-only mode when scale is disconnected
    if (!brewByTimeOnlyConfigured) {
        brewByTimeOnly = !scale->isConnected();
    }
    // If configured as true, always use time-only mode
    else {
        brewByTimeOnly = true;
    }

    // Connect to scale using non-blocking approach
    if (!scale->isConnected()) {
        // Start connection process if not already connecting
        if (!scale->isConnecting()) {
            scale->init();
            currentWeight = 0;

            // Only stop brewing if not brewing by time
            if (shot.brewing && !brewByTimeOnly) {
                shot.brewing = false;
                shot.end = DISCONNECT;
                setBrewingState(false);
            }
        }

        // Update connection state machine
        scale->updateConnection();

        // Detect cleanup by library and re-create the server if necessary
        if (NimBLEDevice::getServer() == nullptr) {
            LOG(WARNING, "BLE server destroyed by scale library cleanup, re-creating...");
            setupBLEServer();
        }
    }

    // Log BLE client connection events (deferred from NimBLE callback task)
    if (bleClientConnected) {
        bleClientConnected = false;
        LOG(INFO, "BLE client connected to shotStopper");
    }

    if (bleClientDisconnected) {
        bleClientDisconnected = false;
        LOG(INFO, "BLE client disconnected from shotStopper");
    }

    // Process any pending BLE characteristic writes from the companion app
    processPendingBLEWrites();

    // Notify companion app of scale connection status changes

    if (const bool scaleConnectedNow = scale->isConnected(); scaleConnectedNow != lastScaleConnected) {
        lastScaleConnected = scaleConnectedNow;

        if (pScaleStatusCharacteristic) {
            const uint8_t status = scaleConnectedNow ? 1 : 0;
            pScaleStatusCharacteristic->setValue(&status, 1);
            (void)pScaleStatusCharacteristic->notify();
            LOGF(INFO, "Scale status changed: %s", scaleConnectedNow ? "connected" : "disconnected");
        }
    }

    // Send a heartbeat message to the scale periodically to maintain connection
    if (scale->isConnected() && scale->heartbeatRequired()) {
        scale->heartbeat();
    }

    // Always call newWeightAvailable to actually receive the datapoint from the scale,
    // otherwise getWeight() will return stale data
    if (scale->isConnected() && scale->newWeightAvailable()) {
        currentWeight = scale->getWeight();

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
    else if (shot.brewing && !scale->isConnected()) {
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
                       && shot.shotTimer > minShotDuration) {
        buttonLatched = true;
        LOG(INFO, "Button latched");
        digitalWrite(OUT, HIGH);
        LOG(DEBUG, "Output HIGH");

        // Get the scale to beep to inform user.
        if (autoTare) {
            scale->tare();
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
        && (!scale->isConnected() || brewByTimeOnly)
        && shot.shotTimer >= targetTime)
    {
        LOGF(INFO, "Target brew time reached: %.1fs", targetTime);
        shot.brewing = false;
        shot.end = TIME;
        setBrewingState(shot.brewing);
    }

    // End shot by weight (only if not in time-only mode)
    if (scale->isConnected()
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
    if (scale->isConnected()
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

void setupWiFi() {
    // Non-blocking mode: if saved credentials exist, connect in the background.
    // Otherwise start a captive portal AP for configuration.
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setConfigPortalTimeout(0); // Portal stays open indefinitely until configured
    wifiManager.setConnectTimeout(10);     // 10s timeout per connection attempt

    if (!wifiManager.autoConnect(hostName.c_str())) {
        LOGF(INFO, "WiFi not configured - captive portal active on AP: %s", hostName.c_str());
    }
    else {
        LOGF(INFO, "WiFi connected: %s", WiFi.localIP().toString().c_str());
    }
}

void processPendingBLEWrites() {
    bool needsSave = false;

    if (pendingWrite.weightDirty) {
        pendingWrite.weightDirty = false;
        const uint8_t val = pendingWrite.weight;

        if (val != static_cast<uint8_t>(goalWeight)) {
            LOGF(INFO, "BLE: Goal weight updated from %.0f to %d", goalWeight, val);
            goalWeight = val;
            config.set<double>("brew.goal_weight", goalWeight);
            needsSave = true;

            if (pWeightCharacteristic) {
                pWeightCharacteristic->setValue(&val, 1);
            }
        }
    }

    if (pendingWrite.reedSwitchDirty) {
        pendingWrite.reedSwitchDirty = false;

        if (const bool val = pendingWrite.reedSwitchVal != 0; val != reedSwitch) {
            LOGF(INFO, "BLE: Reed switch updated to %s", val ? "true" : "false");
            reedSwitch = val;
            in = reedSwitch ? REED_IN : IN;
            config.set<bool>("switch.reedcontact", reedSwitch);
            needsSave = true;
        }
    }

    if (pendingWrite.momentaryDirty) {
        pendingWrite.momentaryDirty = false;

        if (const bool val = pendingWrite.momentaryVal != 0; val != momentary) {
            LOGF(INFO, "BLE: Momentary updated to %s", val ? "true" : "false");
            momentary = val;
            config.set<bool>("switch.momentary", momentary);
            needsSave = true;
        }
    }

    if (pendingWrite.autoTareDirty) {
        pendingWrite.autoTareDirty = false;

        if (const bool val = pendingWrite.autoTareVal != 0; val != autoTare) {
            LOGF(INFO, "BLE: Auto tare updated to %s", val ? "true" : "false");
            autoTare = val;
            config.set<bool>("scale.auto_tare", autoTare);
            needsSave = true;
        }
    }

    if (pendingWrite.minShotDurationDirty) {
        pendingWrite.minShotDurationDirty = false;

        if (const auto val = static_cast<float>(pendingWrite.minShotDurationVal); val != minShotDuration) {
            LOGF(INFO, "BLE: Min shot duration updated from %.0f to %.0f", minShotDuration, val);
            minShotDuration = val;
            config.set<int>("brew.min_shot_duration", static_cast<int>(minShotDuration));
            needsSave = true;
        }
    }

    if (pendingWrite.maxShotDurationDirty) {
        pendingWrite.maxShotDurationDirty = false;

        if (const auto val = static_cast<float>(pendingWrite.maxShotDurationVal); val != maxShotDuration) {
            LOGF(INFO, "BLE: Max shot duration updated from %.0f to %.0f", maxShotDuration, val);
            maxShotDuration = val;
            config.set<int>("brew.max_shot_duration", static_cast<int>(maxShotDuration));
            needsSave = true;
        }
    }

    if (pendingWrite.dripDelayDirty) {
        pendingWrite.dripDelayDirty = false;

        if (const auto val = static_cast<float>(pendingWrite.dripDelayVal); val != dripDelay) {
            LOGF(INFO, "BLE: Drip delay updated from %.0f to %.0f", dripDelay, val);
            dripDelay = val;
            config.set<double>("brew.drip_delay", dripDelay);
            needsSave = true;
        }
    }

    if (needsSave) {
        if (!config.save()) {
            LOG(ERROR, "Failed to save config after BLE write");
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

        if (scale->isConnected()) {
            scale->resetTimer();

            if (autoTare) {
                scale->tare();
            }

            scale->startTimer();
            LOG(DEBUG, "Waiting for weight data...");
        }
        else {
            LOG(INFO, "Shot started (Time Mode)");
        }
    }
    else {
        auto endReason = "unknown";

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
        scale->stopTimer();

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
        if (scale->isConnected()) {
            setColor(millis() / 1000 % 2 ? COLOR_GREEN : COLOR_BLUE);
        }
        else {
            setColor(millis() / 1000 % 2 ? COLOR_RED : COLOR_BLUE);
        }
    }
    else if (!scale->isConnected()) {
        setColor(COLOR_RED);
    }
    else {
        setColor(COLOR_GREEN);
    }
}