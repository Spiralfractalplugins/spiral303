#pragma once

#include <algorithm>
#include <cmath>

/** Morphing oscillator: sine at 0, through saw at 0.5, to square at 1.
    Every discontinuity of the naive waves gets a polyBLEP correction spread over the
    two samples that straddle the jump. Since a jump is only ever detected when it
    falls inside the increment that was just taken, that window is at most one sample
    wide on each side, so one held value is enough to carry it into the next call.
    The analytic sine leg cannot be corrected that way, so a gentle one-pole sits
    above it; at 303 pitches that only trims the very top. */
class WaveOsc
{
public:
    void prepare (double sampleRateHz)
    {
        sampleRate = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz;
        // A pole at 30% of the sample rate: transparent below the audio band and it
        // takes the edge off whatever alias the corrections above cannot reach.
        dampA = std::min (0.95f, std::max (0.05f, static_cast<float> (std::exp (-2.0f * pi * 0.30f))));
        pending = 0.0f;
    }

    /** 0 = sine, 0.5 = saw, 1 = square. */
    void setMorph (float m) { morph = std::min (1.0f, std::max (0.0f, m)); morphStage = (morph <= 0.5f) ? 0 : 1; }

    /** Virus-style supersaw stack (seven detuned saws). */
    void setHypersaw (bool on) { hypersaw = on ? 1.0f : 0.0f; }

    void setFrequency (float hz)
    {
        freq = std::min (static_cast<float> (sampleRate * 0.22), std::max (1.0f, hz));
    }

    void reset (float initialPhase = 0.0f) noexcept { phase = initialPhase - std::floor (initialPhase); pending = 0.0f; }

    float process()
    {
        const float inc = freq / static_cast<float> (sampleRate);
        const float previousPhase = phase;
        float out = naive();
        if (hypersaw <= 0.0f)
            out += pending;
        pending = 0.0f;

        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (hypersaw <= 0.0f)
                // The saw steps down at the wrap and the square steps up; the two land on
                // the same instant, so a single correction carries their difference.
                out += correct (2.0f * (sawLeg() - squareLeg()), (1.0f - previousPhase) / inc);
        }

        // The square flips again mid-cycle; the wrap already moved past that point,
        // so the next crossing is caught on the sample that steps over 0.5.
        if (hypersaw <= 0.0f && squareLeg() > 0.0f && previousPhase < 0.5f && phase >= 0.5f)
            out += correct (2.0f * squareLeg(), (0.5f - previousPhase) / inc);

        return damp (out);
    }

    float getPhase() const { return phase; }

private:
    // MSVC only exposes M_PI when _USE_MATH_DEFINES is set before <cmath>, which a
    // header that other headers include cannot promise, so pi lives in the class.
    static constexpr float pi = 3.14159265358979f;

    float naive() const
    {
        if (hypersaw > 0.0f)
        {
            // A small supersaw stack with the classic Virus-style detune spread.
            static constexpr float offsets[7] = { -0.030f, -0.020f, -0.010f, 0.0f, 0.010f, 0.020f, 0.030f };
            float sum = 0.0f;
            for (float o : offsets)
            {
                float p = phase + o;
                p -= std::floor (p);
                sum += 2.0f * p - 1.0f;
            }
            const float saws = sum / 7.0f;
            return hypersaw * saws + (1.0f - hypersaw) *
                   (morphStage == 0 ? ((1.0f - morph * 2.0f) * std::sin (2.0f * pi * phase)
                                     + morph * 2.0f * (2.0f * phase - 1.0f))
                                  : ((1.0f - ((morph - 0.5f) * 2.0f)) * (2.0f * phase - 1.0f)
                                     + ((morph - 0.5f) * 2.0f) * (phase < 0.5f ? 1.0f : -1.0f)));
        }

        if (morphStage == 0)
        {
            const float sine = std::sin (2.0f * pi * phase);
            return (1.0f - morph * 2.0f) * sine + morph * 2.0f * (2.0f * phase - 1.0f);
        }
        const float t = (morph - 0.5f) * 2.0f;
        const float saw = 2.0f * phase - 1.0f;
        const float square = phase < 0.5f ? 1.0f : -1.0f;
        return (1.0f - t) * saw + t * square;
    }

    float sawLeg() const noexcept { return morphStage == 0 ? morph * 2.0f : 1.0f - (morph - 0.5f) * 2.0f; }
    float squareLeg() const noexcept { return morphStage == 0 ? 0.0f : (morph - 0.5f) * 2.0f; }

    /** Correction of a jump that steps the naive wave up by `jump`, happening
        timeEnd (0..1) samples after the one being produced now. The tail of the
        window belongs to the next sample and is held in `pending`. */
    float correct (float jump, float timeEnd)
    {
        const float s = 0.5f * jump;
        const float u = 1.0f - std::min (1.0f, std::max (0.0f, timeEnd));
        pending += s * (1.0f - u) * (1.0f - u);
        return -s * u * u;
    }

    float damp (float x)
    {
        damped = dampA * damped + (1.0f - dampA) * x;
        return damped;
    }

    double sampleRate = 48000.0;
    float freq = 110.0f, morph = 0.5f, hypersaw = 0.0f, phase = 0.0f, dampA = 0.15f, damped = 0.0f, pending = 0.0f;
    int morphStage = 0;
};
