// The WebAssembly flavour of the audio core.
//
// The same sources as the native build, but compiled against the JUCE
// stand-in in Source/core/JuceLite.h instead of JUCE itself. It sits in its
// own namespace so that both flavours can live in the same test binary and be
// compared sample by sample.
//
// The standard headers are pulled in first, at global scope: the core headers
// include them again, and a header that is already included is skipped by its
// include guard - which is what makes it possible to include the whole core
// inside a namespace at all.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#define SPIRAL303_CORE_ONLY 1

namespace coreparity
{
#include "../Source/dsp/Engine.h"
#include "../Source/dsp/Delay.h"
#include "../Source/dsp/Reverb.h"

// The sequencer's methods live in a .cpp; including it here gives the core
// flavour its own copy of them (the native build compiles the same file
// separately, at global scope).
#include "../Source/dsp/Sequence.cpp"

#include "CoreParityScenario.h"
}

#undef SPIRAL303_CORE_ONLY

extern "C"
{
    /** Renders the parity pattern through the core flavour. */
    void spiral303_core_render (float* left, float* right, int frames)
    {
        std::vector<float> l, r;
        coreparity::renderParityPattern (l, r, frames);
        std::memcpy (left, l.data(), sizeof (float) * (size_t) frames);
        std::memcpy (right, r.data(), sizeof (float) * (size_t) frames);
    }

    /** The FX twin: the same pattern with the delay and the reverb engaged. */
    void spiral303_core_render_fx (float* left, float* right, int frames)
    {
        std::vector<float> l, r;
        coreparity::renderParityPatternFx (l, r, frames);
        std::memcpy (left, l.data(), sizeof (float) * (size_t) frames);
        std::memcpy (right, r.data(), sizeof (float) * (size_t) frames);
    }
}
