#include <WiFi.h>

#include "Logger.h"

int logLevel;

Logger::Logger(const uint16_t port) :
    port_(port), server_(port) {
}

Logger& Logger::getInstanceImpl(const uint16_t port) {
    static Logger instance{port};
    return instance;
}

void Logger::init(const uint16_t port) {
    getInstanceImpl(port);
}

bool Logger::begin() {
    if (WiFi.status() == WL_CONNECTED) {
        getInstance().server_.begin();
    }

    // If the serial interface has not been started, start it now:
    if (!Serial) {
        Serial.begin(115200);
    }
    return true;
}

bool Logger::update() {
    if (getInstance().server_.hasClient()) {
        // If we are already connected to another client, then reject the new connection, otherwise accept the connection.
        if (getInstance().client_.connected()) {
            LOG(WARNING, "Serial Server Connection rejected");
            getInstance().server_.available().stop();
        }
        else {
            LOG(INFO, "Serial Server Connection accepted");
            getInstance().client_ = getInstance().server_.available();
        }
    }

    // update if the loglevel has changed

    if (const auto level = static_cast<Level>(logLevel); getCurrentLevel() != level) {
        setLevel(level);
        LOGF(INFO, "Log level changed to %s", Logger::get_level_identifier(level).c_str());
    }

    return true;
}

uint16_t Logger::getPort() {
    return getInstance().port_;
}

void Logger::log(const Level level, const String& file, const char* function, const uint32_t line, const char* logmsg) {
    char time[12];
    current_time(time);

    if (WiFi.status() == WL_CONNECTED && client_.connected()) {
        client_.print(time);
        client_.print(get_level_identifier(level).c_str());
        client_.print(" ");
        if (level < Level::DEBUG) {
            client_.print(file.c_str());
            client_.print(":");
            client_.print(line);
            client_.print("@");
            client_.print(function);
            client_.print("() ");
        }
        client_.print(logmsg);
        client_.print("\n");
    }
    else {
        Serial.print(time);
        Serial.print(get_level_identifier(level).c_str());
        Serial.print(" ");
        if (level < Level::DEBUG) {
            Serial.print(file.c_str());
            Serial.print(":");
            Serial.print(line);
            Serial.print("@");
            Serial.print(function);
            Serial.print("() ");
        }
        Serial.print(logmsg);
        Serial.print("\n");
    }
}

void Logger::logf(const Level level, const String& file, const char* function, const uint32_t line, const char* format, ...) {
    // reimplement printf method so that we can take dynamic list of parameters as printf does
    // (we can't simply pass these on to existing printf methods it seems)

    va_list arg;
    va_start(arg, format);
    char temp[64]; // allocate a temp buffer
    char* buffer = temp;
    const size_t len = vsnprintf(temp, sizeof(temp), format, arg);
    va_end(arg);

    // if temp buffer was too short, create new one with enough room (includes previous bytes)
    if (len > sizeof(temp) - 1) {
        buffer = new char[len + 1];

        va_start(arg, format);
        vsnprintf(buffer, len + 1, format, arg);
        va_end(arg);
    }

    log(level, file, function, line, buffer);

    if (buffer != temp) {
        delete[] buffer;
    }
}

void Logger::current_time(char* timestamp) {
    time_t rawtime;
    time(&rawtime);
    const tm* timeinfo = localtime(&rawtime);
    snprintf(timestamp, 12, "[%02d:%02d:%02d] ", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

String Logger::get_level_identifier(const Level lvl) {
    switch (lvl) {
        case Level::TRACE:
            return "  TRACE";
        case Level::DEBUG:
            return "  DEBUG";
        case Level::INFO:
            return "   INFO";
        case Level::WARNING:
            return "WARNING";
        case Level::ERROR:
            return "  ERROR";
        case Level::FATAL:
            return "  FATAL";
        default:
            return " SILENT";
    }
}
