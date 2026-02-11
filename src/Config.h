
/**
 * @file Config.h
 *
 * @brief Centralized configuration management with JSON storage
 */

#pragma once

#include "ConfigDef.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <map>
#include <utility>

class Config {
    public:
        /**
         * @brief Initialize the configuration system
         *
         * @return true if successful, false otherwise
         */
        bool begin() {
            if (!LittleFS.begin(true)) {
                LOG(ERROR, "Failed to initialize LittleFS");
                return false;
            }

            // Check if config file exists
            if (!LittleFS.exists(CONFIG_FILE)) {
                LOG(INFO, "Config file not found, creating from defaults");

                createDefaults();

                return save();
            }

            // Try to load existing config
            if (!load()) {
                LOG(WARNING, "Failed to load config, creating from defaults");

                createDefaults();

                return save();
            }

            initializeConfigDefs();

            return true;
        }

        /**
         * @brief Load configuration from file
         *
         * @return true if successful, false otherwise
         */
        bool load() {
            if (!LittleFS.exists(CONFIG_FILE)) {
                LOG(INFO, "Config file does not exist");

                return false;
            }

            File file = LittleFS.open(CONFIG_FILE, "r");

            if (!file) {
                LOG(ERROR, "Failed to open config file for reading");

                return false;
            }

            const DeserializationError error = deserializeJson(_doc, file);
            file.close();

            if (error) {
                LOG(ERROR, "Failed to parse config file");
                return false;
            }

            LOG(INFO, "Configuration loaded successfully");

            return true;
        }

        /**
         * @brief Save configuration to file
         *
         * @return true if successful, false otherwise
         */
        [[nodiscard]] bool save() const {
            File file = LittleFS.open(CONFIG_FILE, "w");

            if (!file) {
                LOG(ERROR, "Failed to open config file for writing");
                return false;
            }

            if (serializeJson(_doc, file) == 0) {
                LOG(ERROR, "Failed to write config to file");
                file.close();
                return false;
            }

            file.close();
            LOG(INFO, "Configuration saved successfully");

            return true;
        }

        bool validateAndApplyFromJson(const String& jsonString) {
            JsonDocument doc;
            const DeserializationError error = deserializeJson(doc, jsonString);

            if (error) {
                LOGF(ERROR, "JSON parsing failed: %s", error.c_str());
                return false;
            }

            if (!validateAndApplyConfig(doc)) {
                return false;
            }

            return true;
        }

        template <typename T>
        T get(const String& path) const {
            return navigatePath(path, [](JsonVariantConst parent, const String& leafKey) -> T {
                if (leafKey.isEmpty() || parent.isNull()) {
                    return T{};
                }

                auto current = parent[leafKey];

                if constexpr (std::is_same_v<T, bool>) {
                    return current.as<bool>();
                }
                else if constexpr (std::is_same_v<T, int>) {
                    return current.as<int>();
                }
                else if constexpr (std::is_same_v<T, uint8_t>) {
                    return current.as<uint8_t>();
                }
                else if constexpr (std::is_same_v<T, float>) {
                    return current.as<float>();
                }
                else if constexpr (std::is_same_v<T, double>) {
                    return current.as<double>();
                }
                else if constexpr (std::is_same_v<T, String>) {
                    return current.as<String>();
                }
                else {
                    static_assert(std::is_arithmetic_v<T> || std::is_same_v<T, String>, "Type must be arithmetic or String");
                    return current.as<T>();
                }
            });
        }

        template <typename T>
        void set(const String& path, const T& value) {
            navigatePath(
                path,
                [&value](JsonVariant parent, const String& leafKey) {
                    if (!leafKey.isEmpty()) {
                        parent[leafKey] = value;
                    }
                },
                true);
        }

        /**
         * @brief Get the ConfigDef for a specific path
         *
         * @param path The configuration path
         * @return Pointer to ConfigDef if found, nullptr otherwise
         */
        [[nodiscard]] const ConfigDef* getConfigDef(const String& path) const {
            auto it = _configDefs.find(path.c_str());
            if (it != _configDefs.end()) {
                return &it->second;
            }
            return nullptr;
        }

