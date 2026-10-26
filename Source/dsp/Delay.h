#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

/** Tempo-synced analog-style stereo delay, the colour of the SquelchBox analog
    tap: the one-pole lowpass sits in the feedback path AND in front of the wet
    output, so the very first repeat is already darkened, and the feedback gain
    never exceeds 0.9 so the tail always dies out. The length is a plain
    seconds figure the caller derives from the tempo and the chosen division;
    the fractional read position makes a tempo change slide the echo line
    instead of re-triggering it.
    JuceLite-compatible: plain C++, no JUCE types, so the WebAssembly core
    compiles this header too, bit for bit against the native build. */
class FxDelay
{
public:
    /** Longest divider is a quarter and the tempo is clamped at 20 BPM, so a
        hair above three seconds covers the slowest legal echo. */
    static constexpr double kMaxSeconds = 3.1;

    void prepare (double newSampleRate)
    {
        sampleRate = (newSampleRate > 1.0 && std::isfinite (newSampleRate)) ? newSampleRate : 48000.0;
        const double capacity = std::ceil (sampleRate * kMaxSeconds) + 8.0;
        for (int channel = 0; channel < kMaxChannels; ++channel)
        {
            buffer[channel].assign (capacity, 0.0f);
            line[channel] = {};
        }
        lpToneHz = 3000.0;
        lpGain = 1.0;
        feedback = 0.35f;
        mixValue = 0.0f;
        delaySamples = sampleRate * 0.25;
    }

    void reset()
    {
        for (int channel = 0; channel < kMaxChannels; ++channel)
        {
            std::fill (buffer[channel].begin(), buffer[channel].end(), 0.0f);
            line[channel] = {};
        }
    }

    /** Per-block parameters. `seconds` is clamped into the capacity range so a
        runaway automation value cannot write past the line; `feedback` already
        arrives 0..0.9 from the parameter range and is clamped again here because
        a MIDI-map CC bypasses the parameter's own range. */
    void setParameters (double seconds, float feedbackNorm, double toneHz, float mix)
    {
        seconds = seconds <= 0.02 ? 0.02 : (seconds >= kMaxSeconds ? kMaxSeconds * 0.98 : seconds);
        delaySamples = seconds * sampleRate;
        feedback = feedbackNorm <= 0.0f ? 0.0f : (feedbackNorm >= 0.9f ? 0.9f : feedbackNorm);
        mixValue = mix <= 0.0f ? 0.0f : (mix >= 1.0f ? 1.0f : mix);
        setToneHz ((toneHz > 100.0 && toneHz < 40000.0 && std::isfinite (toneHz)) ? toneHz : 3000.0);
    }

    /** One-pole coefficient for the feedback/output lowpass. A zero state plus a
        silent input would denormal the accumulator, so the state is flushed like
        every other feedback state in this project. */
    void setToneHz (double hz)
    {
        lpToneHz = hz;
        lpGain = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * hz / sampleRate);
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        if (mixValue <= 0.0f)
            return;

        const int count = numChannels < kMaxChannels ? numChannels : kMaxChannels;
        for (int channel = 0; channel < count; ++channel)
            processChannel (channels[channel], numSamples, line[channel], buffer[channel]);
    }

private:
    static constexpr int kMaxChannels = 2;

    struct LineState
    {
        // Write cursor and the fractional read offset, so a tempo change moves
        // the tap instead of jumping it.
        size_t write = 0;
        double fractional = 0.0;
        float lp = 0.0f;
    };

    void processChannel (float* samples, int numSamples, LineState& state, std::vector<float>& buf)
    {
        const size_t n = buf.size();
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = samples[i];

            // Linear-interpolated read behind the write head.
            double read = static_cast<double> (state.write) - delaySamples;
            while (read < 0.0)
                read += static_cast<double> (n);
            const size_t i0 = static_cast<size_t> (read);
            const size_t i1 = i0 + 1 < n ? i0 + 1 : 0;
            const double frac = read - static_cast<double> (i0);
            const float delayed = buf[i0] + static_cast<float> ((buf[i1] - buf[i0]) * frac);

            // The lowpass darkens both the feedback write and the wet output:
            // that is what makes the first repeat already warm.
            state.lp += static_cast<float> (lpGain) * (delayed - state.lp);
            if (std::fabs (state.lp) < 1e-15f)
                state.lp = 0.0f;
            const float wet = state.lp;

            buf[state.write] = dry + feedback * wet;
            state.write = state.write + 1 < n ? state.write + 1 : 0;

            samples[i] = dry + mixValue * (wet - dry);
        }
    }

    double sampleRate = 48000.0;
    double delaySamples = 48000.0 * 0.25;
    double lpGain = 1.0;
    double lpToneHz = 3000.0;
    float feedback = 0.35f;
    float mixValue = 0.0f;
    LineState line[kMaxChannels];
    std::vector<float> buffer[kMaxChannels];
};

/** Beat multipliers for the Delay Div choices ("1/4", "1/8", "1/8.", "1/16",
    "1/8T"), shared by the two processors: the order matches the choice list
    declared in PluginParameters.h and Source/wclap/ParamTable.cpp. */
inline const double* delayDivisionBeats()
{
    static const double table[5] = { 1.0, 0.5, 0.75, 0.25, 1.0 / 3.0 };
    return table;
}
