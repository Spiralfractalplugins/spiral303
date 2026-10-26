#pragma once

#include <JuceHeader.h>

#include "../PluginProcessor.h"

/** Bounces the current pattern to a mono 24-bit WAV file. Runs a private processor
    instance offline on the calling thread, so it must never be called from the audio
    thread. */
namespace WavExport
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSamples = 512;
    constexpr int maxSeconds = 30;

    /** Renders the given number of pattern loops. Returns false if the file could
        not be written. */
    bool renderPattern (PluginProcessor& source, const juce::File& file, int numBars);
}