    private:
        template <typename Func>
        static auto navigatePath(JsonVariantConst root, const String& path, Func&& leafHandler) {
            auto current = root;
            int startIndex = 0;
            int dotIndex;

            while ((dotIndex = path.indexOf('.', startIndex)) != -1) {
                String segment = path.substring(startIndex, dotIndex);
                if (current[segment].isNull()) {
                    return leafHandler(JsonVariantConst(), "");
                }
                current = current[segment];
                startIndex = dotIndex + 1;
            }

            return leafHandler(current, path.substring(startIndex));
        }

        template <typename Func>
        static auto navigatePath(JsonVariant root, const String& path, Func&& leafHandler, bool createMissing = false) {
            auto current = root;
            int startIndex = 0;
            int dotIndex;

            while ((dotIndex = path.indexOf('.', startIndex)) != -1) {
                String segment = path.substring(startIndex, dotIndex);
                if (createMissing) {
                    if (!current[segment].is<JsonObject>()) {
                        current = current[segment].to<JsonObject>();
                    }
                    else {
                        current = current[segment];
                    }
                }
                else if (current[segment].isNull()) {
                    return leafHandler(JsonVariant(), "");
                }
                else {
                    current = current[segment];
                }
                startIndex = dotIndex + 1;
            }

            return leafHandler(current, path.substring(startIndex));
        }

        template <typename Func>
        auto navigatePath(const String& path, Func&& leafHandler) const {
            return navigatePath(_doc.as<JsonVariantConst>(), path, std::forward<Func>(leafHandler));
        }

        template <typename Func>
        auto navigatePath(const String& path, Func&& leafHandler, bool createMissing = false) {
            return navigatePath(_doc.as<JsonVariant>(), path, std::forward<Func>(leafHandler), createMissing);
        }

        inline static auto CONFIG_FILE = "/config.json";

        JsonDocument _doc;

        std::map<std::string, ConfigDef> _configDefs;

        void initializeConfigDefs() {
            _configDefs.clear();

            // System configuration
            _configDefs.emplace("system.log_level", ConfigDef::forInt(2, 0, 6)); // Default: INFO (2), Range: TRACE (0) to SILENT (6)

            // Switch configuration
            _configDefs.emplace("switch.momentary", ConfigDef::forBool(true));
            _configDefs.emplace("switch.reedcontact", ConfigDef::forBool(false));

            // Scale configuration
            _configDefs.emplace("scale.auto_tare", ConfigDef::forBool(true));
            _configDefs.emplace("scale.min_weight_for_prediction", ConfigDef::forDouble(10.0, 0.0, 50.0));

            // Brew configuration
            _configDefs.emplace("brew.by_time_only", ConfigDef::forBool(false));
            _configDefs.emplace("brew.goal_weight", ConfigDef::forDouble(40.0, 10.0, 100.0));
            _configDefs.emplace("brew.weight_offset", ConfigDef::forDouble(1.5, 0.0, 5.0));
            _configDefs.emplace("brew.max_offset", ConfigDef::forDouble(5.0, 1.0, 10.0));
            _configDefs.emplace("brew.pulse_duration_ms", ConfigDef::forInt(300, 100, 1000));
            _configDefs.emplace("brew.drip_delay", ConfigDef::forDouble(3.0, 1.0, 10.0));
            _configDefs.emplace("brew.reed_switch_delay", ConfigDef::forDouble(1.0, 0.1, 5.0));
            _configDefs.emplace("brew.target_time", ConfigDef::forInt(30, 3, 60)); // min/max used for shot duration limits
        }

