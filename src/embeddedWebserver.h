/**
 * @file embeddedWebserver.h
 *
 * @brief Embedded async webserver for shotStopper
 */

#pragma once

#include <Arduino.h>

#include "FS.h"
#include <AsyncTCP.h>
#include <WiFi.h>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "LittleFS.h"
#include "ParameterRegistry.h"

inline AsyncWebServer server(80);
inline AsyncEventSource events("/events");

// Forward declarations from main.cpp
extern float currentWeight;
extern float goalWeight;
extern float weightOffset;
extern bool isBrewing;
extern float shotTimer;
extern bool brewByTimeOnly;
extern Config config;

// Forward declaration for WiFi reset
class WiFiManager;
extern WiFiManager wifiManager;

void serverSetup();

// Template processor for HTML files — replaces %HEADER% etc. with fragment files
inline String staticProcessor(const String& var) {
    String varLower(var);
    varLower.toLowerCase();

    if (File file = LittleFS.open("/html_fragments/" + varLower + ".html", "r")) {
        String content = file.readString();
        file.close();
        return content;
    }

    return String();
}

// rounds a number to 2 decimal places
inline double round2(const double value) {
    return std::round(value * 100.0) / 100.0;
}

// Send live status via SSE to connected browser clients
inline void sendStatusEvent() {
    if (events.count() == 0) return;

    JsonDocument doc;
    doc["currentWeight"] = round2(currentWeight);
    doc["goalWeight"] = round2(goalWeight);
    doc["weightOffset"] = round2(weightOffset);
    doc["brewing"] = isBrewing;
    doc["shotTimer"] = round2(shotTimer);
    doc["brewByTimeOnly"] = brewByTimeOnly;

    String json;
    serializeJson(doc, json);

    events.send(json.c_str(), "status", millis());
}

inline String getValue(const String& varName) {
    try {
        const auto e = ParameterRegistry::getInstance().getParameterById(varName.c_str());

        if (e == nullptr) {
            return "(unknown variable " + varName + ")";
        }

        return e->getFormattedValue();
    } catch (const std::out_of_range&) {
        return "(unknown variable " + varName + ")";
    }
}

inline void paramToJson(const String& name, const std::shared_ptr<Parameter>& param, JsonVariant doc) {
    doc["type"] = param->getType();
    doc["name"] = name;
    doc["displayName"] = param->getDisplayName();
    doc["section"] = param->getSection();
    doc["sectionName"] = getSectionName(param->getSection());
    doc["position"] = param->getPosition();
    doc["hasHelpText"] = param->hasHelpText();
    doc["show"] = param->shouldShow();
    doc["reboot"] = param->requiresReboot();

    switch (param->getType()) {
        case kInteger:
            doc["value"] = static_cast<int>(param->getValue());
            break;

        case kUInt8:
            doc["value"] = static_cast<uint8_t>(param->getValue());
            break;

        case kDouble:
            doc["value"] = round2(param->getValue());
            break;

        case kFloat:
            doc["value"] = round2(static_cast<float>(param->getValue()));
            break;

        case kCString:
            doc["value"] = param->getStringValue();
            break;

        case kEnum:
            {
                doc["value"] = static_cast<int>(param->getValue());

                const JsonArray options = doc["options"].to<JsonArray>();
                const char* const* enumOptions = param->getEnumOptions();
                const size_t enumCount = param->getEnumCount();

                for (size_t i = 0; i < enumCount && enumOptions[i] != nullptr; i++) {
                    auto optionObj = options.add<JsonObject>();
                    optionObj["value"] = static_cast<int>(i);
                    optionObj["label"] = enumOptions[i];
                }

                break;
            }

        default:
            doc["value"] = param->getValue();
            break;
    }

    doc["min"] = param->getMinValue();
    doc["max"] = param->getMaxValue();
}

