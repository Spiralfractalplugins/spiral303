#pragma once

#include <string>
#include <utility>
#include <vector>

// A small JSON reader, just enough for the .sp303preset files.
//
// The native build uses JUCE's JSON; the WebAssembly build cannot (JUCE does
// not compile for wasm), so this reads the same files in the same format.

namespace spiral303
{
    class JsonValue
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        Type type = Type::Null;
        bool boolean = false;
        double number = 0.0;
        std::string text;
        std::vector<JsonValue> items;
        std::vector<std::pair<std::string, JsonValue>> members;

        bool isNull() const noexcept { return type == Type::Null; }
        bool isBool() const noexcept { return type == Type::Bool; }
        bool isNumber() const noexcept { return type == Type::Number; }
        bool isString() const noexcept { return type == Type::String; }
        bool isArray() const noexcept { return type == Type::Array; }
        bool isObject() const noexcept { return type == Type::Object; }

        const JsonValue* member (const std::string& name) const
        {
            for (const auto& entry : members)
                if (entry.first == name)
                    return &entry.second;

            return nullptr;
        }
    };

    /** Parses a JSON document. Returns false and fills `error` when the text
        is not valid JSON. */
    bool parseJson (const std::string& text, JsonValue& out, std::string& error);

    /** Escapes a string for JSON output. */
    std::string jsonEscape (const std::string& text);
}