        /**
         * @brief Set a value in the JSON document using a dot-separated path
         */
        template <typename T>
        static bool setJsonValue(JsonDocument& doc, const String& path, const T& value) {
            if (path.isEmpty()) {
                LOGF(ERROR, "Empty path provided to setJsonValue");
                return false;
            }

            return navigatePath(
                doc.as<JsonVariant>(), path,
                [&path, &value](JsonVariant parent, const String& leafKey) {
                    if (leafKey.isEmpty() || parent.isNull()) {
                        LOGF(ERROR, "Failed to navigate to path: %s", path.c_str());
                        return false;
                    }

                    parent[leafKey] = value;

                    LOGF(TRACE, "Successfully set %s = %s", path.c_str(), String(value).c_str());

                    return true;
                },
                true);
        }

        /**
         * @brief Create a new configuration with default values
         */
        void createDefaults() {
            LOGF(INFO, "Starting createDefaults");

            initializeConfigDefs();
            _doc.clear();

            LOGF(INFO, "Processing %d config definitions", _configDefs.size());

            int successCount = 0;
            for (const auto& [path, configDef] : _configDefs) {
                const auto pathStr = String(path.c_str());

                LOGF(DEBUG, "Processing path: '%s'", pathStr.c_str());

                bool success = false;

                switch (configDef.type) {
                    case ConfigDef::BOOL:
                        LOGF(DEBUG, "Setting bool %s = %s", pathStr.c_str(), configDef.boolVal ? "true" : "false");
                        success = setJsonValue(_doc, pathStr, configDef.boolVal);
                        break;

                    case ConfigDef::INT:
                        LOGF(DEBUG, "Setting int %s = %d", pathStr.c_str(), configDef.intVal);
                        success = setJsonValue(_doc, pathStr, configDef.intVal);
                        break;

                    case ConfigDef::DOUBLE:
                        LOGF(DEBUG, "Setting double %s = %f", pathStr.c_str(), configDef.doubleVal);
                        success = setJsonValue(_doc, pathStr, configDef.doubleVal);
                        break;

                    case ConfigDef::STRING:
                        LOGF(DEBUG, "Setting string %s = '%s'", pathStr.c_str(), configDef.stringVal.c_str());
                        success = setJsonValue(_doc, pathStr, configDef.stringVal);
                        break;

                    default:
                        LOGF(ERROR, "Unknown config type for path: %s", pathStr.c_str());
                        continue;
                }

                if (success) {
                    successCount++;
                    LOGF(DEBUG, "Successfully set value for %s", pathStr.c_str());
                }
                else {
                    LOGF(ERROR, "Failed to set value for %s", pathStr.c_str());
                }
            }

            LOGF(INFO, "createDefaults completed. Successfully set %d/%d values", successCount, _configDefs.size());

            String jsonStr;
            serializeJsonPretty(_doc, jsonStr);
            LOGF(DEBUG, "Final JSON structure:\n%s", jsonStr.c_str());
        }

