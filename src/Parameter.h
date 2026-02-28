/**
 * @file Parameter.h
 *
 * @brief Represents a configurable parameter with metadata for the web UI
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <cstring>

enum EditableKind {
    kInteger = 0,
    kUInt8 = 1,
    kDouble = 2,
    kFloat = 3,
    kCString = 4,
    kEnum = 5
};

class Parameter {
public:
    // Numeric parameter constructor
    Parameter(
        const char* id,
        const char* displayName,
        EditableKind type,
        int section,
        int position,
        std::function<double()> numericGetter,
        std::function<void(double)> numericSetter,
        double minValue,
        double maxValue,
        bool hasHelp,
        const char* helpText,
        std::function<bool()> showCondition,
        void* globalVar)
        : _id(id),
          _displayName(displayName),
          _type(type),
          _section(section),
          _position(position),
          _numericGetter(std::move(numericGetter)),
          _numericSetter(std::move(numericSetter)),
          _minValue(minValue),
          _maxValue(maxValue),
          _hasHelpText(hasHelp),
          _helpText(helpText ? helpText : ""),
          _showCondition(std::move(showCondition)),
          _globalVar(globalVar) {}

    // Bool parameter constructor
    Parameter(
        const char* id,
        const char* displayName,
        EditableKind type,
        int section,
        int position,
        std::function<bool()> boolGetter,
        std::function<void(bool)> boolSetter,
        bool hasHelp,
        const char* helpText,
        std::function<bool()> showCondition,
        void* globalVar)
        : _id(id),
          _displayName(displayName),
          _type(type),
          _section(section),
          _position(position),
          _boolGetter(std::move(boolGetter)),
          _boolSetter(std::move(boolSetter)),
          _minValue(0),
          _maxValue(1),
          _hasHelpText(hasHelp),
          _helpText(helpText ? helpText : ""),
          _showCondition(std::move(showCondition)),
          _globalVar(globalVar) {}

    // String parameter constructor
    Parameter(
        const char* id,
        const char* displayName,
        EditableKind type,
        int section,
        int position,
        std::function<String()> stringGetter,
        std::function<void(const String&)> stringSetter,
        double maxLength,
        bool hasHelp,
        const char* helpText,
        std::function<bool()> showCondition,
        void* globalVar)
        : _id(id),
          _displayName(displayName),
          _type(type),
          _section(section),
          _position(position),
          _stringGetter(std::move(stringGetter)),
          _stringSetter(std::move(stringSetter)),
          _minValue(0),
          _maxValue(maxLength),
          _hasHelpText(hasHelp),
          _helpText(helpText ? helpText : ""),
          _showCondition(std::move(showCondition)),
          _globalVar(globalVar) {}

    // Enum parameter constructor
    Parameter(
        const char* id,
        const char* displayName,
        EditableKind type,
        int section,
        int position,
        std::function<double()> numericGetter,
        std::function<void(double)> numericSetter,
        const char* const options[],
        int optionCount,
        bool hasHelp,
        const char* helpText,
        std::function<bool()> showCondition,
        void* globalVar)
        : _id(id),
          _displayName(displayName),
          _type(type),
          _section(section),
          _position(position),
          _numericGetter(std::move(numericGetter)),
          _numericSetter(std::move(numericSetter)),
          _enumOptions(options),
          _enumCount(optionCount),
          _minValue(0),
          _maxValue(optionCount - 1),
          _hasHelpText(hasHelp),
          _helpText(helpText ? helpText : ""),
          _showCondition(std::move(showCondition)),
          _globalVar(globalVar) {}

    // Static value string constructor (read-only, e.g. VERSION)
    Parameter(
        const char* id,
        const char* displayName,
        EditableKind type,
        int section,
        int position,
        std::function<const char*()> staticStringGetter,
        std::nullptr_t,
        double maxLength,
        bool hasHelp,
        const char* helpText,
        std::function<bool()> showCondition,
        void* globalVar)
        : _id(id),
          _displayName(displayName),
          _type(type),
          _section(section),
          _position(position),
          _staticStringGetter(std::move(staticStringGetter)),
          _minValue(0),
          _maxValue(maxLength),
          _hasHelpText(hasHelp),
          _helpText(helpText ? helpText : ""),
          _showCondition(std::move(showCondition)),
          _globalVar(globalVar) {}

    // --- Getters ---

    const String& getId() const { return _id; }
    const String& getDisplayName() const { return _displayName; }
    EditableKind getType() const { return _type; }
    int getSection() const { return _section; }
    int getPosition() const { return _position; }
    double getMinValue() const { return _minValue; }
    double getMaxValue() const { return _maxValue; }
    bool hasHelpText() const { return _hasHelpText; }
    const String& getHelpText() const { return _helpText; }
    bool requiresReboot() const { return _requiresReboot; }
    void setRequiresReboot(bool val) { _requiresReboot = val; }
    const char* const* getEnumOptions() const { return _enumOptions; }
    size_t getEnumCount() const { return _enumCount; }
    void* getGlobalVariablePointer() const { return _globalVar; }

    bool shouldShow() const {
        return _showCondition ? _showCondition() : true;
    }

    double getValue() const {
        if (_boolGetter) return _boolGetter() ? 1.0 : 0.0;
        if (_numericGetter) return _numericGetter();
        return 0.0;
    }

    String getStringValue() const {
        if (_stringGetter) return _stringGetter();
        if (_staticStringGetter) return String(_staticStringGetter());
        return String();
    }

    void setValue(double val) {
        if (_boolSetter) {
            _boolSetter(val != 0.0);
        }
        else if (_numericSetter) {
            _numericSetter(val);
        }
    }

    void setStringValue(const String& val) {
        if (_stringSetter) _stringSetter(val);
    }

    template <typename T>
    T getValueAs() const {
        if constexpr (std::is_same_v<T, bool>) {
            return getValue() != 0.0;
        }
        else if constexpr (std::is_same_v<T, String>) {
            return getStringValue();
        }
        else {
            return static_cast<T>(getValue());
        }
    }

    String getFormattedValue() const {
        switch (_type) {
            case kCString:
                return getStringValue();
            case kUInt8:
                return String(static_cast<uint8_t>(getValue()));
            case kInteger:
                return String(static_cast<int>(getValue()));
            case kDouble:
            case kFloat:
                return String(getValue(), 2);
            case kEnum: {
                int idx = static_cast<int>(getValue());
                if (_enumOptions && idx >= 0 && idx < static_cast<int>(_enumCount)) {
                    return String(_enumOptions[idx]);
                }
                return String(idx);
            }
            default:
                return String(getValue());
        }
    }

    void syncToGlobalVariable(double val) const {
        if (!_globalVar) return;
        switch (_type) {
            case kUInt8:
                *static_cast<bool*>(_globalVar) = val != 0.0;
                break;
            case kInteger:
                *static_cast<int*>(_globalVar) = static_cast<int>(val);
                break;
            case kDouble:
                *static_cast<double*>(_globalVar) = val;
                break;
            case kFloat:
                *static_cast<float*>(_globalVar) = static_cast<float>(val);
                break;
            default:
                break;
        }
    }

    void syncToGlobalVariable(const String& val) const {
        if (!_globalVar || _type != kCString) return;
        *static_cast<String*>(_globalVar) = val;
    }

private:
    String _id;
    String _displayName;
    EditableKind _type;
    int _section;
    int _position;

    // Value accessors — only one pair is set depending on parameter type
    std::function<double()> _numericGetter;
    std::function<void(double)> _numericSetter;
    std::function<bool()> _boolGetter;
    std::function<void(bool)> _boolSetter;
    std::function<String()> _stringGetter;
    std::function<void(const String&)> _stringSetter;
    std::function<const char*()> _staticStringGetter;

    // Enum support
    const char* const* _enumOptions = nullptr;
    size_t _enumCount = 0;

    // Constraints
    double _minValue = 0;
    double _maxValue = 0;

    // Metadata
    bool _hasHelpText = false;
    String _helpText;
    bool _requiresReboot = false;
    std::function<bool()> _showCondition;
    void* _globalVar = nullptr;
};

