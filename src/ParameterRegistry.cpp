#include "ParameterRegistry.h"
#include "Logger.h"

#include <algorithm>

ParameterRegistry ParameterRegistry::_singleton;

// Global variables from main.cpp
extern float goalWeight;
extern float weightOffset;
extern float maxOffset;
extern int brewPulseDuration;
extern float dripDelay;
extern float reedSwitchDelay;
extern float minWeightForPrediction;
extern float minShotDuration;
extern float maxShotDuration;
extern float targetTime;
extern bool momentary;
extern bool reedSwitch;
extern bool autoTare;
extern bool brewByTimeOnly;
extern bool brewByTimeOnlyConfigured;
extern String hostName;

static constexpr const char* const logLevels[] = {"TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "SILENT"};

void ParameterRegistry::initialize(Config& config) {
    if (_ready) {
        return;
    }

    _config = &config;

    _parameters.clear();
    _parameterMap.clear();
    _pendingChanges = false;
    _lastChangeTime = 0;

    // --- Brew Section ---

    addNumericConfigParam<double>(
        "brew.goal_weight",
        "Goal Weight (g)",
        kDouble,
        sBrewSection,
        100,
        nullptr,
        10.0, 100.0,
        "Target weight for the shot. The brew will stop once this weight minus the offset is reached."
    );

    addNumericConfigParam<double>(
        "brew.weight_offset",
        "Weight Offset (g)",
        kDouble,
        sBrewSection,
        101,
        nullptr,
        0.0, 5.0,
        "Offset subtracted from the goal weight to account for drip after the pump stops. Automatically adjusted after each shot."
    );

    addNumericConfigParam<double>(
        "brew.max_offset",
        "Max Offset (g)",
        kDouble,
        sBrewSection,
        102,
        nullptr,
        1.0, 10.0,
        "Maximum allowed offset correction. If the error exceeds this, the offset is not updated."
    );

    addNumericConfigParam<int>(
        "brew.pulse_duration_ms",
        "Pulse Duration (ms)",
        kInteger,
        sBrewSection,
        103,
        &brewPulseDuration,
        100, 1000,
        "Duration of the output pulse used to stop the shot in momentary switch mode."
    );

    addNumericConfigParam<double>(
        "brew.drip_delay",
        "Drip Delay (s)",
        kDouble,
        sBrewSection,
        104,
        nullptr,
        1.0, 10.0,
        "Time to wait after the shot ends before measuring the final weight for offset adjustment."
    );

    addNumericConfigParam<double>(
        "brew.reed_switch_delay",
        "Reed Switch Delay (s)",
        kDouble,
        sBrewSection,
        105,
        nullptr,
        0.1, 5.0,
        "Delay after shot ends during which the reed switch reading is forced off to avoid false triggers."
    );

    addNumericConfigParam<int>(
        "brew.target_time",
        "Target Brew Time (s)",
        kInteger,
        sBrewSection,
        106,
        nullptr,
        3, 60,
        "Target brew time used when the scale is disconnected or brew-by-time-only mode is active."
    );

    addNumericConfigParam<int>(
        "brew.min_shot_duration",
        "Min Shot Duration (s)",
        kInteger,
        sBrewSection,
        107,
        nullptr,
        1, 30,
        "Minimum shot duration before the brew can be ended by weight prediction."
    );

    addNumericConfigParam<int>(
        "brew.max_shot_duration",
        "Max Shot Duration (s)",
        kInteger,
        sBrewSection,
        108,
        nullptr,
        10, 120,
        "Maximum shot duration. The brew will always stop after this time."
    );

    addBoolConfigParam(
        "brew.by_time_only",
        "Brew by Time Only",
        sBrewSection,
        109,
        &brewByTimeOnlyConfigured,
        "When enabled, the brew always stops by time regardless of scale connection."
    );

    // --- Scale Section ---

    addBoolConfigParam(
        "scale.auto_tare",
        "Auto Tare",
        sScaleSection,
        200,
        &autoTare,
        "Automatically tare the scale when a brew starts."
    );

    addNumericConfigParam<double>(
        "scale.min_weight_for_prediction",
        "Min Weight for Prediction (g)",
        kDouble,
        sScaleSection,
        201,
        nullptr,
        0.0, 50.0,
        "Minimum weight before the end-time prediction algorithm activates."
    );

    // --- Switch Section ---

    addBoolConfigParam(
        "switch.momentary",
        "Momentary Switch",
        sSwitchSection,
        300,
        &momentary,
        "Enable if your brew switch is a momentary (push) button rather than a toggle switch."
    );

    addBoolConfigParam(
        "switch.reedcontact",
        "Reed Switch",
        sSwitchSection,
        301,
        &reedSwitch,
        "Enable if you are using a reed contact/magnetic switch instead of a wired button."
    );

    // --- System Section ---

    addStringConfigParam(
        "system.hostname",
        "Hostname",
        sSystemSection,
        400,
        &hostName,
        32,
        "Hostname of the device on the network and for BLE advertising.",
        [] { return true; },
        true
    );

    addEnumConfigParam(
        "system.log_level",
        "Log Level",
        sSystemSection,
        401,
        nullptr,
        logLevels,
        7,
        "Set the logging verbosity level."
    );

    // Sort by position
    std::sort(_parameters.begin(), _parameters.end(), [](const std::shared_ptr<Parameter>& a, const std::shared_ptr<Parameter>& b) { return a->getPosition() < b->getPosition(); });

    _ready = true;

    LOG(INFO, "ParameterRegistry initialized with shotStopper parameters");
}

std::shared_ptr<Parameter> ParameterRegistry::getParameterById(const char* id) {
    if (const auto it = _parameterMap.find(id); it != _parameterMap.end()) {
        return it->second;
    }

    return nullptr;
}

void ParameterRegistry::syncGlobalVariables() const {
    for (const auto& param : _parameters) {
        if (param && param->getGlobalVariablePointer()) {
            if (param->getType() == kCString) {
                param->syncToGlobalVariable(param->getStringValue());
            }
            else {
                param->syncToGlobalVariable(param->getValue());
            }
        }
    }
}