        bool validateAndApplyConfig(const JsonDocument& doc) {
            LOGF(INFO, "Validating and applying configuration with %d parameters", _configDefs.size());

            // Helper function to recursively extract all paths from JSON
            std::function<void(JsonVariantConst, const String&, std::vector<std::pair<String, JsonVariantConst>>&)> extractPaths = [&](JsonVariantConst obj, const String& prefix,
                                                                                                                                       std::vector<std::pair<String, JsonVariantConst>>& paths) {
                if (obj.is<JsonObjectConst>()) {
                    for (JsonPairConst pair : obj.as<JsonObjectConst>()) {
                        String newPath = prefix.isEmpty() ? String(pair.key().c_str()) : prefix + "." + pair.key().c_str();
                        extractPaths(pair.value(), newPath, paths);
                    }
                }
                else {
                    // Leaf value
                    paths.emplace_back(prefix, obj);
                }
            };

            // Extract all paths from the document
            std::vector<std::pair<String, JsonVariantConst>> docPaths;
            extractPaths(doc.as<JsonVariantConst>(), "", docPaths);

            LOGF(DEBUG, "Found %d parameters in uploaded config", docPaths.size());

            // Validate each path against _configDefs
            for (const auto& [path, value] : docPaths) {
                auto it = _configDefs.find(path.c_str());

                if (it == _configDefs.end()) {
                    LOGF(WARNING, "Unknown parameter in config: %s - skipping", path.c_str());
                    continue;
                }

                const ConfigDef& def = it->second;

                // Validate and apply based on type
                bool validationSuccess = false;
                switch (def.type) {
                    case ConfigDef::BOOL:
                        {
                            if (value.is<bool>()) {
                                bool boolVal = value.as<bool>();
                                set<bool>(path.c_str(), boolVal);
                                validationSuccess = true;
                                LOGF(TRACE, "Applied bool %s = %s", path.c_str(), boolVal ? "true" : "false");
                            }
                            else {
                                LOGF(ERROR, "Invalid type for boolean parameter %s", path.c_str());
                            }
                            break;
                        }

                    case ConfigDef::INT:
                        {
                            if (value.is<int>()) {
                                if (auto intVal = value.as<int>(); intVal >= def.minValue && intVal <= def.maxValue) {
                                    set<int>(path.c_str(), intVal);
                                    validationSuccess = true;
                                    LOGF(TRACE, "Applied int %s = %d", path.c_str(), intVal);
                                }
                                else {
                                    LOGF(ERROR, "Value %d for %s outside range [%.2f, %.2f]", intVal, path.c_str(), def.minValue, def.maxValue);
                                }
                            }
                            else {
                                LOGF(ERROR, "Invalid type for integer parameter %s", path.c_str());
                            }

                            break;
                        }

                    case ConfigDef::DOUBLE:
                        {
                            if (value.is<double>() || value.is<float>()) {
                                if (auto doubleVal = value.as<double>(); doubleVal >= def.minValue && doubleVal <= def.maxValue) {
                                    set<double>(path.c_str(), doubleVal);
                                    validationSuccess = true;
                                    LOGF(TRACE, "Applied double %s = %.4f", path.c_str(), doubleVal);
                                }
                                else {
                                    LOGF(ERROR, "Value %.4f for %s outside range [%.2f, %.2f]", doubleVal, path.c_str(), def.minValue, def.maxValue);
                                }
                            }
                            else {
                                LOGF(ERROR, "Invalid type for double parameter %s", path.c_str());
                            }
                            break;
                        }

                    case ConfigDef::STRING:
                        {
                            if (value.is<const char*>() || value.is<String>()) {
                                auto stringVal = value.as<String>();

                                if (stringVal.length() <= def.maxLength) {
                                    set<String>(path.c_str(), stringVal);
                                    validationSuccess = true;
                                    LOGF(TRACE, "Applied string %s = %s", path.c_str(), stringVal.c_str());
                                }
                                else {
                                    LOGF(ERROR, "String value for %s too long: %d > %d", path.c_str(), stringVal.length(), def.maxLength);
                                }
                            }
                            else {
                                LOGF(ERROR, "Invalid type for string parameter %s", path.c_str());
                            }
                            break;
                        }
                }

                if (!validationSuccess) {
                    LOGF(ERROR, "Failed to validate parameter: %s", path.c_str());
                    return false;
                }
            }

            LOGF(INFO, "Successfully validated and applied all configuration parameters");

            return save();
        }

        template <typename T>
        static bool validateParameterRange(const char* paramName, T value, T min, T max) {
            if (value < min || value > max) {
                LOGF(ERROR, "Parameter %s value %.2f out of range [%.2f, %.2f]", paramName, static_cast<double>(value), static_cast<double>(min), static_cast<double>(max));
                return false;
            }
            return true;
        }

        static String constrainStringParameter(const String& value, const size_t maxLength, const char* paramName = nullptr) {
            if (value.length() <= maxLength) {
                return value;
            }

            LOGF(WARNING, "Parameter '%s' truncated from %d to %d characters", paramName, value.length(), maxLength);

            return value.substring(0, maxLength);
        }
};
