#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/** A small Schroeder room, the mono tail of the SquelchBox reverb: predelay,
    four parallel feedback combs with mutually prime lengths
    (1117/1188/1277/1356 samples scaled by sr/48000), then two series allpass
    stages (556/441, gain 0.5). Tuned for a short room, not a cathedral: the
    comb gain law keeps the tail from ringing away at full decay.
    JuceLite-compatible: plain C++, no JUCE types, so the WebAssembly core
    compiles this header too, bit for bit against the native build. */
class FxReverb
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = (newSampleRate > 1.0 && std::isfinite (newSampleRate)) ? newSampleRate : 48000.0;
        const double scale = sampleRate / 48000.0;

        const int combSamples[4] = { 1117, 1188, 1277, 1356 };
        for (int i = 0; i < 4; ++i)
        {
            // At least 8 samples so the index maths stays sane at any rate.
            combLengths[i] = (int) std::min (std::max (8.0, std::floor (combSamples[i] * scale)), 40000.0);
            comb[i].assign (combLengths[i], 0.0f);
            combIndex[i] = 0;
        }

        // Predelay: 480 samples at 48 kHz, ~10 ms of air before the tail.
        predelayLength = (int) std::max (4.0, std::floor (480.0 * scale));
        predelay.assign (predelayLength, 0.0f);
        predelayIndex = 0;

        const int allpassSamples[2] = { 556, 441 };
        for (int i = 0; i < 2; ++i)
        {
            allpassLengths[i] = (int) std::max (4.0, std::floor (allpassSamples[i] * scale));
            allpass[i].assign (allpassLengths[i], 0.0f);
            allpassIndex[i] = 0;
        }

        combGain = 0.7f * (0.3f + 0.7f * 0.5f);
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            std::fill (comb[i].begin(), comb[i].end(), 0.0f);
            combIndex[i] = 0;
        }
        std::fill (predelay.begin(), predelay.end(), 0.0f);
        predelayIndex = 0;
        for (int i = 0; i < 2; ++i)
        {
            std::fill (allpass[i].begin(), allpass[i].end(), 0.0f);
            allpassIndex[i] = 0;
        }
    }

    /** 0 = a thin, listless room, 1 = full room; the comb gain never reaches
        the ringing region, so the tail always dies out. */
    void setDecay (float decayNorm)
    {
        const float d = decayNorm <= 0.0f ? 0.0f : (decayNorm >= 1.0f ? 1.0f : decayNorm);
        combGain = 0.7f * (0.3f + 0.7f * d);
    }

    /** Wet-only return: the dry path stays with the caller, so the reverb can
        ride the mid channel as a send while the stereo sides keep their width. */
    float processSample (float input)
    {
        // Predelay ring: read the sample that is about to be overwritten, so the
        // tail starts predelayLength samples after the input.
        const float delayed = predelay[predelayIndex];
        predelay[predelayIndex] = input;
        predelayIndex = predelayIndex + 1 < predelayLength ? predelayIndex + 1 : 0;

        // Four parallel feedback combs: the state is a one-sample-delayed
        // (input + gain * out) pair, and each cell is flushed when it dies into
        // the denormal range so a fading tail reads an exact zero.
        float wet = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float out = comb[i][combIndex[i]];
            wet += out;
            float v = delayed + combGain * out;
            if (std::fabs (v) < 1e-15f)
                v = 0.0f;
            comb[i][combIndex[i]] = v;
            combIndex[i] = combIndex[i] + 1 < combLengths[i] ? combIndex[i] + 1 : 0;
        }
        float x = wet * 0.25f;

        // Two series Schroeder allpass stages (gain 0.5): out = -x + buf,
        // buf = x + 0.5 * buf (freeverb shape).
        for (int i = 0; i < 2; ++i)
        {
            const float bufout = allpass[i][allpassIndex[i]];
            const float out = bufout - x;
            allpass[i][allpassIndex[i]] = x + 0.5f * bufout;
            allpassIndex[i] = allpassIndex[i] + 1 < allpassLengths[i] ? allpassIndex[i] + 1 : 0;
            x = out;
        }
        return x;
    }

private:
    double sampleRate = 48000.0;
    int combLengths[4] = { 1117, 1188, 1277, 1356 };
    int allpassLengths[2] = { 556, 441 };
    int predelayLength = 480;
    size_t combIndex[4] = {};
    size_t allpassIndex[2] = {};
    size_t predelayIndex = 0;
    float combGain = 0.5f;
    std::vector<float> comb[4];
    std::vector<float> allpass[2];
    std::vector<float> predelay;
};
