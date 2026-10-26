#pragma once

#include <JuceHeader.h>

#include "../dsp/FilterResponse.h"

/** The scope's axes as data: x is log frequency, y is decibels. The grid used to be
    drawn at four equal pixel gaps, which put every line on no frequency at all. */
struct ScopeAxes
{
    static constexpr float kFreqMin = 20.0f, kFreqMax = 20000.0f;
    /** The top of the window is the cascade peak at full resonance, so the tallest
        honest hump touches it and nothing about it is cut off. */
    static constexpr float kMaxQ = 18.5f;                     // 0.5 + 1.0^2 * 18
    static constexpr float kDbTop = 25.46f, kDbBottom = -48.0f;

    juce::Rectangle<float> plot;

    float freqToX (float f) const
    {
        return plot.getX() + std::log (f / kFreqMin) / std::log (kFreqMax / kFreqMin) * plot.getWidth();
    }

    float xToFreq (float x) const
    {
        return kFreqMin * std::pow (kFreqMax / kFreqMin, (x - plot.getX()) / plot.getWidth());
    }

    /** Unclamped on purpose: the top of the window is the honest peak at full
        resonance, so anything that still leaves the plot is the skirt running below
        -48 dB, which the clip region cuts off rather than smearing over the panel. */
    float dbToY (float db) const
    {
        return juce::jmap (db, kDbBottom, kDbTop, plot.getBottom(), plot.getY());
    }

    float yToDb (float y) const
    {
        return juce::jmap (y, plot.getBottom(), plot.getY(), kDbBottom, kDbTop);
    }

    /** Second-order lowpass magnitude in dB, blended between the single section
        (12 dB/oct) and the cascaded pair (24 dB/oct) the engine crossfades to. The
        resonance is counted exactly once: the full Q is split over the two sections,
        each running at sqrt(q). Peak of one^2: with u = r^2 the denominator
        g = (1-u)^2 + u/q is smallest at u = 1 - 1/(2q), where 1/g = q/(1 - 1/(4q)),
        which is 18.75 -> 25.46 dB at q = kMaxQ - hence kDbTop. */
    static float responseDb (float freq, float cutoff, float res, float slope)
    {
        return filterResponseDb (freq, cutoff, res, slope);
    }

    /** Decade minors with the 1-2-5 steps raised to majors, the way a hardware
        analyser labels itself. */
    static const std::vector<float>& gridFrequencies()
    {
        static const std::vector<float> freqs = []
        {
            std::vector<float> out;
            for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
                for (int step = 1; step <= 9; ++step)
                {
                    const float f = decade * static_cast<float> (step);
                    if (f >= kFreqMin && f <= kFreqMax)
                        out.push_back (f);
                }
            std::sort (out.begin(), out.end());
            return out;
        }();
        return freqs;
    }

    static bool isMajorFrequency (float f)
    {
        const float lead = f / std::pow (10.0f, std::floor (std::log10 (f)));
        return lead == 1.0f || lead == 2.0f || lead == 5.0f;
    }

    /** Horizontal grid every 12 dB, 0 dB carried by the caller as the brighter line. */
    static const std::vector<float>& gridDecibels()
    {
        static const std::vector<float> db = { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f };
        return db;
    }
};
