#include <functional>
#include <vector>

#include <JuceHeader.h>

#include "../Source/dsp/Engine.h"
#include "../Source/dsp/Delay.h"
#include "../Source/dsp/Reverb.h"
#include "CoreParityScenario.h"

// The WebAssembly flavour of the core, compiled in another translation unit.
extern "C" void spiral303_core_render (float* left, float* right, int frames);
extern "C" void spiral303_core_render_fx (float* left, float* right, int frames);

/** Renders one scenario through both flavours and checks the samples against
    each other, the worst difference reported in the message. `fxOn` picks the
    tail twin: the same pattern with the delay and the reverb running. */
static void compareParityScenarios (const std::function <void (bool, const juce::String&)>& check,
                                    int frames, bool fxOn)
{
    using namespace juce;

    std::vector<float> juceLeft, juceRight;
    if (fxOn)
        renderParityPatternFx (juceLeft, juceRight, frames);
    else
        renderParityPattern (juceLeft, juceRight, frames);

    std::vector<float> coreLeft ((size_t) frames), coreRight ((size_t) frames);
    if (fxOn)
        spiral303_core_render_fx (coreLeft.data(), coreRight.data(), frames);
    else
        spiral303_core_render (coreLeft.data(), coreRight.data(), frames);

    double worst = 0.0;
    int worstIndex = -1;
    bool same = true;
    for (int i = 0; i < frames; ++i)
    {
        const double dl = std::fabs ((double) juceLeft[(size_t) i] - (double) coreLeft[(size_t) i]);
        const double dr = std::fabs ((double) juceRight[(size_t) i] - (double) coreRight[(size_t) i]);
        const double d = std::max (dl, dr);
        if (d > worst)
        {
            worst = d;
            worstIndex = i;
        }
        if (d != 0.0)
            same = false;
    }

    check (same, fxOn
           ? String ("the WebAssembly core renders the FX tail bit for bit like the native one")
           : String ("the WebAssembly core renders bit for bit like the native one")
           + (worstIndex >= 0
                  ? String (" (worst difference ") + String (worst) + " at sample "
                    + String (worstIndex) + ")"
                  : String()));

    // The pattern has to make sound in the first place, otherwise "identical
    // silence" would pass the comparison above.
    double peak = 0.0;
    for (int i = 0; i < frames; ++i)
        peak = std::max (peak, std::fabs ((double) juceLeft[(size_t) i]));
    check (peak > 0.01, (fxOn ? String ("the FX parity pattern makes sound (peak ")
                          : String ("the parity pattern makes sound (peak "))
           + String (peak, 5) + ")");

    // The stereo path must not be a copy of silence either: the right channel
    // is written by the float vector copy in the engine.
    double rightPeak = 0.0;
    for (int i = 0; i < frames; ++i)
        rightPeak = std::max (rightPeak, std::fabs ((double) juceRight[(size_t) i]));
    check (rightPeak > 0.01, "the right channel carries the pattern too (peak "
           + String (rightPeak, 5) + ")");
}

void runCoreParityTests (const std::function <void (bool, const juce::String&)>& check)
{
    // The point of the stand-in in Source/core/JuceLite.h is that the WebAssembly
    // build sounds exactly like the native one. This renders the same pattern
    // through both and compares the samples: the core is bit-exact sensitive,
    // so a single ULP of drift in a clamp or in the random generator shows up
    // here as a difference.
    const int frames = 4096;

    compareParityScenarios (check, frames, false);
    compareParityScenarios (check, frames, true);
}
