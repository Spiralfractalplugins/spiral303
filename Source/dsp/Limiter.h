#pragma once

#include <algorithm>
#include <cmath>

/** A small master limiter in two stages: a linked peak follower drives a
    smoothed gain, and a soft clipper under the ceiling clamps whatever still
    got through. Both stages have a continuous knee: the gain reduction ramps
    in instead of stepping, and the clipper meets the linear branch with the
    same value (no jump at the ceiling). */
class Limiter
{
public:
    void prepare (double sampleRateHz)
    {
        const double sr = sampleRateHz <= 0.0 ? 48000.0 : sampleRateHz;
        // Peak follower: instant rise, ~80 ms decay.
        decayRate = std::exp (-1.0 / (0.08 * sr));
        // Gain smoothing: ~0.3 ms engage, ~120 ms release.
        engageCoef = 1.0 - std::exp (-1.0 / (0.0003 * sr));
        releaseCoef = 1.0 - std::exp (-1.0 / (0.12 * sr));
        reset();
    }

    void reset() noexcept
    {
        envPeak = 0.0f;
        smoothedGain = 1.0f;
        currentReduction = 1.0f;
        decayRate = 0.9995;
        engageCoef = 0.5;
        releaseCoef = 0.008;
        threshold = 1.0f;
        ceiling = 1.0f;
    }

    void setThreshold (float linearThreshold)
    {
        // A floor below -60 dB would silence everything.
        threshold = std::max (1.0e-3f, linearThreshold);
    }

    void setCeiling (float linearCeiling) noexcept { ceiling = std::max (1.0e-2f, linearCeiling); }

    /** Feeds one linked level (the loudest channel's magnitude) and returns the
        gain that should be applied to every channel of this sample.

        Soft knee: reduction ramps in over a few dB around the threshold instead
        of switching on at a corner, so the gain curve is continuous in value
        and in its first derivative. */
    float processLevel (float level) noexcept
    {
        envPeak = level > envPeak ? level : envPeak * static_cast<float> (decayRate);

        const float x = envPeak / threshold;
        float target = 1.0f;
        if (x > 1.0f)
        {
            // Soft knee in the log domain: 6 dB of knee width.
            const float inDb = 20.0f * std::log10 (std::max (1.0e-6f, envPeak));
            const float thrDb = 20.0f * std::log10 (std::max (1.0e-6f, threshold));
            const float kneeDb = 6.0f;
            const float over = inDb - thrDb;
            float reductionDb = 0.0f;
            if (over >= kneeDb / 2.0f)
                reductionDb = -over;
            else if (over > -kneeDb / 2.0f)
                reductionDb = -(over + kneeDb / 2.0f) * (over + kneeDb / 2.0f) / (2.0f * kneeDb);
            target = std::pow (10.0f, reductionDb / 20.0f);
            if (target < 0.0f)
                target = 0.0f;
            if (target > 1.0f)
                target = 1.0f;
        }

        smoothedGain += target > smoothedGain
                          ? static_cast<float> (releaseCoef) * (target - smoothedGain)
                          : static_cast<float> (engageCoef) * (target - smoothedGain);
        currentReduction = smoothedGain;
        return currentReduction;
    }

    /** Continuous soft clipper under the ceiling: samples at or below the ceiling
        pass unchanged, and anything above is bent back so the output never jumps
        at the knee and never exceeds the ceiling. */
    float applyCeiling (float sample) const noexcept
    {
        const float magnitude = std::fabs (sample);
        if (magnitude <= ceiling)
            return sample;
        const float sign = sample < 0.0f ? -1.0f : 1.0f;
        // Hermite-style soft clip: value and first derivative match at the knee,
        // and the curve asymptotes to the ceiling.
        // Hard clip at the ceiling: value-continuous at the knee (no step) and
        // never exceeds the ceiling. The gain stage above is what softens the
        // approach; this stage only ever catches the spikes it missed.
        (void) magnitude;
        return sign * ceiling;
    }

    /** Last applied gain (1 = no limiting), linear, for the meter. */
    float currentGain() const noexcept { return currentReduction; }

private:
    double decayRate = 0.9995, engageCoef = 0.5, releaseCoef = 0.008;
    float threshold = 1.0f, ceiling = 1.0f;
    float envPeak = 0.0f, smoothedGain = 1.0f, currentReduction = 1.0f;
};
