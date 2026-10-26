#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spiral303
{
    struct PresetValue
    {
        std::string paramId;
        float rawValue = 0.0f;
    };

    struct PresetDef
    {
        std::string name;
        std::vector<PresetValue> values;
        std::string sequence;   // the sequencer text; empty keeps the current pattern
        std::string gate;       // the gate text; empty keeps the current gate
        std::string credit;     // the attribution line the preset menu shows
        std::string comment;    // the user's own note
    };

    /** Reads one .sp303preset object. Returns false and says what is wrong. */
    bool parsePresetText (const std::string& jsonText, PresetDef& out, std::string& error);

    /** The factory presets, in file-name order. */
    const std::vector<PresetDef>& factoryPresets();

    /** The presets that did not load, as "name: reason". */
    const std::vector<std::string>& factoryPresetErrors();
}