inline void serverSetup() {
    // --- GET/POST /parameters ---
    server.on("/parameters", [](AsyncWebServerRequest* request) {
        if (!request->client() || !request->client()->connected()) {
            return;
        }

        if (request->method() == 1) { // HTTP_GET
            const auto& registry = ParameterRegistry::getInstance();
            const auto& parameters = registry.getParameters();

            // Optional pagination
            int offset = 0;
            int limit = 50;

            if (request->hasParam("offset")) {
                offset = request->getParam("offset")->value().toInt();
            }

            if (request->hasParam("limit")) {
                limit = request->getParam("limit")->value().toInt();
            }

            // Optional section filter
            int sectionFilter = -1;
            if (request->hasParam("section")) {
                sectionFilter = request->getParam("section")->value().toInt();
            }

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            response->print("{\"parameters\":[");

            bool first = true;
            int filteredCount = 0;
            int sent = 0;

            for (const auto& param : parameters) {
                if (!param->shouldShow()) {
                    continue;
                }

                if (sectionFilter >= 0 && param->getSection() != sectionFilter) {
                    continue;
                }

                if (filteredCount++ < offset) {
                    continue;
                }

                if (sent >= limit) {
                    break;
                }

                if (!first) {
                    response->print(",");
                }

                first = false;

                JsonDocument doc;
                paramToJson(param->getId(), param, doc.to<JsonVariant>());
                serializeJson(doc, *response);

                sent++;
            }

            response->printf(R"(],"offset":%d,"limit":%d,"returned":%d,"total":%d})", offset, limit, sent, filteredCount);
            request->send(response);
        }
        else if (request->method() == 2) { // HTTP_POST
            auto& registry = ParameterRegistry::getInstance();

            bool hasErrors = false;

            const auto requestParams = request->params();

            for (auto i = 0u; i < requestParams; ++i) {
                if (auto* p = request->getParam(i); p && p->name().length() > 0 && p->value().length() > 0) {
                    const String& varName = p->name();
                    const String& value = p->value();

                    try {
                        std::shared_ptr<Parameter> paramPtr = registry.getParameterById(varName.c_str());

                        if (paramPtr == nullptr || !paramPtr->shouldShow()) {
                            continue;
                        }

                        if (paramPtr->getType() == kCString) {
                            registry.setParameterValue(varName.c_str(), value);
                        }
                        else {
                            double newVal = std::stod(value.c_str());
                            registry.setParameterValue(varName.c_str(), newVal);
                        }
                    } catch (const std::exception& e) {
                        LOGF(INFO, "Parameter %s processing failed: %s", varName.c_str(), e.what());
                        hasErrors = true;
                    }
                }
            }

            registry.forceSave();

            AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", hasErrors ? "Partial Success" : "OK");
            response->addHeader("Connection", "close");
            request->send(response);
        }
        else {
            LOGF(ERROR, "Unsupported HTTP method %d for /parameters", request->method());
            AsyncWebServerResponse* response = request->beginResponse(405, "text/plain", "Method Not Allowed");
            response->addHeader("Connection", "close");
            request->send(response);
        }
    });

    // --- GET /parameterHelp ---
    server.on("/parameterHelp", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        auto* p = request->getParam(0);

        if (p == nullptr) {
            request->send(422, "text/plain", "parameter is missing");
            return;
        }

        const String& varValue = p->value();

        const std::shared_ptr<Parameter> param = ParameterRegistry::getInstance().getParameterById(varValue.c_str());

        if (param == nullptr) {
            request->send(404, "application/json", "parameter not found");
            return;
        }

        doc["name"] = varValue;
        doc["helpText"] = param->getHelpText();

        String helpJson;
        serializeJson(doc, helpJson);
        request->send(200, "application/json", helpJson);
    });

    // --- GET /status ---
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print('{');
        response->print("\"currentWeight\":");
        response->print(currentWeight, 2);
        response->print(",\"goalWeight\":");
        response->print(goalWeight, 2);
        response->print(",\"weightOffset\":");
        response->print(weightOffset, 2);
        response->print(",\"brewing\":");
        response->print(isBrewing ? "true" : "false");
        response->print(",\"shotTimer\":");
        response->print(shotTimer, 1);
        response->print(",\"brewByTimeOnly\":");
        response->print(brewByTimeOnly ? "true" : "false");
        response->print(",\"freeHeap\":");
        response->print(ESP.getFreeHeap());
        response->print(",\"uptime\":");
        response->print(millis() / 1000);
        response->print('}');
        request->send(response);
    });

    // --- GET /download/config ---
    server.on("/download/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!LittleFS.exists("/config.json")) {
            request->send(404, "text/plain", "Config file not found");
            return;
        }

        File configFile = LittleFS.open("/config.json", "r");

        if (!configFile) {
            request->send(500, "text/plain", "Failed to open config file");
            return;
        }

        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, configFile);
        configFile.close();

        if (error) {
            request->send(500, "text/plain", "Failed to parse config file");
            return;
        }

        String prettifiedJson;
        serializeJsonPretty(doc, prettifiedJson);

        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", prettifiedJson);
        response->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
        request->send(response);
    });

    // --- POST /upload/config ---
    server.on(
        "/upload/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Response is sent from the upload handler
        },
        [](AsyncWebServerRequest* request, const String& filename, const size_t index, const uint8_t* data, const size_t len, const bool final) {
            static String uploadBuffer;
            static size_t totalSize = 0;

            if (index == 0) {
                uploadBuffer = "";
                uploadBuffer.reserve(8192);
                totalSize = 0;
                LOGF(INFO, "Config upload started: %s", filename.c_str());
            }

            for (size_t i = 0; i < len; i++) {
                uploadBuffer += static_cast<char>(data[i]);
            }

            totalSize += len;

            if (final) {
                LOGF(INFO, "Config upload finished: %s, total size: %u bytes", filename.c_str(), totalSize);

                if (config.validateAndApplyFromJson(uploadBuffer)) {
                    LOG(INFO, "Configuration validated and applied successfully");

                    AsyncWebServerResponse* response = request->beginResponse(200, "application/json",
                        R"({"success": true, "message": "Configuration validated and applied successfully.", "restart": true})");
                    response->addHeader("Connection", "close");
                    request->send(response);
                }
                else {
                    LOG(ERROR, "Configuration validation failed");

                    AsyncWebServerResponse* response = request->beginResponse(400, "application/json",
                        R"({"success": false, "message": "Configuration validation failed. Please check parameter values."})");
                    response->addHeader("Connection", "close");
                    request->send(response);
                }
            }
        });

    // --- POST /restart ---
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Restarting...");
        delay(100);
        ESP.restart();
    });

    // --- POST /factoryreset ---
    server.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest* request) {
        const bool removed = LittleFS.remove("/config.json");

        request->send(200, "text/plain", removed ? "Factory reset. Restarting..." : "Could not delete config.json. Restarting...");

        delay(100);
        ESP.restart();
    });

    // --- POST /wifireset ---
    server.on("/wifireset", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "WiFi settings are being reset. Rebooting...");

        delay(1000);
        wifiManager.resetSettings();
        ESP.restart();
    });

    // --- 404 handler ---
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    // --- SSE event source ---
    events.onConnect([](AsyncEventSourceClient* client) {
        if (client->lastId()) {
            LOGF(DEBUG, "SSE client reconnected, last message ID was: %u", client->lastId());
        }

        client->send("hello", nullptr, millis(), 10000);
    });

    server.addHandler(&events);

    // --- Static file serving ---
    LittleFS.begin();
    server.serveStatic("/js", LittleFS, "/js/", "max-age=604800");
    server.serveStatic("/manifest.json", LittleFS, "/manifest.json", "max-age=604800");
    server.serveStatic("/", LittleFS, "/html/", "max-age=604800").setDefaultFile("index.html").setTemplateProcessor(staticProcessor);

    server.begin();

    LOG(INFO, ("Web server started at " + WiFi.localIP().toString()).c_str());
}
