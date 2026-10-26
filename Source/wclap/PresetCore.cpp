#include "PresetCore.h"

#include "JsonLite.h"
#include "FactoryPresetData.h"

namespace spiral303
{
    namespace
    {
        std::string trim (const std::string& text)
        {
            size_t first = 0;
            while (first < text.size() && std::isspace (static_cast<unsigned char> (text[first])))
                ++first;

            size_t last = text.size();
            while (last > first && std::isspace (static_cast<unsigned char> (text[last - 1])))
                --last;

            return text.substr (first, last - first);
        }
    }

    bool parsePresetText (const std::string& jsonText, PresetDef& out, std::string& error)
    {
        JsonValue root;
        if (! parseJson (jsonText, root, error))
            return false;

        if (! root.isObject())
        {
            error = "expected a JSON object";
            return false;
        }

        const JsonValue* format = root.member ("format");
        if (format == nullptr || ! format->isNumber())
        {
            error = "missing the format number";
            return false;
        }

        if (static_cast<int> (format->number) != 1)
        {
            error = "format " + std::to_string (static_cast<int> (format->number))
                    + " is not readable by this build, which reads format 1";
            return false;
        }

        const JsonValue* name = root.member ("name");
        if (name == nullptr || ! name->isString() || trim (name->text).empty())
        {
            error = "missing a name";
            return false;
        }

        const JsonValue* params = root.member ("params");
        if (params == nullptr || ! params->isObject())
        {
            error = "missing a params object";
            return false;
        }

        PresetDef preset;
        preset.name = trim (name->text);

        for (const auto& entry : params->members)
        {
            if (! entry.second.isNumber())
            {
                error = "param " + entry.first + " is not a number";
                return false;
            }

            preset.values.push_back ({ entry.first, static_cast<float> (entry.second.number) });
        }

        if (preset.values.empty())
        {
            error = "params holds no value";
            return false;
        }

        if (const auto* sequence = root.member ("sequence"); sequence != nullptr && sequence->isString())
            preset.sequence = trim (sequence->text);

        if (const auto* gate = root.member ("gate"); gate != nullptr && gate->isString())
            preset.gate = trim (gate->text);

        if (const auto* credit = root.member ("credit"); credit != nullptr && credit->isString())
            preset.credit = credit->text;

        if (const auto* comment = root.member ("comment"); comment != nullptr && comment->isString())
            preset.comment = comment->text;

        out = std::move (preset);
        return true;
    }

    namespace
    {
        struct Bank
        {
            std::vector<PresetDef> presets;
            std::vector<std::string> errors;
        };

        const Bank& bank()
        {
            static const Bank b = []
            {
                Bank result;

                for (int i = 0; i < FactoryPresetData::entryCount; ++i)
                {
                    const auto& entry = FactoryPresetData::entries[i];

                    PresetDef def;
                    std::string error;
                    if (! parsePresetText (entry.text, def, error))
                    {
                        result.errors.push_back (std::string (entry.name) + ": " + error);
                        continue;
                    }

                    result.presets.push_back (std::move (def));
                }

                return result;
            }();

            return b;
        }
    }

    const std::vector<PresetDef>& factoryPresets()
    {
        return bank().presets;
    }

    const std::vector<std::string>& factoryPresetErrors()
    {
        return bank().errors;
    }
}
