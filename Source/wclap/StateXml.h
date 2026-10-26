#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ParamTable.h"

namespace spiral303
{
    /** Everything the plugin keeps between sessions. */
    struct PluginState
    {
        float values[kParamCount] {};
        std::string sequence;
        std::string gate;
        std::string presetNote;
    };

    /** Writes the state in the format the native build uses: JUCE's value tree
        XML inside the container JUCE's copyXmlToBinary writes. A session saved
        in one build loads in the other. */
    std::vector<uint8_t> encodeState (const PluginState& state);

    /** Reads a state in that format. Returns false when the data is not a state
        this build understands; the state is then left untouched. */
    bool decodeState (const void* data, size_t size, PluginState& state);
}
